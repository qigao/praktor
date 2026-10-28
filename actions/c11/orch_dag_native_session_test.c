/* Real YAML -> canonical C11 Orch AST -> owned Session Plan -> real native
 * NativeHost/Session leaves -> CFlow ManualExecutor -> owner DAG settlement.
 *
 * This proof is deliberately single-driver: the ManualExecutor runs on the
 * owner thread, as required by Session and NativeHost lifetime contracts.
 * WorkerExecutor migration, dynamic triggers, file-based plans and real Shell
 * Process leases remain separate qualification gates. */
#include "orch_session_plan.h"
#include "workflow_parallel_process_guard.h"
#include "workflow_snapshot_values.h"
#include "native_host.h"
#include "yml/workflow_dag_state.h"
#include <cflow/executor.h>
#include <salts/process_retainer.h>
#include <json_parser.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static const char *success_yaml=
    "name: C11 native DAG leaves\n"
    "variables: {seed: '9'}\n"
    "tasks:\n"
    "  - name: prepare\n"
    "    vars: {seed: '7'}\n"
    "    set_variable: answer\n"
    "    from: seed\n"
    "  - name: verify\n"
    "    depends_on: [prepare]\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - CheckExitCode: {input_key: answer, expected: 7}\n"
    "        - Sleep: {duration: 0s}\n";
static const char *cancel_yaml=
    "variables: {late: original}\n"
    "tasks:\n"
    "  - name: wait\n"
    "    vars: {late: task-local}\n"
    "    actions:\n"
    "      Sequence:\n"
    "        - Sleep: {duration: 10s}\n"
    "        - set_variable: late\n"
    "          value: done\n"
    "  - name: after\n"
    "    depends_on: [wait]\n"
    "    set_variable: after\n"
    "    value: visible\n";

typedef struct native_dag_fixture {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
} native_dag_fixture;
typedef struct native_task {
    praktor_c11_native_host host;
    praktor_c11_bt_host hooks;
    praktor_c11_session_config config;
    praktor_c11_session session;
    uint64_t now_ns;
    int tick_status;
    salts_bt_node_status outcome;
    unsigned ran, cancelled, finalized;
} native_task;

