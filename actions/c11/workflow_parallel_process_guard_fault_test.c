/* Test-only simulated NativeIO/ack faults. The actual C11 Parallel transaction
 * and typed registry remain linked; only Process recovery progress is injected.
 * Native close failures cannot be normalized into BUSY or false success. */
#include "workflow_parallel_process_guard.h"
#include <string.h>
#include <tinytest.h>

static int injected_status=SALTS_OK;
static size_t injected_pending=0u;
static size_t calls=0u;
static int fault_retainer_ready(cflow_process_retainer *owner,size_t steps,
                                size_t *out_reaped,size_t *out_pending) {
    (void)owner;(void)steps;
    ++calls;
    if(out_reaped)*out_reaped=0u;
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
