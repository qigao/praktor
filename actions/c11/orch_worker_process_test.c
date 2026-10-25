/* Source-only qualification: a real Shell/NativeIO Session is created,
 * driven, cancelled/closed and destroyed within ONE CFlow worker callback.
 *
 * The single retainer is borrowed exclusively by the worker until CFlow
 * FINALIZE + CLOSED + wait_idle. Only then may the application Owner drive
 * its adopted Process to actual quiescence or publish results.
 *
 * No production WorkerExecutor switch, no Session migration, no second
 * Process engine, no Legacy fallback, and NO concurrent retainer operations.
 */
#include "orch_session_plan.h"
#include "native_host.h"
#include "workflow_parallel_process_guard.h"
#include "workflow_snapshot_values.h"
#include "yml/workflow_dag_state.h"
#include <cflow/executor.h>
#include <salts/clock.h>
#include <salts/process_retainer.h>
#include <salts/thread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static const char *success_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: parsed_command\n"
    "    command: echo ready\n"
    "    parse_lines: {output_key: parsed}\n"
    "  - name: verify\n"
    "    depends_on: [parsed_command]\n"
    "    set_variable: confirmed\n"
    "    from: parsed_count\n";
#if defined(_WIN32)
static const char *cancel_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: shell\n"
    "    shell: {cmd: 'ping -n 5 127.0.0.1 > NUL', stream_output: false}\n"
    "  - name: queued\n"
    "    set_variable: queued_result\n"
    "    value: forbidden\n"
    "  - name: verify\n"
    "    depends_on: [shell, queued]\n"
    "    set_variable: confirmed\n"
    "    value: forbidden\n";
#else
static const char *cancel_yaml=
    "variables: {BASE: untouched}\n"
    "tasks:\n"
    "  - name: shell\n"
    "    shell: {cmd: 'sleep 3', stream_output: false}\n"
    "  - name: queued\n"
    "    set_variable: queued_result\n"
    "    value: forbidden\n"
    "  - name: verify\n"
    "    depends_on: [shell, queued]\n"
    "    set_variable: confirmed\n"
    "    value: forbidden\n";
#endif

typedef struct process_fixture {
    praktor_workflow_snapshot *snapshot;
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    praktor_c11_value_store root;
    cflow_process_retainer retainer;
} process_fixture;

typedef struct process_gate {
    atomic_uint started;
    atomic_bool release;
    atomic_bool cancel;
} process_gate;

typedef struct process_worker {
    /* Owned by the application Owner, borrowed read-only until CFlow idle. */
    praktor_c11_orch_session_plan *plan;
    praktor_c11_value_store input;
    cflow_process_retainer *retainer;
    process_gate *gate;
    bool cancel_after_first_tick;
    const char *selected_key;

    /* One worker callback writes; Owner reads ONLY after wait_idle/finalize. */
    int init_status, tick_status, cancel_status, destroy_status, selected_status;
    salts_bt_node_status outcome;
    char selected[96];
    atomic_uint ran, cancelled, finalized;
} process_worker;

