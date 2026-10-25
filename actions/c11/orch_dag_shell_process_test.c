/* Qualification-only real native Shell / Process DAG lifecycle.
 * The default Orch plan still rejects Shell. Only the explicit single-shell
 * admission path and a real initialized Process retainer can start it.
 *
 * No Legacy fallback or worker-thread Session transfer. ManualExecutor
 * runs each borrowed Session on its owning thread. A completed CFlow callback
 * is NOT equivalent to terminal Shell/NativeIO cleanup. The application owner
 * alone publishes root values and TaskRegistry outputs after CFlow settles and
 * the existing Process retainer reports zero outstanding work. */
#include "orch_session_plan.h"
#include "native_host.h"
#include "workflow_parallel_process_guard.h"
#include "workflow_snapshot_values.h"
#include "yml/workflow_dag_state.h"
#include <cflow/executor.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <salts/process_retainer.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static const char *success_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: shell\n"
    "    shell: {cmd: echo native-c11-dag, stream_output: false}\n"
    "  - name: verify\n"
    "    depends_on: [shell]\n"
    "    check_exit_code: {input_key: shell_exit_code, expected: 0}\n";
#if defined(_WIN32)
static const char *cancel_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: shell\n"
    "    shell: {cmd: 'ping -n 5 127.0.0.1 > NUL', stream_output: false}\n"
    "  - name: verify\n"
    "    depends_on: [shell]\n"
    "    check_exit_code: {input_key: shell_exit_code, expected: 0}\n";
#else
static const char *cancel_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: shell\n"
    "    shell: {cmd: 'sleep 3', stream_output: false}\n"
    "  - name: verify\n"
    "    depends_on: [shell]\n"
    "    check_exit_code: {input_key: shell_exit_code, expected: 0}\n";
#endif

typedef struct shell_fixture {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
} shell_fixture;

typedef struct shell_owner_task {
    praktor_c11_native_host host;
    praktor_c11_bt_host hooks;
    praktor_c11_session session;
    praktor_c11_session_config config;
    salts_bt_node_status outcome;
    int tick_status;
    bool until_terminal;
    unsigned run_count,cancel_count,finalize_count;
} shell_owner_task;

