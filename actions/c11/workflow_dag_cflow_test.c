/* Qualification-only C11 DAG + canonical Salts CFlow integration.
 * The DAG is a single-owner readiness counter, not another task queue.
 * CFlow executes callbacks and finalizes borrowed descriptors. Only AFTER
 * wait_idle do we settle the DAG on the owning application thread.
 * This does not exercise the production WorkflowExecutor/trigger scheduler,
 * NativeIO Process leases or atomic Parallel output publication. */
#include "yml/workflow_dag_state.h"
#include <cflow/executor.h>
#include <salts/thread.h>
#include <json_parser.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

enum { DAG_TASKS = 5u };

typedef struct dag_gate {
    atomic_uint started, in_flight, peak;
    atomic_bool release;
} dag_gate;

typedef struct dag_probe {
    dag_gate *gate;
    bool block, fail_run;
    atomic_uint ran, cancelled, finalized;
    /* +1 = successful run; -1 = failed run; -2 = cancelled; 0 = pending */
    atomic_int result;
} dag_probe;

static void gate_init(dag_gate *gate) {
    atomic_init(&gate->started, 0u);
    atomic_init(&gate->in_flight, 0u);
    atomic_init(&gate->peak, 0u);
    atomic_init(&gate->release, false);
}
static void probe_init(dag_probe *probe,dag_gate *gate,
                       bool block,bool fail_run) {
    memset(probe,0,sizeof(*probe));
    probe->gate=gate;probe->block=block;probe->fail_run=fail_run;
    atomic_init(&probe->ran,0u);
    atomic_init(&probe->cancelled,0u);
    atomic_init(&probe->finalized,0u);
    atomic_init(&probe->result,0);
}
static void probe_run(void *user) {
    dag_probe *probe=(dag_probe *)user;
    atomic_fetch_add(&probe->ran,1u);
    if(probe->block) {
        dag_gate *gate=probe->gate;
        const unsigned live=atomic_fetch_add(&gate->in_flight,1u)+1u;
        unsigned peak=atomic_load(&gate->peak);
        while(peak<live&&!atomic_compare_exchange_weak(&gate->peak,&peak,live)) {}
        atomic_fetch_add(&gate->started,1u);
        while(!atomic_load(&gate->release))cmeta_sleep_ms(1u);
        atomic_fetch_sub(&gate->in_flight,1u);
    }
    atomic_store(&probe->result,probe->fail_run?-1:1);
}
static void probe_cancel(void *user) {
    dag_probe *probe=(dag_probe *)user;
    atomic_fetch_add(&probe->cancelled,1u);
    atomic_store(&probe->result,-2);
}
static void probe_finalize(void *user) {
    dag_probe *probe=(dag_probe *)user;
    atomic_fetch_add(&probe->finalized,1u);
}
static cflow_executor_task descriptor(dag_probe *probe) {
    cflow_executor_task task={0};
    task.run=probe_run;task.cancel=probe_cancel;
    task.finalize=probe_finalize;task.user=probe;
    return task;
}
static bool await_started(dag_gate *gate,unsigned wanted) {
    unsigned elapsed=0u;
    while(atomic_load(&gate->started)<wanted&&elapsed++<6000u)
        cmeta_sleep_ms(1u);
    return atomic_load(&gate->started)>=wanted;
}

/* Root tasks a and e. Two parallel children b/c depend on a, where c
 * deliberately has a duplicated edge. d depends on both b and c.
 * Graph identity survives destruction of the input JSON metadata. */