static void fixture_init(process_fixture *f,const char *yaml,size_t max_active) {
    char reason[256]={0};
    memset(f,0,sizeof(*f));
    check(praktor_workflow_snapshot_create_inline(
        vstr_from_cstr(yaml),&f->snapshot,reason,sizeof reason)==SALTS_OK);
    if(!f->snapshot){info("Worker Process YAML: %s",reason);return;}
    check(praktor_workflow_task_registry_create(f->snapshot,&f->registry)==SALTS_OK);
    check(praktor_workflow_dag_state_create(
        praktor_workflow_snapshot_dependencies(f->snapshot),
        max_active,&f->dag)==SALTS_OK);
    check(praktor_c11_value_store_init(&f->root,12u,64u,512u)==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_seed_root_text_variables(
        f->snapshot,&f->root)==PRAKTOR_C11_VALUE_OK);
    /* Capacity two: one real Shell reservation and one explicit test hold. */
    check(cflow_process_retainer_init(&f->retainer,2u)==SALTS_OK);
}
static void fixture_destroy(process_fixture *f) {
    check(cflow_process_retainer_destroy(&f->retainer)==SALTS_OK);
    praktor_c11_value_store_destroy(&f->root);
    praktor_workflow_dag_state_destroy(f->dag);
    praktor_workflow_task_registry_destroy(f->registry);
    praktor_workflow_snapshot_destroy(f->snapshot);
}
static bool root_is(const praktor_c11_value_store *store,
                    const char *key,const char *expected) {
    char text[128]={0};size_t size=0u;
    return praktor_c11_value_store_get(store,key,text,sizeof text,&size)==
        PRAKTOR_C11_VALUE_OK&&size==strlen(expected)&&
        strcmp(text,expected)==0;
}
static bool root_lacks(const praktor_c11_value_store *store,const char *key) {
    int present=1;
    return praktor_c11_value_store_contains(store,key,&present)==
           PRAKTOR_C11_VALUE_OK&&!present;
}
static void check_dag(process_fixture *f,size_t active,size_t completed,
                      size_t ready,praktor_workflow_dag_terminal terminal,
                      bool succeeded) {
    praktor_workflow_dag_stats stats={0};
    check(praktor_workflow_dag_get_stats(f->dag,&stats)==SALTS_OK);
    check(stats.active==active&&stats.completed==completed&&stats.ready==ready);
    check(stats.terminal==terminal&&stats.all_succeeded==succeeded);
}
static void gate_init(process_gate *gate) {
    atomic_init(&gate->started,0u);
    atomic_init(&gate->release,false);
    atomic_init(&gate->cancel,false);
}
static bool await_worker(process_gate *gate) {
    unsigned waited=0u;
    while(!atomic_load(&gate->started)&&waited++<6000u)cmeta_sleep_ms(1u);
    return atomic_load(&gate->started)!=0u;
}
static void prepare_worker(process_worker *w,process_fixture *f,
                           size_t index,int plan_mode,process_gate *gate,
                           const char *selected,bool cancel_first) {
    praktor_c11_session_config config={0};
    char reason[256]={0};
    int admission=SALTS_EINVAL;
    memset(w,0,sizeof(*w));
    w->retainer=&f->retainer;w->gate=gate;
    w->selected_key=selected;w->cancel_after_first_tick=cancel_first;
    w->init_status=w->tick_status=w->cancel_status=w->destroy_status=SALTS_EINVAL;
    w->selected_status=PRAKTOR_C11_VALUE_ENOTFOUND;
    w->outcome=SALTS_BT_FAILURE;
    atomic_init(&w->ran,0u);atomic_init(&w->cancelled,0u);
    atomic_init(&w->finalized,0u);
    praktor_c11_session_config_init(&config);
    if(plan_mode==1)admission=praktor_c11_orch_session_plan_create_native_shell(
        praktor_workflow_snapshot_task_tree(f->snapshot,index),
        &config.tree,&w->plan,reason,sizeof reason);
    else if(plan_mode==2)admission=praktor_c11_orch_session_plan_create_native_command_lines(
        praktor_workflow_snapshot_task_tree(f->snapshot,index),
        &config.tree,&w->plan,reason,sizeof reason);
    else admission=praktor_c11_orch_session_plan_create(
        praktor_workflow_snapshot_task_tree(f->snapshot,index),
        &config.tree,&w->plan,reason,sizeof reason);
    check(admission==SALTS_OK&&w->plan!=NULL);
    if(!w->plan)info("Worker Process Plan: %s",reason);
    check(praktor_c11_value_store_init(&w->input,12u,64u,512u)==PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_workflow_load_task_text_scope(
        f->snapshot,index,&f->root,&w->input)==PRAKTOR_C11_VALUE_OK);
}
static void release_worker(process_worker *w) {
    /* Wait for FINALIZE + CLOSED idle before destroying borrowed inputs. */
    praktor_c11_orch_session_plan_destroy(w->plan);
    praktor_c11_value_store_destroy(&w->input);
}
static void run_worker(void *user) {
    process_worker *w=(process_worker *)user;
    praktor_c11_native_host host={0};
    praktor_c11_native_host_config hc={0};
    praktor_c11_bt_host hooks={0};
    praktor_c11_session_config config={0};
    praktor_c11_session session={0};
    size_t len=0u;
    int r;
    uint64_t started=0u;
    atomic_fetch_add(&w->ran,1u);
    praktor_c11_native_host_config_init(&hc,12u,64u,512u);
    hc.max_running_leaves=1u;
    r=praktor_c11_native_host_init_with_config(&host,&hc);
    if(r!=SALTS_OK){w->init_status=r;goto done;}
    r=praktor_c11_native_host_bind(&host,&hooks);
    if(r!=SALTS_OK){w->init_status=r;goto done;}
    r=praktor_c11_value_store_replace_clone(
        praktor_c11_native_host_values(&host),&w->input);
    if(r!=PRAKTOR_C11_VALUE_OK){w->init_status=r;goto done;}
    praktor_c11_session_config_init(&config);
    config.host=hooks;
    config.retainer=w->retainer; /* sole worker uses it until FINALIZE */
    if(w->cancel_after_first_tick)config.shell_close_budget_ms=0u;
    w->init_status=praktor_c11_orch_session_plan_init_session(
        w->plan,&config,&session);
    if(w->init_status!=SALTS_OK)goto done;

    if(w->cancel_after_first_tick) {
        /* The worker, never the Owner, creates a real in-flight Process. */
        w->tick_status=praktor_c11_session_tick(&session,&w->outcome);
        if(w->tick_status!=SALTS_OK||w->outcome!=SALTS_BT_RUNNING)goto done;
    }
    atomic_fetch_add(&w->gate->started,1u);
    started=cmeta_hrtime();
    while(!atomic_load(&w->gate->release)&&
          cmeta_hrtime()-started<UINT64_C(7000000000))cmeta_sleep_ms(1u);

    if(w->cancel_after_first_tick) {
        if(atomic_load(&w->gate->cancel)) {
            w->cancel_status=praktor_c11_session_cancel(&session);
        }
    } else {
        started=cmeta_hrtime();
        do {
            w->tick_status=praktor_c11_session_tick(&session,&w->outcome);
            if(w->tick_status!=SALTS_OK||w->outcome!=SALTS_BT_RUNNING)break;
            cmeta_sleep_ms(1u);
        } while(cmeta_hrtime()-started<UINT64_C(8000000000));
        if(w->tick_status==SALTS_OK&&w->outcome==SALTS_BT_SUCCESS&&w->selected_key)
            w->selected_status=praktor_c11_value_store_get(
                praktor_c11_native_host_values(&host),
                w->selected_key,w->selected,sizeof w->selected,&len);
    }

done:
    if(session.impl) {
        /* Local cancellation precedes close/retainer adoption. Never let an
         * external thread call into the active Session/Host. */
        if(w->outcome==SALTS_BT_RUNNING&&w->cancel_status==SALTS_EINVAL)
            w->cancel_status=praktor_c11_session_cancel(&session);
        w->destroy_status=praktor_c11_session_destroy(&session);
    }
    praktor_c11_native_host_destroy(&host);
}
static void cancel_queued(void *user) {
    process_worker *w=(process_worker *)user;
    /* No native Session was created for this rejected queue entry. */
    atomic_fetch_add(&w->cancelled,1u);
}
static void finalized(void *user) {
    process_worker *w=(process_worker *)user;
    atomic_fetch_add(&w->finalized,1u);
}
static cflow_executor_task descriptor(process_worker *w) {
    cflow_executor_task task={0};
    task.run=run_worker;task.cancel=cancel_queued;
    task.finalize=finalized;task.user=w;
    return task;
}
static void assert_worker(const process_worker *w,const char *expected) {
    check(atomic_load(&w->ran)==1u&&atomic_load(&w->cancelled)==0u);
    check(atomic_load(&w->finalized)==1u);
    check(w->init_status==SALTS_OK&&w->tick_status==SALTS_OK);
    check(w->outcome==SALTS_BT_SUCCESS);
    check(w->destroy_status==SALTS_OK);
    check(w->selected_status==PRAKTOR_C11_VALUE_OK);
    check(strcmp(w->selected,expected)==0);
}
static bool await_retainer(cflow_process_retainer *r,size_t *reaped_total) {
    const uint64_t started=cmeta_hrtime();
    size_t pending=SIZE_MAX;
    int status=SALTS_EBUSY;
    *reaped_total=0u;
    do {
        size_t reaped=0u;
        status=cflow_process_retainer_run_ready(r,64u,&reaped,&pending);
        *reaped_total+=reaped;
        if(status!=SALTS_OK&&status!=SALTS_EBUSY)return false;
        if(pending)cmeta_sleep_ms(2u);
    } while(pending&&cmeta_hrtime()-started<UINT64_C(8000000000));
    return pending==0u&&status==SALTS_OK;
}
spec("C11 real Shell/NativeIO worker-to-owner recovery handoff") {
    it("holds real Command+ParseLines publication until CFlow and retainer both settle") {
        process_fixture f;
        process_worker command,successor;
        process_gate first_gate,second_gate;
        cflow_executor executor={0},next={0};
        cflow_executor_control control={0},next_control={0};
        cflow_process_retainer_token hold={0};
        praktor_c11_value_txn transaction={0},last={0};
        const char *typed_text="{\"accepted\":true}";
        json_value_t *typed=json_parse(typed_text,strlen(typed_text));
        praktor_workflow_task_output_update update={0u,typed};
        const char *final_text="{\"result\":1}";
        json_value_t *last_typed=json_parse(final_text,strlen(final_text));
        praktor_workflow_task_output_update downstream={1u,last_typed};
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u,pending=SIZE_MAX;
        int publication=PRAKTOR_C11_PARALLEL_BUSY;
        gate_init(&first_gate);gate_init(&second_gate);
        fixture_init(&f,success_yaml,1u);
        if(!f.snapshot)return;
        check(typed!=NULL&&last_typed!=NULL);
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==0u);
        prepare_worker(&command,&f,0u,2,&first_gate,"parsed_count",false);
        check(cflow_executor_worker_init_with_capacity(&executor,1u,1u));
        check(cflow_executor_as_control(&executor,&control));
        {
            cflow_executor_task posted=descriptor(&command);
            check(cflow_executor_try_post_task(&executor,&posted)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(await_worker(&first_gate));
        check(root_is(&f.root,"BASE","untouched"));
        check(root_lacks(&f.root,"parsed_count"));
        check_dag(&f,1u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        atomic_store(&first_gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        assert_worker(&command,"1");
        check(root_lacks(&f.root,"parsed_count"));
        check(praktor_c11_value_txn_begin(&transaction,&f.root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(
            &transaction,"parsed_count",command.selected)==PRAKTOR_C11_VALUE_OK);
        /* A pending recovery reservation blocks BOTH typed and text publish
         * even though the worker callback has completed and finalized. */
        check(cflow_process_retainer_reserve(&f.retainer,&hold)==SALTS_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,&update,&transaction,1u,&control,&f.retainer,
            8u,&pending)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending>=1u&&transaction.impl!=NULL);
        check(root_lacks(&f.root,"parsed_count"));
        check(cflow_process_retainer_release(&f.retainer,hold)==SALTS_OK);
        {
            const uint64_t started=cmeta_hrtime();
            do {
                publication=praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
                    f.registry,&update,&transaction,1u,&control,&f.retainer,
                    64u,&pending);
                check(publication==PRAKTOR_C11_PARALLEL_BUSY||
                      publication==PRAKTOR_C11_PARALLEL_OK);
                if(publication==PRAKTOR_C11_PARALLEL_BUSY)cmeta_sleep_ms(2u);
            } while(publication==PRAKTOR_C11_PARALLEL_BUSY&&
                    cmeta_hrtime()-started<UINT64_C(8000000000));
        }
        check(publication==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&transaction.impl==NULL);
        check(praktor_workflow_task_registry_complete(f.registry,0u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,true)==SALTS_OK);
        check_dag(&f,0u,1u,1u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(root_is(&f.root,"parsed_count","1"));
        cflow_executor_destroy(&executor);release_worker(&command);

        /* An entirely new worker-local Session sees ONLY committed root. */
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==1u&&ids[0]==1u);
        prepare_worker(&successor,&f,1u,0,&second_gate,"confirmed",false);
        check(cflow_executor_worker_init_with_capacity(&next,1u,1u));
        check(cflow_executor_as_control(&next,&next_control));
        {
            cflow_executor_task posted=descriptor(&successor);
            check(cflow_executor_try_post_task(&next,&posted)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(await_worker(&second_gate));
        check(cflow_executor_control_shutdown(&next_control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        atomic_store(&second_gate.release,true);
        check(cflow_executor_control_wait_idle(&next_control)==CFLOW_EXECUTOR_WAIT_IDLE);
        assert_worker(&successor,"1");
        check(praktor_c11_value_txn_begin(&last,&f.root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&last,"confirmed",successor.selected)==
              PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,&downstream,&last,1u,&next_control,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&last.impl==NULL);
        check(praktor_workflow_task_registry_complete(f.registry,1u)==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,1u,true)==SALTS_OK);
        check_dag(&f,0u,2u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,true);
        check(root_is(&f.root,"confirmed","1"));
        cflow_executor_destroy(&next);release_worker(&successor);
        json_free(typed);json_free(last_typed);
        fixture_destroy(&f);
    }

    it("cancel-first worker Shell adopts live NativeIO and prevents queued task admission") {
        process_fixture f;
        process_worker active,queued;
        process_gate gate;
        cflow_executor executor={0};
        cflow_executor_control control={0};
        cflow_executor_protocol_stats stats={0};
        cflow_process_retainer_token hold={0};
        praktor_c11_value_txn staged[2]={{0},{0}};
        const char *typed_text="{\"should_not\":true}";
        json_value_t *typed=json_parse(typed_text,strlen(typed_text));
        praktor_workflow_task_output_update outputs[2]={{0u,typed},{1u,typed}};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_PENDING;
        size_t ids[2]={SIZE_MAX,SIZE_MAX},count=0u;
        size_t pending=SIZE_MAX,reaped=0u,reaped_before=0u;
        gate_init(&gate);
        fixture_init(&f,cancel_yaml,2u);
        if(!f.snapshot)return;
        check(typed!=NULL);
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_OK);
        check(count==2u&&ids[0]==0u&&ids[1]==1u);
        prepare_worker(&active,&f,0u,1,&gate,NULL,true);
        prepare_worker(&queued,&f,1u,0,&gate,"queued_result",false);
        check(cflow_executor_worker_init_with_capacity(&executor,1u,2u));
        check(cflow_executor_as_control(&executor,&control));
        {
            cflow_executor_task first=descriptor(&active);
            check(cflow_executor_try_post_task(&executor,&first)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(await_worker(&gate)); /* real OS Process already RUNNING */
        {
            cflow_executor_task second=descriptor(&queued);
            check(cflow_executor_try_post_task(&executor,&second)==CFLOW_ADMISSION_ACCEPTED);
        }
        check(root_is(&f.root,"BASE","untouched"));
        check_dag(&f,2u,0u,0u,PRAKTOR_WORKFLOW_DAG_CONTINUE,false);
        check(cflow_executor_control_shutdown(
            &control,CFLOW_EXECUTOR_SHUTDOWN_CANCEL_PENDING));
        atomic_store(&gate.cancel,true);
        atomic_store(&gate.release,true);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(cflow_executor_control_get_stats(&control,&stats));
        check(stats.accepted==2u&&stats.completed==1u&&stats.cancelled==1u);
        check(atomic_load(&active.ran)==1u&&atomic_load(&active.finalized)==1u);
        check(active.init_status==SALTS_OK&&active.tick_status==SALTS_OK);
        check(active.outcome==SALTS_BT_RUNNING);
        check(active.cancel_status==SALTS_OK||active.cancel_status==SALTS_EBUSY);
        check(active.destroy_status==SALTS_OK);
        check(atomic_load(&queued.ran)==0u&&atomic_load(&queued.cancelled)==1u);
        check(atomic_load(&queued.finalized)==1u);
        /* Only AFTER all callbacks settle does the Owner take retainer turns. */
        check(cflow_process_retainer_reserve(&f.retainer,&hold)==SALTS_OK);
        check(cflow_process_retainer_destroy(&f.retainer)==SALTS_EBUSY);
        check(cflow_process_retainer_run_ready(
            &f.retainer,8u,&reaped_before,&pending)==SALTS_EBUSY);
        check(pending>=1u);
        for(size_t i=0u;i<2u;++i) {
            check(praktor_c11_value_txn_begin(&staged[i],&f.root)==
                  PRAKTOR_C11_VALUE_OK);
            check(praktor_c11_value_txn_set(
                &staged[i],i?"queued_result":"should_not","visible")==
                  PRAKTOR_C11_VALUE_OK);
        }
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
            f.registry,outputs,staged,2u,&control,&f.retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_CANCELLED);
        /* Even an otherwise valid two-task output group is forbidden after
         * CFlow cancels one queued callback; no root or typed publication. */
        check(root_lacks(&f.root,"should_not"));
        check(root_lacks(&f.root,"queued_result"));
        check(staged[0].impl!=NULL&&staged[1].impl!=NULL);
        praktor_c11_value_txn_abort(&staged[0]);
        praktor_c11_value_txn_abort(&staged[1]);
        check(cflow_process_retainer_release(&f.retainer,hold)==SALTS_OK);
        check(await_retainer(&f.retainer,&reaped));
        check(reaped+reaped_before>=1u); /* real adopted Process */
        check(praktor_workflow_dag_stop(
            f.dag,PRAKTOR_WORKFLOW_DAG_CANCELLED)==SALTS_OK);
        check(praktor_workflow_task_registry_fail(
            f.registry,0u,vstr_from_cstr("cancelled"))==SALTS_OK);
        check(praktor_workflow_dag_finish(f.dag,0u,false)==SALTS_OK);
        check(praktor_workflow_dag_refuse(f.dag,1u)==SALTS_OK);
        check_dag(&f,0u,1u,0u,PRAKTOR_WORKFLOW_DAG_CANCELLED,false);
        count=SIZE_MAX;
        check(praktor_workflow_dag_claim_ready(f.dag,ids,2u,&count)==SALTS_EBUSY);
        check(count==0u);
        check(root_is(&f.root,"BASE","untouched"));
        check(root_lacks(&f.root,"queued_result"));
        check(praktor_workflow_task_registry_state(f.registry,1u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        cflow_executor_destroy(&executor);
        release_worker(&active);release_worker(&queued);
        json_free(typed);
        fixture_destroy(&f);
    }
}
