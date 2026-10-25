/* Source-only CFlow WorkerExecutor -> worker-local C11 Session qualification.
 * The Owner builds immutable plans and isolated task-scope snapshots BEFORE
 * posting. Each accepted run callback constructs, ticks/cancels and destroys
 * its own Session/NativeHost on ONE worker. No Session or live Host is handed
 * to the Owner or moved between callbacks. The Owner reads bounded result
 * copies only after CFlow finalizers and closed admission, then performs the
 * existing typed/text + DAG publication on its exclusive turn.
 *
 * This does NOT qualify Shell/Process owners on arbitrary worker threads,
 * production WorkerExecutor cutover, dynamic tasks, or persistent recovery. */
#include "orch_session_plan.h"
#include "native_host.h"
#include "workflow_parallel_process_guard.h"
#include "workflow_snapshot_values.h"
#include "yml/workflow_dag_state.h"
#include <cflow/executor.h>
#include <salts/process_retainer.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static const char *success_yaml=
    "variables: {seed: global}\n"
    "tasks:\n"
    "  - name: left\n"
    "    vars: {seed: left}\n"
    "    set_variable: left_value\n"
    "    from: seed\n"
    "  - name: right\n"
    "    vars: {seed: right}\n"
    "    set_variable: right_value\n"
    "    from: seed\n"
    "  - name: verify\n"
    "    depends_on: [left, right]\n"
    "    set_variable: confirmed\n"
    "    from: left_value\n";

static const char *cancel_yaml=
    "variables: {late: original}\n"
    "tasks:\n"
    "  - name: wait\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - Sleep: {duration: 10s}\n"
    "        - set_variable: late\n"
    "          value: published\n"
    "  - name: queued\n"
    "    set_variable: queued_result\n"
    "    value: must_not_run\n"
    "  - name: after\n"
    "    depends_on: [wait, queued]\n"
    "    set_variable: after\n"
    "    value: must_not_run\n";

typedef struct worker_fixture {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
} worker_fixture;

typedef struct session_gate {
    atomic_uint started, in_flight, peak;
    atomic_bool release, cancel_requested;
} session_gate;

typedef struct worker_session {
    /* Immutable or exclusively borrowed while a CFlow callback is live. */
    praktor_c11_orch_session_plan *plan;
    praktor_c11_value_store input;
    session_gate *gate;
    const char *result_key;
    bool cancel_running_sleep;

    /* Only the worker writes these; Owner reads after wait_idle/finalize. */
    int setup_status, tick_status, cancel_status, destroy_status, output_status;
    salts_bt_node_status outcome;
    char output[129];
    atomic_uint ran, cancelled, finalized;
} worker_session;