static void fixture_init(native_dag_fixture *f,const char *source) {
    char error[256]={0};
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(source),
        &f->snapshot,error,sizeof error)==SALTS_OK);
    if(!f->snapshot) { info("C11 DAG snapshot: %s",error);return; }
    check(praktor_workflow_task_registry_create(
        f->snapshot,&f->registry)==SALTS_OK);
    check(praktor_workflow_dag_state_create(
        praktor_workflow_snapshot_dependencies(f->snapshot),1u,&f->dag)==SALTS_OK);
    check(praktor_c11_value_store_init(&f->root,8u,64u,128u)==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_seed_root_text_variables(
        f->snapshot,&f->root)==PRAKTOR_C11_VALUE_OK);
    check(cflow_process_retainer_init(&f->retainer,2u)==SALTS_OK);
}
static void fixture_destroy(native_dag_fixture *f) {
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_dag_state_destroy(f->dag);
    praktor_workflow_task_registry_destroy(f->registry);
    praktor_workflow_snapshot_destroy(f->snapshot);
}
static uint64_t clock_ns(void *user) {
    return *(const uint64_t *)user;
}
static void task_init(native_dag_fixture *f,native_task *task,size_t index) {
    praktor_c11_native_host_config host_config={0};
    praktor_c11_orch_session_plan *plan=NULL;
    char error[256]={0};
    memset(task,0,sizeof(*task));
    praktor_c11_native_host_config_init(&host_config,8u,64u,128u);
    host_config.max_running_leaves=1u;
    host_config.now_ns=clock_ns;host_config.clock_user=&task->now_ns;
    check(praktor_c11_native_host_init_with_config(
        &task->host,&host_config)==SALTS_OK);
    check(praktor_c11_native_host_bind(&task->host,&task->hooks)==SALTS_OK);
    check(praktor_c11_workflow_load_task_text_scope(
        f->snapshot,index,&f->root,
        praktor_c11_native_host_values(&task->host))==
        PRAKTOR_C11_VALUE_OK);
    praktor_c11_session_config_init(&task->config);
    task->config.host=task->hooks;
    check(praktor_c11_orch_session_plan_create(
        praktor_workflow_snapshot_task_tree(f->snapshot,index),
        &task->config.tree,&plan,error,sizeof error)==SALTS_OK);
    if(!plan) {info("C11 Orch Session Plan: %s",error);return;}
    check(praktor_c11_orch_session_plan_init_session(
        plan,&task->config,&task->session)==SALTS_OK);
    praktor_c11_orch_session_plan_destroy(plan);
}
static void task_destroy(native_task *task) {
    check(praktor_c11_session_destroy(&task->session)==SALTS_OK);
    praktor_c11_native_host_destroy(&task->host);
}
static void task_run(void *user) {
    native_task *task=(native_task *)user;
    ++task->ran;
    task->tick_status=praktor_c11_session_tick(&task->session,&task->outcome);
}
static void task_cancel(void *user) {
    native_task *task=(native_task *)user;
    ++task->cancelled;
}
static void task_finalize(void *user) {
    native_task *task=(native_task *)user;
    ++task->finalized;
}
static void task_post(cflow_executor *executor,native_task *task) {
    cflow_executor_task descriptor={0};
    descriptor.run=task_run;
    descriptor.cancel=task_cancel;
    descriptor.finalize=task_finalize;
    descriptor.user=task;
    check(cflow_executor_try_post_task(executor,&descriptor)==CFLOW_ADMISSION_ACCEPTED);
}
static void owner_execute(cflow_executor *executor,
                          cflow_executor_control *control,native_task *task) {
    check(cflow_executor_manual_init_with_capacity(executor,1u));
    check(cflow_executor_as_control(executor,control));
    task_post(executor,task);
    check(cflow_executor_run_ready(executor)==1u);
    check(cflow_executor_control_shutdown(
        control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
    check(cflow_executor_control_wait_idle(control)==CFLOW_EXECUTOR_WAIT_IDLE);
    check(task->ran==1u&&task->cancelled==0u&&task->finalized==1u);
    check(task->tick_status==SALTS_OK);
}
static bool get_text(const praktor_c11_value_store *store,
                     const char *key,char *buffer,size_t capacity) {
    size_t size=0u;
    return praktor_c11_value_store_get(store,key,buffer,capacity,&size)==
        PRAKTOR_C11_VALUE_OK;
}
static bool text_is(const praktor_c11_value_store *store,
                    const char *key,const char *expected) {
    char buffer[128]={0};
    return get_text(store,key,buffer,sizeof buffer)&&
        strcmp(buffer,expected)==0;
}
static void check_dag(const native_dag_fixture *f,size_t active,size_t completed,
                      size_t ready,praktor_workflow_dag_terminal terminal,
                      bool success) {
    praktor_workflow_dag_stats s={0};
    check(praktor_workflow_dag_get_stats(f->dag,&s)==SALTS_OK);
    check(s.active==active&&s.completed==completed&&s.ready==ready);
    check(s.terminal==terminal&&s.all_succeeded==success);
}

spec("Real native C11 Session leaves driven by DAG owner and CFlow") {
    it("uses an actual SetVariable leaf before admitting dependent CheckExitCode") {
        native_dag_fixture f;
        native_task first,second;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        praktor_c11_value_txn txn={0};
        praktor_workflow_task_output_update update={0};
        json_value_t *output=NULL;
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u,pending=SIZE_MAX;
        char answer[32]={0};
        fixture_init(&f,success_yaml);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==0u);
        task_init(&f,&first,0u);
        owner_execute(&executor,&control,&first);
        check(first.outcome==SALTS_BT_SUCCESS);
        check(get_text(praktor_c11_native_host_values(&first.host),
                       "answer",answer,sizeof answer));
        check(strcmp(answer,"7")==0);
        check(!text_is(&f.root,"answer","7"));
        check_dag(&f,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        /* No mutation of canonical root/TaskRegistry until after finalize,
         * closed CFlow admission and Process-retainer quiescence. */
        {
            const char *outputs[]={"answer"};
            check(praktor_c11_workflow_prepare_task_text_outputs(
                &f.root,praktor_c11_native_host_values(&first.host),
                outputs,1u,&txn)==PRAKTOR_C11_VALUE_OK);
        }
        output=json_parse("{\"answer\":\"7\"}",14u);
        check(output!=NULL);
        update.task_index=0u;
        update.updates=output;
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,&update,&txn,1u,&control,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(praktor_workflow_task_registry_complete(f.registry,0u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,true)==SALTS_OK);
        check_dag(&f,0u,1u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(text_is(&f.root,"answer","7"));
        check(text_is(&f.root,"seed","9")); /* task seed=7 never merged */
        json_free(output);
        cflow_executor_destroy(&executor);
        task_destroy(&first);

        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==1u);
        task_init(&f,&second,1u);
        /* The dependent Host is atomically forked from the updated root:
         * no hand-written answer injection and no wholesale local merge. */
        check(text_is(praktor_c11_native_host_values(&second.host),
                      "answer","7"));
        check(text_is(praktor_c11_native_host_values(&second.host),
                      "seed","9"));
        executor=(cflow_executor){0};control=(cflow_executor_control){0};
        owner_execute(&executor,&control,&second);
        check(second.outcome==SALTS_BT_SUCCESS);
        check(praktor_workflow_task_registry_complete(f.registry,1u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,1u,true)==SALTS_OK);
        check_dag(&f,0u,2u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true);
        cflow_executor_destroy(&executor);
        task_destroy(&second);
        fixture_destroy(&f);
    }

    it("retains a running Session despite a completed CFlow callback and cancels before successors") {
        native_dag_fixture f;
        native_task waiting;
        cflow_executor executor={0};cflow_executor_control control={0};
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u;
        fixture_init(&f,cancel_yaml);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==0u);
        task_init(&f,&waiting,0u);
        owner_execute(&executor,&control,&waiting);
        check(waiting.outcome==SALTS_BT_RUNNING);
        check_dag(&f,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(text_is(&f.root,"late","original"));
        check(text_is(praktor_c11_native_host_values(&waiting.host),
                      "late","task-local"));
        /* CFlow finalized the tick, NOT the still-running Session. */
        check(praktor_c11_session_cancel(&waiting.session)==SALTS_OK);
        check(praktor_workflow_dag_stop(
            f.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(praktor_workflow_task_registry_fail(
            f.registry,0u,vstr_from_cstr("cancelled"))==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,false)==SALTS_OK);
        check_dag(&f,0u,1u,0u,PRAKTOR_WORKFLOW_DAG_CANCELLED,false);
        check(praktor_workflow_dag_claim_ready(
            f.dag,ids,2u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(text_is(&f.root,"late","original"));
        check(!text_is(&f.root,"after","visible"));
        cflow_executor_destroy(&executor);
        task_destroy(&waiting);
        fixture_destroy(&f);
    }
}
