#include "workflow_parallel_commit.h"

/* The *only* mutable value publication is the established optimistic
 * transaction-group implementation. The staged task JSONs are already owned
 * and are published only after that group reports success. */
static int registry_status(int result) {
    switch(result) {
    case SALTS_OK:return PRAKTOR_C11_PARALLEL_OK;
    case SALTS_ENOMEM:return PRAKTOR_C11_PARALLEL_MEMORY;
    case SALTS_EBUSY:return PRAKTOR_C11_PARALLEL_BUSY;
    default:return PRAKTOR_C11_PARALLEL_INVALID;
    }
}
static int value_status(int result) {
    switch(result) {
    case PRAKTOR_C11_VALUE_OK:return PRAKTOR_C11_PARALLEL_OK;
    case PRAKTOR_C11_VALUE_ECONFLICT:return PRAKTOR_C11_PARALLEL_CONFLICT;
    case PRAKTOR_C11_VALUE_ENOSPC:
    case PRAKTOR_C11_VALUE_ENOBUFS:return PRAKTOR_C11_PARALLEL_CAPACITY;
    case PRAKTOR_C11_VALUE_ENOMEM:return PRAKTOR_C11_PARALLEL_MEMORY;
    default:return PRAKTOR_C11_PARALLEL_INVALID;
    }
}
int praktor_c11_workflow_parallel_commit(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,size_t branch_count) {
    praktor_workflow_task_output_group *staged=NULL;
    int status;
    if(!registry||!task_outputs||!branch_values||
       !branch_count||branch_count>praktor_workflow_task_registry_count(registry))
        return PRAKTOR_C11_PARALLEL_INVALID;
    status=praktor_workflow_task_registry_prepare_output_group(
        registry,task_outputs,branch_count,&staged);
    if(status!=SALTS_OK)return registry_status(status);

    /* Successful commit aborts/consumes all transactions. On any refusal they
     * remain active and the caller still owns their cleanup. */
    status=praktor_c11_value_txn_commit_group(branch_values,branch_count);
    if(status!=PRAKTOR_C11_VALUE_OK) {
        praktor_workflow_task_registry_discard_output_group(staged);
        return value_status(status);
    }

    /* Under the documented single-driver owner turn there is no intervening
     * writer between JSON prepare and this allocation-free publication.
     * If violated, do not invent rollback of already committed value state:
     * signal an explicit invariant failure rather than claiming atomicity. */
    status=praktor_workflow_task_registry_publish_output_group(
        registry,&staged);
    if(status!=SALTS_OK) {
        praktor_workflow_task_registry_discard_output_group(staged);
        return PRAKTOR_C11_PARALLEL_INVARIANT;
    }
    return PRAKTOR_C11_PARALLEL_OK;
}