static void gate_init(session_gate *gate) {
    atomic_init(&gate->started,0u);
    atomic_init(&gate->in_flight,0u);
    atomic_init(&gate->peak,0u);
    atomic_init(&gate->release,false);
    atomic_init(&gate->cancel_requested,false);
}
static bool await_started(session_gate *gate,unsigned count) {
    unsigned waited=0u;
    while(atomic_load(&gate->started)<count && waited++<6000u)
        cmeta_sleep_ms(1u);
    return atomic_load(&gate->started)>=count;
}
static void gate_enter(session_gate *gate) {
    const unsigned live=atomic_fetch_add(&gate->in_flight,1u)+1u;
    unsigned peak=atomic_load(&gate->peak);
    while(peak<live &&
          !atomic_compare_exchange_weak(&gate->peak,&peak,live)) {}
    atomic_fetch_add(&gate->started,1u);
}
static void fixture_init(worker_fixture *f,const char *yaml,size_t max_active) {
    char error[256]={0};
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&f->snapshot,error,sizeof error)==SALTS_OK);
    if(!f->snapshot){info("Worker Session YAML: %s",error);return;}
    check(praktor_workflow_task_registry_create(f->snapshot,&f->registry)==SALTS_OK);
    check(praktor_workflow_dag_state_create(
        praktor_workflow_snapshot_dependencies(f->snapshot),
        max_active,&f->dag)==SALTS_OK);
    check(praktor_c11_value_store_init(&f->root,12u,64u,128u)==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_seed_root_text_variables(
        f->snapshot,&f->root)==PRAKTOR_C11_VALUE_OK);
    check(cflow_process_retainer_init(&f->retainer,1u)==SALTS_OK);
}
static void fixture_destroy(worker_fixture *f) {
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_dag_state_destroy(f->dag);
    praktor_workflow_task_registry_destroy(f->registry);
    praktor_workflow_snapshot_destroy(f->snapshot);
}
static bool value_is(const praktor_c11_value_store *values,
                     const char *key,const char *expected) {
    char buffer[129]={0};size_t size=0u;
    return praktor_c11_value_store_get(values,key,buffer,sizeof buffer,&size)==
           PRAKTOR_C11_VALUE_OK &&
           size==strlen(expected)&&strcmp(buffer,expected)==0;
}
static uint64_t frozen_now(void *user) {
    return *(const uint64_t *)user;
}
static void prepare_worker(worker_session *task,worker_fixture *f,
                           size_t task_index,session_gate *gate,
                           const char *result_key,bool cancel_sleep) {
    praktor_c11_session_config config={0};
    char error[256]={0};
    memset(task,0,sizeof(*task));
    task->gate=gate;task->result_key=result_key;
    task->cancel_running_sleep=cancel_sleep;
    task->setup_status=SALTS_EINVAL;
    task->tick_status=SALTS_EINVAL;
    task->cancel_status=SALTS_EINVAL;
    task->destroy_status=SALTS_EINVAL;
    task->output_status=PRAKTOR_C11_VALUE_ENOTFOUND;
    task->outcome=SALTS_BT_FAILURE;
    atomic_init(&task->ran,0u);
    atomic_init(&task->cancelled,0u);
    atomic_init(&task->finalized,0u);
    praktor_c11_session_config_init(&config);
    check(praktor_c11_orch_session_plan_create(
        praktor_workflow_snapshot_task_tree(f->snapshot,task_index),
        &config.tree,&task->plan,error,sizeof error)==SALTS_OK);
    if(!task->plan)info("Worker Plan: %s",error);
    check(task->plan!=NULL);
    check(praktor_c11_value_store_init(&task->input,12u,64u,128u)==
          PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_load_task_text_scope(
        f->snapshot,task_index,&f->root,&task->input)==PRAKTOR_C11_VALUE_OK);
}
static void release_worker(worker_session *task) {
    /* Only the Owner frees immutable plan/input AFTER CFlow wait_idle. */
    praktor_c11_orch_session_plan_destroy(task->plan);
    praktor_c11_value_store_destroy(&task->input);
}
static void worker_run(void *user) {
    worker_session *task=(worker_session *)user;
    praktor_c11_native_host host={0};
    praktor_c11_native_host_config host_config={0};
    praktor_c11_bt_host hooks={0};
    praktor_c11_session_config config={0};
    praktor_c11_session session={0};
    uint64_t now_ns=0u;
    size_t output_size=0u;
    bool entered=false;
    int r;
    atomic_fetch_add(&task->ran,1u);

    /* No mutable Session/NativeHost object exists on the application Owner. */
    praktor_c11_native_host_config_init(&host_config,12u,64u,128u);
    host_config.max_running_leaves=1u;
    host_config.now_ns=frozen_now;
    host_config.clock_user=&now_ns;
    r=praktor_c11_native_host_init_with_config(&host,&host_config);
    if(r!=SALTS_OK){task->setup_status=r;goto done;}
    r=praktor_c11_native_host_bind(&host,&hooks);
    if(r!=SALTS_OK){task->setup_status=r;goto done;}
    r=praktor_c11_value_store_replace_clone(
        praktor_c11_native_host_values(&host),&task->input);
    if(r!=PRAKTOR_C11_VALUE_OK){task->setup_status=r;goto done;}
    praktor_c11_session_config_init(&config);
    config.host=hooks;
    task->setup_status=praktor_c11_orch_session_plan_init_session(
        task->plan,&config,&session);
    if(task->setup_status!=SALTS_OK)goto done;
    if(task->cancel_running_sleep) {
        task->tick_status=praktor_c11_session_tick(&session,&task->outcome);
        if(task->tick_status!=SALTS_OK||task->outcome!=SALTS_BT_RUNNING)
            goto done;
    }
    gate_enter(task->gate);
    entered=true;
    while(!atomic_load(&task->gate->release))cmeta_sleep_ms(1u);

    if(task->cancel_running_sleep) {
        if(atomic_load(&task->gate->cancel_requested))
            task->cancel_status=praktor_c11_session_cancel(&session);
    } else {
        task->tick_status=praktor_c11_session_tick(&session,&task->outcome);
        if(task->tick_status==SALTS_OK &&
           task->outcome==SALTS_BT_SUCCESS && task->result_key) {
            task->output_status=praktor_c11_value_store_get(
                praktor_c11_native_host_values(&host),task->result_key,
                task->output,sizeof task->output,&output_size);
        }
    }

done:
    /* Cancel a still-active Session before destroying it, exclusively on
     * the SAME worker. No concurrent Owner-initiated Session cancel exists. */
    if(session.impl) {
        if(task->outcome==SALTS_BT_RUNNING &&
           task->cancel_status==SALTS_EINVAL)
            task->cancel_status=praktor_c11_session_cancel(&session);
        task->destroy_status=praktor_c11_session_destroy(&session);
    }
    praktor_c11_native_host_destroy(&host);
    if(entered)atomic_fetch_sub(&task->gate->in_flight,1u);
}
static void worker_cancel(void *user) {
    worker_session *task=(worker_session *)user;
    /* Queued cancellation must not touch a Session: none was created. */
    atomic_fetch_add(&task->cancelled,1u);
}
static void worker_finalize(void *user) {
    worker_session *task=(worker_session *)user;
    atomic_fetch_add(&task->finalized,1u);
}
static cflow_executor_task descriptor(worker_session *task) {
    cflow_executor_task posted={0};
    posted.run=worker_run;
    posted.cancel=worker_cancel;
    posted.finalize=worker_finalize;
    posted.user=task;
    return posted;
}
static void assert_worker_success(const worker_session *task,const char *wanted) {
    check(atomic_load(&task->ran)==1u);
    check(atomic_load(&task->cancelled)==0u);
    check(atomic_load(&task->finalized)==1u);
    check(task->setup_status==SALTS_OK);
    check(task->tick_status==SALTS_OK);
    check(task->outcome==SALTS_BT_SUCCESS);
    check(task->destroy_status==SALTS_OK);
    check(task->output_status==PRAKTOR_C11_VALUE_OK);
    check(strcmp(task->output,wanted)==0);
}
static void assert_dag(worker_fixture *f,size_t active,size_t completed,
                       size_t ready,praktor_workflow_dag_terminal terminal,
                       bool succeeded) {
    praktor_workflow_dag_stats stats={0};
    check(praktor_workflow_dag_get_stats(f->dag,&stats)==SALTS_OK);
    check(stats.active==active&&stats.completed==completed&&stats.ready==ready);
    check(stats.terminal==terminal&&stats.all_succeeded==succeeded);
}
static json_value_t *parse_json(const char *text) {
    json_value_t *json=json_parse(text,strlen(text));
    check(json!=NULL);
    return json;
}
spec("C11 Session lifetime remains on one real WorkerExecutor callback") {
    it("joins two worker-local Sessions before atomic owner publication and dependent admission") {
        worker_fixture f;
        worker_session branches[2],successor;
        session_gate first_gate,second_gate;
        cflow_executor executor={0},next={0};
        cflow_executor_control control={0},next_control={0};
        cflow_executor_protocol_stats stats={0};
        praktor_c11_value_txn txns[2]={{0},{0}},last={0};
        const char *typed_left="{\"result\":\"left\"}";
        const char *typed_right="{\"result\":\"right\"}";
        json_value_t *left=parse_json(typed_left),*right=parse_json(typed_right);
        json_value_t *confirmed=parse_json("{\"result\":\"left\"}");
        praktor_workflow_task_output_update outputs[2]={{0u,left},{1u,right}};
        praktor_workflow_task_output_update final_output={2u,confirmed};
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u,pending=SIZE_MAX;
        gate_init(&first_gate);gate_init(&second_gate);
        fixture_init(&f,success_yaml,2u);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==2u&&ids[0]==0u&&ids[1]==1u);
        check(cflow_executor_worker_init_with_capacity(&executor,2u,2u));
        check(cflow_executor_as_control(&executor,&control));
        for(size_t i=0u;i<2u;++i) {
            cflow_executor_task posted;
            prepare_worker(&branches[i],&f,i,&first_gate,
                           i?"right_value":"left_value",false);
            posted=descriptor(&branches[i]);
            check(cflow_executor_try_post_task(&executor,&posted)==CFLOW_ADMISSION_ACCEPTED);
        }
        const bool both_started=await_started(&first_gate,2u);
        check(both_started);
        check(atomic_load(&first_gate.peak)>=2u);
        check(value_is(&f.root,"seed","global"));
        check(praktor_c11_value_store_size(&f.root)==1u);
        assert_dag(&f,2u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        atomic_store(&first_gate.release,true); /* always release both workers */
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(atomic_load(&first_gate.in_flight)==0u);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.accepted==2u&&stats.completed==2u&&stats.cancelled==0u);
        assert_worker_success(&branches[0],"left");
        assert_worker_success(&branches[1],"right");
        check(praktor_c11_value_store_size(&f.root)==1u);
        for(size_t i=0u;i<2u;++i) {
            check(praktor_c11_value_txn_begin(&txns[i],&f.root)==
                  PRAKTOR_C11_VALUE_OK);
            check(praktor_c11_value_txn_set(
                &txns[i],i?"right_value":"left_value",branches[i].output)==
                PRAKTOR_C11_VALUE_OK);
        }
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,outputs,txns,2u,&control,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&!txns[0].impl&&!txns[1].impl);
        check(value_is(&f.root,"left_value","left"));
        check(value_is(&f.root,"right_value","right"));
        check(value_is(&f.root,"seed","global"));
        for(size_t i=0u;i<2u;++i) {
            check(praktor_workflow_task_registry_complete(f.registry,i)==SALTS_OK);
            check(praktor_workflow_dag_finish(f.dag,i,true)==SALTS_OK);
        }
        assert_dag(&f,0u,2u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        cflow_executor_destroy(&executor);
        for(size_t i=0u;i<2u;++i)release_worker(&branches[i]);

        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==2u);
        prepare_worker(&successor,&f,2u,&second_gate,"confirmed",false);
        check(cflow_executor_worker_init_with_capacity(&next,1u,1u));
        check(cflow_executor_as_control(&next,&next_control));
        {
            cflow_executor_task posted=descriptor(&successor);
            check(cflow_executor_try_post_task(&next,&posted)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(await_started(&second_gate,1u));
        check(cflow_executor_control_shutdown(
            &next_control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        atomic_store(&second_gate.release,true);
        check(cflow_executor_control_wait_idle(&next_control)==CFLOW_EXECUTOR_WAIT_IDLE);
        assert_worker_success(&successor,"left");
        check(praktor_c11_value_txn_begin(&last,&f.root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&last,"confirmed",successor.output)==
              PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,&final_output,&last,1u,&next_control,&f.retainer,
            8u,&pending)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&!last.impl);
        check(praktor_workflow_task_registry_complete(f.registry,2u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,2u,true)==SALTS_OK);
        assert_dag(&f,0u,3u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true);
        check(value_is(&f.root,"confirmed","left"));
        cflow_executor_destroy(&next);
        release_worker(&successor);
        json_free(left);json_free(right);json_free(confirmed);
        fixture_destroy(&f);
    }

    it("cancels a running Sleep on its worker and never constructs a queued Session") {
        worker_fixture f;
        worker_session running,queued;
        session_gate gate;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        cflow_executor_protocol_stats stats={0};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u;
        gate_init(&gate);
        fixture_init(&f,cancel_yaml,2u);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==2u&&ids[0]==0u&&ids[1]==1u);
        prepare_worker(&running,&f,0u,&gate,NULL,true);
        prepare_worker(&queued,&f,1u,&gate,"queued_result",false);
        check(cflow_executor_worker_init_with_capacity(&executor,1u,2u));
        check(cflow_executor_as_control(&executor,&control));
        {
            cflow_executor_task first=descriptor(&running);
            check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
        }
        const bool started=await_started(&gate,1u);
        check(started); /* Session already ticked to RUNNING on this worker. */
        {
            cflow_executor_task second=descriptor(&queued);
            check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(value_is(&f.root,"late","original"));
        assert_dag(&f,2u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        atomic_store(&gate.cancel_requested,true);
        atomic_store(&gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.accepted==2u&&stats.completed==1u&&stats.cancelled==1u);
        check(atomic_load(&running.ran)==1u&&atomic_load(&running.cancelled)==0u);
        check(atomic_load(&running.finalized)==1u);
        check(running.setup_status==SALTS_OK);
        check(running.tick_status==SALTS_OK&&running.outcome==SALTS_BT_RUNNING);
        check(running.cancel_status==SALTS_OK&&running.destroy_status==SALTS_OK);
        check(atomic_load(&queued.ran)==0u&&atomic_load(&queued.cancelled)==1u);
        check(atomic_load(&queued.finalized)==1u);
        check(atomic_load(&gate.in_flight)==0u);
        check(praktor_workflow_dag_stop(
            f.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(praktor_workflow_task_registry_fail(
            f.registry,0u,vstr_from_cstr("worker cancelled"))==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,false)==SALTS_OK);
        check(praktor_workflow_dag_refuse(f.dag,1u)==SALTS_OK);
        assert_dag(&f,0u,1u,0u,PRAKTOR_WORKFLOW_DAG_CANCELLED,false);
        count=SIZE_MAX;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(value_is(&f.root,"late","original"));
        check(praktor_c11_value_store_size(&f.root)==1u);
        check(praktor_workflow_task_registry_state(f.registry,1u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        cflow_executor_destroy(&executor);
        release_worker(&running);
        release_worker(&queued);
        fixture_destroy(&f);
    }
}
