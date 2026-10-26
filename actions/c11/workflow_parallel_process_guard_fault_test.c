/* Test-only simulated NativeIO/ack faults. The actual C11 Parallel transaction
 * and typed registry remain linked; only Process recovery progress is injected.
 * Native close failures cannot be normalized into BUSY or false success. */
#include "workflow_parallel_process_guard.h"
#include <string.h>
#include <tinytest.h>

static int injected_status=SALTS_OK;
static size_t injected_pending=0u;
static size_t calls=0u;
static cflow_process_retainer *override_owner=NULL;
static int override_status=SALTS_OK;
static size_t override_pending=0u;
static int fault_retainer_ready(cflow_process_retainer *owner,size_t steps,
                                size_t *out_reaped,size_t *out_pending) {
    (void)owner;(void)steps;
    ++calls;
    if(out_reaped)*out_reaped=0u;
    if(owner==override_owner) {
        if(out_pending)*out_pending=override_pending;
        return override_status;
    }
    if(out_pending)*out_pending=injected_pending;
    return injected_status;
}
#define cflow_process_retainer_run_ready fault_retainer_ready
#include "workflow_parallel_process_guard.c"
#undef cflow_process_retainer_run_ready

static praktor_workflow_task_registry *create_registry(void) {
    const char *yaml=
        "tasks:\n"
        "  - {name: left, command: echo left}\n";
    praktor_workflow_snapshot *snapshot=NULL;
    praktor_workflow_task_registry *registry=NULL;
    check(praktor_workflow_snapshot_create_inline(vstr_from_cstr(yaml),
        &snapshot,NULL,0)==SALTS_OK);
    check(praktor_workflow_task_registry_create(snapshot,&registry)==SALTS_OK);
    praktor_workflow_snapshot_destroy(snapshot);
    return registry;
}
spec("Parallel native Process recovery errors are fail-closed") {
    it("preserves the first NativeIO error even when leases remain active") {
        praktor_workflow_task_registry *registry=create_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn branch={0};
        cflow_process_retainer retainer={0};
        const char *json_text="{\"answer\":123}";
        json_value_t *json=json_parse(json_text,strlen(json_text));
        praktor_workflow_task_output_update update={0u,json};
        size_t pending=SIZE_MAX;
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        check(json!=NULL);
        check(cflow_process_retainer_init(&retainer,1u)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,2u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&branch,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&branch,"answer","value")==PRAKTOR_C11_VALUE_OK);
        injected_status=SALTS_EIO;
        injected_pending=1u;
        calls=0u;
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            registry,&update,&branch,1u,&retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==1u&&calls==1u);
        check(branch.impl!=NULL);
        check(praktor_c11_value_store_size(&root)==0u);
        check(praktor_workflow_task_registry_state(registry,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        injected_pending=0u; /* Error is still real even if no pending. */
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            registry,&update,&branch,1u,&retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==0u&&branch.impl!=NULL);
        injected_status=SALTS_OK;
        injected_pending=1u; /* Some provider returned OK but count is live. */
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            registry,&update,&branch,1u,&retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u&&branch.impl!=NULL);
        injected_pending=0u;
        check(praktor_c11_workflow_parallel_commit_after_process_drain(
            registry,&update,&branch,1u,&retainer,8u,&pending)==
            PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&!branch.impl);
        json_free(json);
        praktor_c11_value_store_destroy(&root);
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
        praktor_workflow_task_registry_destroy(registry);
    }
}
spec("Independent Process recovery failures remain fail-closed") {
    it("does not hide one worker's NativeIO error behind another drained retainer") {
        praktor_c11_process_cohort_recovery recovery={0};
        cflow_process_retainer local[2]={{0},{0}};
        cflow_process_retainer *owners[2]={&local[0],&local[1]};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX;
        check(praktor_c11_process_cohort_recovery_init(&recovery)==PRAKTOR_C11_PARALLEL_OK);
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_init(&local[i],1u)==SALTS_OK);
        injected_status=SALTS_OK;injected_pending=0u;
        override_owner=&local[1];override_status=SALTS_EIO;override_pending=1u;
        calls=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==1u&&reaped==0u&&calls==2u);
        check(recovery.first_native_error==SALTS_EIO);
        override_pending=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==0u);
        override_status=SALTS_EBUSY;override_pending=1u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==1u);
        override_status=SALTS_OK;override_pending=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,2u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==0u&&recovery.first_native_error==SALTS_EIO);
        override_owner=NULL;
        for(size_t i=0u;i<2u;++i)
            check(cflow_process_retainer_destroy(&local[i])==SALTS_OK);
    }
}