static void fixture_init(shell_fixture *f,const char *yaml) {
    char error[256]={0};
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&f->snapshot,error,sizeof error)==SALTS_OK);
    if(!f->snapshot){info("C11 real Shell YAML: %s",error);return;}
    check(praktor_workflow_task_registry_create(
        f->snapshot,&f->registry)==SALTS_OK);
    check(praktor_workflow_dag_state_create(
        praktor_workflow_snapshot_dependencies(f->snapshot),
        1u,&f->dag)==SALTS_OK);
    check(praktor_c11_value_store_init(
        &f->root,10u,64u,512u)==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_seed_root_text_variables(
        f->snapshot,&f->root)==PRAKTOR_C11_VALUE_OK);
    check(cflow_process_retainer_init(&f->retainer,2u)==SALTS_OK);
}
static void drain_retainer(cflow_process_retainer *retainer) {
    size_t reaped=0u,pending=SIZE_MAX;
    const uint64_t started=cmeta_hrtime();
    int status=SALTS_EBUSY;
    do {
        status=cflow_process_retainer_run_ready(
            retainer,64u,&reaped,&pending);
        check(status==SALTS_OK||status==SALTS_EBUSY);
        if(status!=SALTS_OK&&status!=SALTS_EBUSY)break;
        if(pending)cmeta_sleep_ms(2u);
    } while(pending&&cmeta_hrtime()-started<UINT64_C(8000000000));
    check(pending==0u);
}
static void fixture_destroy(shell_fixture *f) {
    drain_retainer(&f->retainer);
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_dag_state_destroy(f->dag);
    praktor_workflow_task_registry_destroy(f->registry);
    praktor_workflow_snapshot_destroy(f->snapshot);
}
static bool root_is(const praktor_c11_value_store *store,
                    const char *key,const char *expected) {
    char text[96]={0};size_t n=0u;
    return praktor_c11_value_store_get(
        store,key,text,sizeof text,&n)==PRAKTOR_C11_VALUE_OK&&
        n==strlen(expected)&&strcmp(text,expected)==0;
}
static void session_init(shell_fixture *f,shell_owner_task *owner,
                         size_t task_index,bool allow_shell,bool immediate_close) {
    praktor_c11_orch_session_plan *plan=NULL;
    praktor_c11_native_host_config config={0};
    char error[256]={0};
    int status;
    memset(owner,0,sizeof(*owner));
    praktor_c11_native_host_config_init(&config,10u,64u,512u);
    config.max_running_leaves=1u;
    check(praktor_c11_native_host_init_with_config(
        &owner->host,&config)==SALTS_OK);
    check(praktor_c11_workflow_load_task_text_scope(
        f->snapshot,task_index,&f->root,
        praktor_c11_native_host_values(&owner->host))==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_native_host_bind(&owner->host,&owner->hooks)==SALTS_OK);
    praktor_c11_session_config_init(&owner->config);
    owner->config.host=owner->hooks;
    owner->config.retainer=&f->retainer;
    if(immediate_close)owner->config.shell_close_budget_ms=0u;
    status=allow_shell
        ?praktor_c11_orch_session_plan_create_native_shell(
            praktor_workflow_snapshot_task_tree(f->snapshot,task_index),
            &owner->config.tree,&plan,error,sizeof error)
        :praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(f->snapshot,task_index),
            &owner->config.tree,&plan,error,sizeof error);
    check(status==SALTS_OK);
    if(!plan) {
        info("Orch Shell plan: %s",error);
        return;
    }
    check(praktor_c11_orch_session_plan_init_session(
        plan,&owner->config,&owner->session)==SALTS_OK);
    praktor_c11_orch_session_plan_destroy(plan);
}
static void session_destroy(shell_owner_task *owner) {
    check(praktor_c11_session_destroy(&owner->session)==SALTS_OK);
    praktor_c11_native_host_destroy(&owner->host);
}
static void tick_cflow(void *user) {
    shell_owner_task *owner=(shell_owner_task *)user;
    ++owner->run_count;
    owner->outcome=SALTS_BT_RUNNING;
    owner->tick_status=SALTS_OK;
    const uint64_t started=cmeta_hrtime();
    do {
        owner->tick_status=praktor_c11_session_tick(
            &owner->session,&owner->outcome);
        if(owner->tick_status!=SALTS_OK||!owner->until_terminal||
           owner->outcome!=SALTS_BT_RUNNING)break;
        cmeta_sleep_ms(1u);
    } while(cmeta_hrtime()-started<UINT64_C(8000000000));
}
static void cancel_cflow(void *user) {
    shell_owner_task *owner=(shell_owner_task *)user;
    ++owner->cancel_count;
}
static void finalize_cflow(void *user) {
    shell_owner_task *owner=(shell_owner_task *)user;
    ++owner->finalize_count;
}
static void dispatch_one(cflow_executor *executor,
                         cflow_executor_control *control,
                         shell_owner_task *owner) {
    cflow_executor_task task={0};
    check(cflow_executor_manual_init_with_capacity(executor,1u));
    check(cflow_executor_as_control(executor,control));
    task.run=tick_cflow;task.cancel=cancel_cflow;
    task.finalize=finalize_cflow;task.user=owner;
    check(cflow_executor_try_post_task(executor,&task)==CFLOW_ADMISSION_ACCEPTED);
    check(cflow_executor_run_ready(executor)==1u);
    check(cflow_executor_control_shutdown(control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
    check(cflow_executor_control_wait_idle(control)==CFLOW_EXECUTOR_WAIT_IDLE);
    check(owner->run_count==1u&&owner->cancel_count==0u&&owner->finalize_count==1u);
    check(owner->tick_status==SALTS_OK);
}
static void assert_dag(shell_fixture *f,size_t active,size_t completed,
                       size_t ready,praktor_workflow_dag_terminal terminal,
                       bool success) {
    praktor_workflow_dag_stats stats={0};
    check(praktor_workflow_dag_get_stats(f->dag,&stats)==SALTS_OK);
    check(stats.task_count==2u);
    check(stats.active==active&&stats.completed==completed&&stats.ready==ready);
    check(stats.terminal==terminal&&stats.all_succeeded==success);
}

spec("Native Shell Process ownership through C11 DAG and CFlow ManualExecutor") {
    it("requires an explicit retainer and preserves default fail-closed Shell admission") {
        shell_fixture f;
        praktor_c11_session_config config={0};
        praktor_c11_orch_session_plan *plan=NULL;
        praktor_c11_session session={0};
        char error[256]={0};
        fixture_init(&f,success_yaml);
        if(!f.snapshot)return;
        praktor_c11_session_config_init(&config);
        check(praktor_c11_orch_session_plan_create(
            praktor_workflow_snapshot_task_tree(f.snapshot,0u),
            &config.tree,&plan,error,sizeof error)==SALTS_ENOTSUP);
        check(plan==NULL);
        check(strstr(error,"no Legacy fallback")!=NULL);
        check(praktor_c11_orch_session_plan_create_native_shell(
            praktor_workflow_snapshot_task_tree(f.snapshot,1u),
            &config.tree,&plan,error,sizeof error)==SALTS_ENOTSUP);
        check(plan==NULL); /* a CheckExitCode, not a Shell */
        check(praktor_c11_orch_session_plan_create_native_shell(
            praktor_workflow_snapshot_task_tree(f.snapshot,0u),
            &config.tree,&plan,error,sizeof error)==SALTS_OK);
        check(plan!=NULL);
        if(plan) {
            check(praktor_c11_orch_session_plan_node_count(plan)==1u);
            check(praktor_c11_orch_session_plan_init_session(
                plan,&config,&session)==SALTS_ENOTSUP);
            check(session.impl==NULL);
            praktor_c11_orch_session_plan_destroy(plan);
        }
        fixture_destroy(&f);
    }

    it("executes a real Shell, publishes only after Process drain and releases DAG successor") {
        shell_fixture f;
        shell_owner_task first,second;
        cflow_executor executor={0},successor={0};
        cflow_executor_control control={0},successor_control={0};
        praktor_c11_value_txn staged={0};
        praktor_workflow_task_output_update update={0};
        json_value_t *json=NULL;
        size_t chosen[2]={SIZE_MAX,SIZE_MAX},count=0u,pending=SIZE_MAX;
        int commit_status;
        const char *outputs[]={"shell_exit_code"};
        const uint64_t started=cmeta_hrtime();
        fixture_init(&f,success_yaml);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(
            f.dag,chosen,2u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==0u);
        session_init(&f,&first,0u,true,false);
        first.until_terminal=true;
        dispatch_one(&executor,&control,&first);
        check(first.outcome==SALTS_BT_SUCCESS);
        check(cmeta_hrtime()-started<UINT64_C(10000000000));
        check(root_is(praktor_c11_native_host_values(&first.host),
                      "shell_exit_code","0"));
        check(root_is(&f.root,"BASE","untouched"));
        assert_dag(&f,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        {
            int present=1;
            check(praktor_c11_value_store_contains(&f.root,"shell_exit_code",
                &present)==PRAKTOR_C11_VALUE_OK&&present==0);
        }
        check(praktor_c11_workflow_prepare_task_text_outputs(
            &f.root,praktor_c11_native_host_values(&first.host),
            outputs,1u,&staged)==PRAKTOR_C11_VALUE_OK);
        json=json_parse("{\"exit\":0}",10u);
        check(json!=NULL);
        update.task_index=0u;update.updates=json;
        do {
            commit_status=praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
                f.registry,&update,&staged,1u,&control,&f.retainer,
                64u,&pending);
            check(commit_status==PRAKTOR_C11_PARALLEL_OK||
                  commit_status==PRAKTOR_C11_PARALLEL_BUSY);
            if(commit_status==PRAKTOR_C11_PARALLEL_BUSY)cmeta_sleep_ms(2u);
        } while(commit_status==PRAKTOR_C11_PARALLEL_BUSY&&
                cmeta_hrtime()-started<UINT64_C(10000000000));
        check(commit_status==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u);
        check(staged.impl==NULL);
        check(praktor_workflow_task_registry_complete(f.registry,0u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,true)==SALTS_OK);
        assert_dag(&f,0u,1u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(root_is(&f.root,"shell_exit_code","0"));
        json_free(json);
        cflow_executor_destroy(&executor);
        session_destroy(&first);

        check(praktor_workflow_dag_claim_ready(
            f.dag,chosen,2u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==1u);
        session_init(&f,&second,1u,false,false);
        check(root_is(praktor_c11_native_host_values(&second.host),
                      "shell_exit_code","0"));
        dispatch_one(&successor,&successor_control,&second);
        check(second.outcome==SALTS_BT_SUCCESS);
        check(praktor_workflow_task_registry_complete(f.registry,1u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,1u,true)==SALTS_OK);
        assert_dag(&f,0u,2u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true);
        cflow_executor_destroy(&successor);
        session_destroy(&second);
        fixture_destroy(&f);
    }

    it("cancels an actual running Shell and drains the adopted Process without downstream admission") {
        shell_fixture f;
        shell_owner_task first;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        size_t chosen[2]={SIZE_MAX,SIZE_MAX},count=0u;
        int status;
        fixture_init(&f,cancel_yaml);
        if(!f.snapshot)return;
        check(praktor_workflow_dag_claim_ready(
            f.dag,chosen,2u,&count)==SALTS_OK);
        check(count==1u&&chosen[0]==0u);
        /* Zero-budget close is selected BEFORE Session copies its config;
         * outstanding NativeIO leases transfer to the sole recovery owner. */
        session_init(&f,&first,0u,true,true);
        dispatch_one(&executor,&control,&first);
        check(first.outcome==SALTS_BT_RUNNING);
        assert_dag(&f,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        status=praktor_c11_session_cancel(&first.session);
        check(status==SALTS_OK||status==SALTS_EBUSY);
        check(praktor_workflow_dag_stop(
            f.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(praktor_workflow_task_registry_fail(
            f.registry,0u,vstr_from_cstr("cancelled"))==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,false)==SALTS_OK);
        assert_dag(&f,0u,1u,0u,PRAKTOR_WORKFLOW_DAG_CANCELLED,false);
        count=88u;
        check(praktor_workflow_dag_claim_ready(
            f.dag,chosen,2u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(root_is(&f.root,"BASE","untouched"));
        {
            int present=1;
            check(praktor_c11_value_store_contains(&f.root,"shell_exit_code",
                &present)==PRAKTOR_C11_VALUE_OK&&present==0);
        }
        cflow_executor_destroy(&executor);
        session_destroy(&first); /* canonical Process owner adopts live leases */
        fixture_destroy(&f); /* drives the real retainer until pending==0 */
    }
}