static praktor_workflow_dependencies *fixture_graph(void) {
    static const char *rows[DAG_TASKS]={
        "{\"name\":\"a\"}",
        "{\"name\":\"b\",\"depends_on\":[\"a\"]}",
        "{\"name\":\"c\",\"depends_on\":[\"a\",\"a\"]}",
        "{\"name\":\"d\",\"depends_on\":[\"b\",\"c\"]}",
        "{\"name\":\"e\"}"
    };
    json_value_t *owned[DAG_TASKS]={0};
    const json_value_t *borrowed[DAG_TASKS]={0};
    praktor_workflow_dependencies *graph=NULL;
    size_t i;
    for(i=0u;i<DAG_TASKS;++i) {
        owned[i]=json_parse(rows[i],strlen(rows[i]));
        check(owned[i]!=NULL);
        borrowed[i]=owned[i];
    }
    if(borrowed[0]&&borrowed[1]&&borrowed[2]&&borrowed[3]&&borrowed[4])
        check(praktor_workflow_dependencies_create(
            borrowed,DAG_TASKS,&graph,NULL,0u)==SALTS_OK);
    for(i=0u;i<DAG_TASKS;++i)if(owned[i])json_free(owned[i]);
    return graph;
}
static void init_probes(dag_probe probes[DAG_TASKS],dag_gate *gate,
                        bool gate_roots,bool fail_root) {
    size_t i;
    for(i=0u;i<DAG_TASKS;++i)
        probe_init(&probes[i],gate,gate_roots&&(i==0u||i==4u),
                   fail_root&&i==0u);
}
static void settle(praktor_workflow_dag_state *dag,
                   dag_probe probes[DAG_TASKS],size_t index,
                   unsigned expect_run,unsigned expect_cancel) {
    check(atomic_load(&probes[index].ran)==expect_run);
    check(atomic_load(&probes[index].cancelled)==expect_cancel);
    check(atomic_load(&probes[index].finalized)==1u);
    /* MSVC C11 rejects nested atomic_load in a multi-argument call.
     * Load after finalize, then pass a plain immutable success value. */
    const int callback_result=atomic_load(&probes[index].result);
    const bool succeeded=callback_result==1;
    check(praktor_workflow_dag_finish(dag,index,succeeded)==SALTS_OK);
}
static void require_terminal(praktor_workflow_dag_state *dag,
                             praktor_workflow_dag_terminal reason,
                             size_t completed,bool all_success) {
    praktor_workflow_dag_stats stats={0};
    check(praktor_workflow_dag_get_stats(dag,&stats)==SALTS_OK);
    check(stats.task_count==DAG_TASKS);
    check(stats.active==0u);
    check(stats.completed==completed);
    check(stats.ready==0u);
    check(stats.terminal==reason);
    check(stats.quiescent);
    check(stats.all_succeeded==all_success);
}
static void post_batch(cflow_executor *executor,
                       dag_probe probes[DAG_TASKS],
                       const size_t *indices,size_t count) {
    size_t i;
    for(i=0u;i<count;++i) {
        cflow_executor_task task=descriptor(&probes[indices[i]]);
        check(cflow_executor_try_post_task(executor,&task)==CFLOW_ADMISSION_ACCEPTED);
    }
}

/* A ManualExecutor has no threads: the sole DAG owner drives real callbacks
 * and only retires edges after all finalizers for the current wave. */
spec("C11 DAG with Salts CFlow ManualExecutor owner-turn admission") {
    it("drains three dependency waves while preserving duplicate edge counts") {
        praktor_workflow_dependencies *graph=fixture_graph();
        praktor_workflow_dag_state *dag=NULL;
        cflow_executor executor={0};cflow_executor_control control={0};
        dag_gate gate;dag_probe probes[DAG_TASKS];
        const size_t expected[3][2]={{0u,4u},{1u,2u},{3u,0u}};
        const size_t sizes[3]={2u,2u,1u};
        size_t chosen[2]={SIZE_MAX,SIZE_MAX},count=0u;
        gate_init(&gate);init_probes(probes,&gate,false,false);
        check(graph!=NULL);
        check(praktor_workflow_dag_state_create(graph,2u,&dag)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t wave=0u;wave<3u;++wave) {
            check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
            check(count==sizes[wave]);
            for(size_t i=0u;i<count;++i) {
                check(chosen[i]==expected[wave][i]);
                check(atomic_load(&probes[chosen[i]].ran)==0u);
            }
            post_batch(&executor,probes,chosen,count);
            check(cflow_executor_run_ready(&executor)==count);
            check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
            for(size_t i=0u;i<count;++i)
                settle(dag,probes,chosen[i],1u,0u);
        }
        require_terminal(dag,PRAKTOR_WORKFLOW_DAG_CONTINUE,DAG_TASKS,true);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        cflow_executor_destroy(&executor);
        praktor_workflow_dag_state_destroy(dag);
        praktor_workflow_dependencies_destroy(graph);
    }
}

/* Worker callbacks may execute concurrently and NEVER mutate the DAG. The
 * owner observes callback settlement before the next dependency wave. */