/* The single-retainer compatibility test above intentionally remains
 * stateless. Cohort qualification MUST use the same owner-local recovery
 * latch for all sweeps and publication attempts. */
static void completed_callback(void *user) {
    unsigned *observed=(unsigned *)user;
    ++*observed;
}
spec("NativeIO recovery poisoning survives a clean later Process drain") {
    it("never publishes committed typed/text outputs after a transient native error") {
        praktor_workflow_task_registry *registry=create_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn branch={0};
        praktor_c11_process_cohort_recovery recovery={0};
        cflow_process_retainer retainer={0};
        cflow_process_retainer *owners[1]={&retainer};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        unsigned callbacks=0u;
        const bool succeeded[1]={true};
        const char *data="{\"answer\":123}";
        json_value_t *json=json_parse(data,strlen(data));
        praktor_workflow_task_output_update update={0u,json};
        praktor_workflow_task_state state=PRAKTOR_WORKFLOW_TASK_RUNNING;
        size_t pending=SIZE_MAX,reaped=SIZE_MAX,prior_calls=0u;
        cflow_executor_task task={0};
        check(registry!=NULL&&json!=NULL);
        check(praktor_c11_process_cohort_recovery_init(&recovery)==
              PRAKTOR_C11_PARALLEL_OK);
        check(recovery.last_pending==SIZE_MAX); /* not yet observed */
        check(cflow_process_retainer_init(&retainer,1u)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,2u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&branch,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&branch,"answer","value")==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,1u));
        check(cflow_executor_as_control(&executor,&control));
        task.run=completed_callback;task.finalize=completed_callback;task.user=&callbacks;
        check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==1u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(callbacks==2u);
        injected_status=SALTS_OK;injected_pending=0u;
        override_owner=&retainer;override_status=SALTS_EIO;override_pending=1u;
        calls=0u;
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            registry,&update,&branch,1u,succeeded,&recovery,&control,
            owners,1u,8u,&pending)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==1u&&calls==1u);
        check(recovery.first_native_error==SALTS_EIO&&branch.impl!=NULL);
        check(recovery.last_pending==1u);
        /* Retainer remains unquieted: a rejected commit must not report
         * zero just because it deliberately skipped the native poll. */
        prior_calls=calls;
        pending=SIZE_MAX;
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            registry,&update,&branch,1u,succeeded,&recovery,&control,
            owners,1u,8u,&pending)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==1u&&calls==prior_calls);
        check(praktor_c11_value_store_size(&root)==0u);
        check(praktor_workflow_task_registry_state(registry,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);

        /* A later OK with zero pending may be a genuine cleanup result, but
         * can never undo the first observed provider failure. */
        override_status=SALTS_OK;override_pending=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,1u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==0u&&reaped==0u&&recovery.first_native_error==SALTS_EIO);
        check(recovery.last_pending==0u); /* explicit cleanup did observe zero */
        prior_calls=calls;
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            registry,&update,&branch,1u,succeeded,&recovery,&control,
            owners,1u,8u,&pending)==PRAKTOR_C11_PARALLEL_RECOVERY);
        check(pending==0u&&calls==prior_calls); /* no repeated native operations */
        check(praktor_c11_process_cohort_recovery_init(&recovery)==
              PRAKTOR_C11_PARALLEL_INVALID); /* poisoned cohort cannot reset */
        check(praktor_c11_value_store_size(&root)==0u&&branch.impl!=NULL);
        check(praktor_workflow_task_registry_state(registry,0u,&state)==SALTS_OK);
        check(state==PRAKTOR_WORKFLOW_TASK_PENDING);
        praktor_c11_value_txn_abort(&branch);
        check(branch.impl==NULL);
        override_owner=NULL;
        cflow_executor_destroy(&executor);
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
        json_free(json);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
    it("allows BUSY then clean recovery when no provider error ever occurred") {
        praktor_c11_process_cohort_recovery recovery={0};
        cflow_process_retainer retainer={0};
        cflow_process_retainer *owners[1]={&retainer};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX;
        check(praktor_c11_process_cohort_recovery_init(&recovery)==PRAKTOR_C11_PARALLEL_OK);
        check(cflow_process_retainer_init(&retainer,1u)==SALTS_OK);
        injected_status=SALTS_EBUSY;injected_pending=1u;
        override_owner=NULL;calls=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,1u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_BUSY);
        check(pending==1u&&recovery.first_native_error==SALTS_OK);
        injected_status=SALTS_OK;injected_pending=0u;
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,1u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_OK);
        check(pending==0u&&recovery.first_native_error==SALTS_OK&&calls==2u);
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
    }
}

