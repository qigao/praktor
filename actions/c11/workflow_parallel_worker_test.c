/* True worker-thread qualification, not another queue/scheduler.
 * The ONLY task queue and callback ownership belong to Salts CFlow.
 * Branches own independent optimistic transactions; the single caller/owner
 * commits typed outputs only AFTER worker wait_idle proves terminal settlement.
 * Shared gate/counters are atomic so ASan/TSan can assess the handoff.
 */
#include "workflow_parallel_process_guard.h"
#include <cflow/executor.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <string.h>
#include <tinytest.h>

typedef struct worker_gate {
    atomic_uint started;
    atomic_uint in_flight;
    atomic_uint peak;
    atomic_bool release;
} worker_gate;
typedef struct worker_branch {
    worker_gate *gate;
    praktor_c11_value_txn *transaction;
    const char *key, *value;
    bool fail_run;
    atomic_uint ran, cancelled, finalized;
    atomic_int write_status;
} worker_branch;

static void gate_init(worker_gate *gate) {
    atomic_init(&gate->started,0u);
    atomic_init(&gate->in_flight,0u);
    atomic_init(&gate->peak,0u);
    atomic_init(&gate->release,false);
}
static void branch_init(worker_branch *branch,worker_gate *gate,
                        praktor_c11_value_txn *transaction,
                        const char *key,const char *value,bool fail_run) {
    memset(branch,0,sizeof(*branch));
    branch->gate=gate;
    branch->transaction=transaction;
    branch->key=key;branch->value=value;
    branch->fail_run=fail_run;
    atomic_init(&branch->ran,0u);
    atomic_init(&branch->cancelled,0u);
    atomic_init(&branch->finalized,0u);
    atomic_init(&branch->write_status,PRAKTOR_C11_VALUE_EINVAL);
}
static void branch_run(void *user) {
    worker_branch *branch=(worker_branch *)user;
    worker_gate *gate=branch->gate;
    unsigned live=atomic_fetch_add(&gate->in_flight,1u)+1u;
    unsigned high=atomic_load(&gate->peak);
    while(high<live&&!atomic_compare_exchange_weak(&gate->peak,&high,live)) {}
    atomic_fetch_add(&branch->ran,1u);
    atomic_fetch_add(&gate->started,1u);
    while(!atomic_load(&gate->release))cmeta_sleep_ms(1u);
    if(!branch->fail_run)
        atomic_store(&branch->write_status,
            praktor_c11_value_txn_set(branch->transaction,branch->key,branch->value));
    atomic_fetch_sub(&gate->in_flight,1u);
}
static void branch_cancel(void *user) {
    worker_branch *branch=(worker_branch *)user;
    atomic_fetch_add(&branch->cancelled,1u);
}
static void branch_finalize(void *user) {
    worker_branch *branch=(worker_branch *)user;
    atomic_fetch_add(&branch->finalized,1u);
}
static cflow_executor_task descriptor(worker_branch *branch) {
    cflow_executor_task task={0};
    task.run=branch_run;task.cancel=branch_cancel;
    task.finalize=branch_finalize;task.user=branch;
    return task;
}
static bool await_started(worker_gate *gate,unsigned count) {
    unsigned elapsed=0u;
    while(atomic_load(&gate->started)<count&&elapsed++<6000u)
        cmeta_sleep_ms(1u);
    return atomic_load(&gate->started)>=count;
}
static praktor_workflow_task_registry *test_registry(void) {
    static const char *yaml=
        "tasks:\n"
        "  - {name: left, command: echo left}\n"
        "  - {name: right, command: echo right}\n";
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_workflow_task_registry *registry=NULL;
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(snapshot,&registry)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return registry;
}
static bool text_is(const praktor_c11_value_store *store,const char *key,
                    const char *expected) {
    char buffer[64]={0};size_t n=0;
    return praktor_c11_value_store_get(store,key,buffer,sizeof buffer,&n)==
        PRAKTOR_C11_VALUE_OK&&n==strlen(expected)&&strcmp(buffer,expected)==0;
}
static bool output_is(const praktor_workflow_task_registry *registry,
                      size_t task,const char *key,const char *expected) {
    const json_value_t *value=NULL;char *encoded;bool equal=false;
    if(praktor_workflow_task_registry_get_output(registry,task,
            vstr_from_cstr(key),&value)!=SALTS_OK)return false;
    encoded=json_serialize(value,NULL);
    if(encoded) {
        equal=strcmp(encoded,expected)==0;
        json_serialize_free(encoded);
    }
    return equal;
}
static bool terminal_callbacks(const worker_branch *branch) {
    return atomic_load(&branch->ran)+atomic_load(&branch->cancelled)==1u&&
           atomic_load(&branch->finalized)==1u;
}
spec("Salts CFlow WorkerExecutor C11 Parallel owner-turn qualification") {
    it("runs truly overlapping workers but only owner publishes after wait_idle") {
        cflow_executor executor={0};cflow_executor_control control={0};
        praktor_workflow_task_registry *registry=test_registry();
        praktor_c11_value_store root={0};
        cflow_process_retainer retainer={0};
        size_t process_pending=SIZE_MAX;
        praktor_c11_value_txn txns[2]={{0},{0}};
        worker_gate gate;worker_branch branches[2];
        const char *a_text="{\"result\":{\"id\":1}}";
        const char *b_text="{\"result\":{\"id\":2}}";
        json_value_t *a=json_parse(a_text,strlen(a_text));
        json_value_t *b=json_parse(b_text,strlen(b_text));
        praktor_workflow_task_output_update outputs[2]={{0u,a},{1u,b}};
        cflow_executor_protocol_stats stats={0};
        check(a&&b);gate_init(&gate);
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_store_set(&root,"seed","base")==PRAKTOR_C11_VALUE_OK);
        check(cflow_process_retainer_init(&retainer,1u)==SALTS_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,2u,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task;
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
            branch_init(&branches[i],&gate,&txns[i],i?"right":"left",i?"R":"L",false);
            task=descriptor(&branches[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        const bool both_started=await_started(&gate,2u);
        const unsigned peak=atomic_load(&gate.peak);
        check(both_started);
        check(peak>=2u);
        /* Both callbacks have started, but no result is published yet. */
        check(praktor_c11_value_store_size(&root)==1u);
        atomic_store(&gate.release,true); /* always release even on test failure */
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.accepted==2u&&stats.completed==2u&&stats.cancelled==0u);
        for(size_t i=0;i<2u;++i) {
            check(terminal_callbacks(&branches[i]));
            check(atomic_load(&branches[i].ran)==1u);
            check(atomic_load(&branches[i].write_status)==PRAKTOR_C11_VALUE_OK);
        }
        /* CLOSED admission + idle callbacks + empty retainer are required,
         * not merely an optimistic txn with two successful worker returns. */
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            registry,outputs,txns,2u,&control,&retainer,8u,&process_pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(process_pending==0u);
        check(!txns[0].impl&&!txns[1].impl);
        json_free(a);json_free(b);
        check(text_is(&root,"seed","base")&&text_is(&root,"left","L")&&
              text_is(&root,"right","R"));
        for(size_t i=0;i<2u;++i)
            check(praktor_workflow_task_registry_complete(registry,i)==SALTS_OK);
        check(output_is(registry,0u,"result","{\"id\":1}"));
        check(output_is(registry,1u,"result","{\"id\":2}"));
        cflow_executor_destroy(&executor);
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("cancels queued task while one worker is active, then drains before abort") {
        cflow_executor executor={0};cflow_executor_control control={0};
        praktor_workflow_task_registry *registry=test_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        worker_gate gate;worker_branch branches[2];
        cflow_executor_protocol_stats stats={0};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        gate_init(&gate);
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,1u,2u));
        check(cflow_executor_as_control(&executor,&control));
        check(praktor_c11_value_txn_begin(&txns[0],&root)==PRAKTOR_C11_VALUE_OK);
        branch_init(&branches[0],&gate,&txns[0],"left","L",false);
        cflow_executor_task first=descriptor(&branches[0]);
        check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
        check(await_started(&gate,1u));
        check(praktor_c11_value_txn_begin(&txns[1],&root)==PRAKTOR_C11_VALUE_OK);
        branch_init(&branches[1],&gate,&txns[1],"right","R",false);
        cflow_executor_task second=descriptor(&branches[1]);
        check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_ACCEPTED);
        /* Shutdown cancels queued work; the already running worker still owns
         * its transaction until it is released and finalized. */
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        atomic_store(&gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.accepted==2u&&stats.completed==1u&&stats.cancelled==1u);
        check(terminal_callbacks(&branches[0]));
        check(terminal_callbacks(&branches[1]));
        check(atomic_load(&branches[0].ran)==1u);
        check(atomic_load(&branches[1].ran)==0u);
        check(atomic_load(&branches[1].cancelled)==1u);
        /* Caller aborts even the completed branch: all-or-none. */
        praktor_c11_value_txn_abort(&txns[0]);
        praktor_c11_value_txn_abort(&txns[1]);
        check(praktor_c11_value_store_size(&root)==0u);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        cflow_executor_destroy(&executor);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("one settled failure prevents a sibling success from being published") {
        cflow_executor executor={0};cflow_executor_control control={0};
        praktor_workflow_task_registry *registry=test_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn txns[2]={{0},{0}};
        worker_gate gate;worker_branch branches[2];
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        gate_init(&gate);
        check(praktor_c11_value_store_init(&root,3u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,2u,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0;i<2u;++i) {
            cflow_executor_task task;
            check(praktor_c11_value_txn_begin(&txns[i],&root)==PRAKTOR_C11_VALUE_OK);
            branch_init(&branches[i],&gate,&txns[i],
                i?"right":"left",i?"R":"L",i==1u);
            task=descriptor(&branches[i]);
            check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(await_started(&gate,2u));
        atomic_store(&gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(terminal_callbacks(&branches[0])&&terminal_callbacks(&branches[1]));
        check(atomic_load(&branches[0].write_status)==PRAKTOR_C11_VALUE_OK);
        check(atomic_load(&branches[1].write_status)==PRAKTOR_C11_VALUE_EINVAL);
        for(size_t i=0;i<2u;++i)praktor_c11_value_txn_abort(&txns[i]);
        check(praktor_c11_value_store_size(&root)==0u);
        for(size_t i=0;i<2u;++i) {
            check(praktor_workflow_task_registry_state(registry,i,&state)==SALTS_OK);
            check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        }
        cflow_executor_destroy(&executor);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
}