spec("C11 DAG with real CFlow WorkerExecutor callbacks") {
    it("runs overlapping root workers and releases successors only after finalize") {
        praktor_workflow_dependencies *graph=fixture_graph();
        praktor_workflow_dag_state *dag=NULL;
        cflow_executor executor={0};cflow_executor_control control={0};
        dag_gate gate;dag_probe probes[DAG_TASKS];
        size_t chosen[2]={SIZE_MAX,SIZE_MAX},count=0u;
        praktor_workflow_dag_stats stats={0};
        bool started;
        gate_init(&gate);init_probes(probes,&gate,true,false);
        check(graph!=NULL);
        check(praktor_workflow_dag_state_create(graph,2u,&dag)==SALTS_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,2u,2u));
        check(cflow_executor_as_control(&executor,&control));
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        post_batch(&executor,probes,chosen,count);
        started=await_started(&gate,2u);
        check(started);
        check(atomic_load(&gate.peak)==2u);
        check(praktor_workflow_dag_get_stats(dag,&stats)==SALTS_OK);
        check(stats.active==2u&&stats.completed==0u&&stats.ready==0u);
        atomic_store(&gate.release,true); /* always release even on timeout */
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        for(size_t i=0u;i<count;++i)settle(dag,probes,chosen[i],1u,0u);
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==1u&&chosen[1]==2u);
        post_batch(&executor,probes,chosen,count);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        for(size_t i=0u;i<count;++i)settle(dag,probes,chosen[i],1u,0u);
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==3u);
        post_batch(&executor,probes,chosen,count);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        settle(dag,probes,chosen[0],1u,0u);
        require_terminal(dag,PRAKTOR_WORKFLOW_DAG_CONTINUE,DAG_TASKS,true);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        cflow_executor_destroy(&executor);
        praktor_workflow_dag_state_destroy(dag);
        praktor_workflow_dependencies_destroy(graph);
    }

    it("stops DAG on a failed sibling after both real worker callbacks settle") {
        praktor_workflow_dependencies *graph=fixture_graph();
        praktor_workflow_dag_state *dag=NULL;
        cflow_executor executor={0};cflow_executor_control control={0};
        dag_gate gate;dag_probe probes[DAG_TASKS];
        size_t chosen[2]={0u,0u},count=0u;
        gate_init(&gate);init_probes(probes,&gate,false,true);
        check(praktor_workflow_dag_state_create(graph,2u,&dag)==SALTS_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,2u,2u));
        check(cflow_executor_as_control(&executor,&control));
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        post_batch(&executor,probes,chosen,count);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        settle(dag,probes,0u,1u,0u); /* semantic failure in first callback */
        check(atomic_load(&probes[0].result)==-1);
        settle(dag,probes,4u,1u,0u);
        require_terminal(dag,PRAKTOR_WORKFLOW_DAG_FAILED,2u,false);
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        cflow_executor_destroy(&executor);
        praktor_workflow_dag_state_destroy(dag);
        praktor_workflow_dependencies_destroy(graph);
    }

    it("cancels a queued sibling while a worker runs and never admits descendants") {
        praktor_workflow_dependencies *graph=fixture_graph();
        praktor_workflow_dag_state *dag=NULL;
        cflow_executor executor={0};cflow_executor_control control={0};
        cflow_executor_protocol_stats cstats={0};
        dag_gate gate;dag_probe probes[DAG_TASKS];
        size_t chosen[2]={0u,0u},count=0u;
        bool started;
        gate_init(&gate);init_probes(probes,&gate,false,false);
        probes[0].block=true;
        check(praktor_workflow_dag_state_create(graph,2u,&dag)==SALTS_OK);
        check(cflow_executor_worker_init_with_capacity(&executor,1u,2u));
        check(cflow_executor_as_control(&executor,&control));
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        {
            cflow_executor_task first=descriptor(&probes[0]);
            check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
        }
        started=await_started(&gate,1u);
        check(started);
        {
            cflow_executor_task second=descriptor(&probes[4]);
            check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(praktor_workflow_dag_stop(dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        atomic_store(&gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&cstats));
        check(cstats.accepted==2u&&cstats.completed==1u&&cstats.cancelled==1u);
        settle(dag,probes,0u,1u,0u);
        settle(dag,probes,4u,0u,1u);
        require_terminal(dag,PRAKTOR_WORKFLOW_DAG_CANCELLED,2u,false);
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_EBUSY);
        cflow_executor_destroy(&executor);
        praktor_workflow_dag_state_destroy(dag);
        praktor_workflow_dependencies_destroy(graph);
    }
}

/* A full ManualExecutor queue declines the second descriptor. There is NO
 * callback for refusal, so the owner immediately releases just that DAG
 * reservation, cancels accepted tasks, then waits for their finalize. */
spec("C11 DAG treats CFlow capacity refusal as a terminal admission result") {
    it("settles a refused task without inventing callbacks or releasing edges") {
        praktor_workflow_dependencies *graph=fixture_graph();
        praktor_workflow_dag_state *dag=NULL;
        cflow_executor executor={0};cflow_executor_control control={0};
        dag_gate gate;dag_probe probes[DAG_TASKS];
        size_t chosen[2]={0u,0u},count=0u;
        gate_init(&gate);init_probes(probes,&gate,false,false);
        check(praktor_workflow_dag_state_create(graph,2u,&dag)==SALTS_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,1u));
        check(cflow_executor_as_control(&executor,&control));
        check(praktor_workflow_dag_claim_ready(dag,chosen,2u,&count)==SALTS_OK);
        check(count==2u&&chosen[0]==0u&&chosen[1]==4u);
        {
            cflow_executor_task first=descriptor(&probes[0]);
            cflow_executor_task second=descriptor(&probes[4]);
            check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
            check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_FULL);
        }
        check(praktor_workflow_dag_refuse(dag,4u)==SALTS_OK);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        check(cflow_executor_run_ready(&executor)==0u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        settle(dag,probes,0u,0u,1u);
        check(atomic_load(&probes[4].ran)==0u);
        check(atomic_load(&probes[4].cancelled)==0u);
        check(atomic_load(&probes[4].finalized)==0u);
        require_terminal(dag,PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED,1u,false);
        cflow_executor_destroy(&executor);
        praktor_workflow_dag_state_destroy(dag);
        praktor_workflow_dependencies_destroy(graph);
    }
}