spec("Completed Process cohort may publish exactly once") {
    it("rejects second typed/text commit and native recovery after publication") {
        praktor_workflow_task_registry *registry=create_registry();
        praktor_c11_value_store root={0};
        praktor_c11_value_txn first={0},second={0};
        praktor_c11_process_cohort_recovery recovery={0};
        cflow_process_retainer retainer={0};
        cflow_process_retainer *owners[1]={&retainer};
        cflow_executor executor={0};
        cflow_executor_control control={0};
        cflow_executor_task task={0};
        unsigned callbacks=0u;
        const bool succeeded[1]={true};
        const char *data="{\"answer\":123}";
        json_value_t *json=json_parse(data,strlen(data));
        praktor_workflow_task_output_update update={0u,json};
        size_t pending=SIZE_MAX,reaped=SIZE_MAX,length=0u;
        char value[32]={0};
        check(registry!=NULL&&json!=NULL);
        check(praktor_c11_process_cohort_recovery_init(&recovery)==PRAKTOR_C11_PARALLEL_OK);
        check(cflow_process_retainer_init(&retainer,1u)==SALTS_OK);
        check(praktor_c11_value_store_init(&root,4u,32u,32u)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_begin(&first,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&first,"answer","original")==PRAKTOR_C11_VALUE_OK);
        check(cflow_executor_manual_init_with_capacity(&executor,1u));
        check(cflow_executor_as_control(&executor,&control));
        task.run=completed_callback;task.finalize=completed_callback;task.user=&callbacks;
        check(cflow_executor_try_post_task(&executor,&task)==CFLOW_ADMISSION_ACCEPTED);
        check(cflow_executor_control_shutdown(&control,CFLOW_EXECUTOR_SHUTDOWN_DRAIN));
        check(cflow_executor_run_ready(&executor)==1u);
        check(cflow_executor_control_wait_idle(&control)==CFLOW_EXECUTOR_WAIT_IDLE);
        check(callbacks==2u);
        override_owner=NULL;
        injected_status=SALTS_OK;injected_pending=0u;calls=0u;
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            registry,&update,&first,1u,succeeded,&recovery,&control,
            owners,1u,8u,&pending)==PRAKTOR_C11_PARALLEL_OK);
        check(first.impl==NULL&&recovery.sealed&&pending==0u&&calls==1u);
        check(praktor_c11_value_store_get(
            &root,"answer",value,sizeof value,&length)==PRAKTOR_C11_VALUE_OK);
        check(length==8u&&strcmp(value,"original")==0);
        check(praktor_c11_value_txn_begin(&second,&root)==PRAKTOR_C11_VALUE_OK);
        check(praktor_c11_value_txn_set(&second,"retry","forbidden")==PRAKTOR_C11_VALUE_OK);
        /* A second group would otherwise be silently re-admitted by the
         * same CLOSED CFlow control and zero-pending retained Process owner. */
        check(praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
            registry,&update,&second,1u,succeeded,&recovery,&control,
            owners,1u,8u,&pending)==PRAKTOR_C11_PARALLEL_INVALID);
        check(second.impl!=NULL&&calls==1u);
        check(praktor_c11_workflow_parallel_drain_process_retainers(
            &recovery,owners,1u,8u,&pending,&reaped)==PRAKTOR_C11_PARALLEL_INVALID);
        check(calls==1u&&praktor_c11_value_store_size(&root)==1u);
        check(praktor_c11_process_cohort_recovery_init(&recovery)==PRAKTOR_C11_PARALLEL_INVALID);
        praktor_c11_value_txn_abort(&second);
        check(praktor_workflow_task_registry_complete(registry,0u)==SALTS_OK);
        cflow_executor_destroy(&executor);
        check(cflow_process_retainer_destroy(&retainer)==SALTS_OK);
        json_free(json);
        praktor_c11_value_store_destroy(&root);
        praktor_workflow_task_registry_destroy(registry);
    }
}
