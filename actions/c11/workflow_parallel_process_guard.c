#include "workflow_parallel_process_guard.h"
#include <salts/error_codes.h>

/* Process retainer is the sole owner of reservations/adopted Process handles.
 * A successful worker callback or cancellation request never implies native
 * I/O has quiesced; only the retainer can attest pending == 0. */
int praktor_c11_workflow_parallel_commit_after_process_drain(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,
    size_t branch_count,
    cflow_process_retainer *retainer,
    size_t max_steps,
    size_t *out_pending) {
    size_t pending=0u,reaped=0u;
    int status;
    if(out_pending)*out_pending=0u;
    if(!registry||!task_outputs||!branch_values||!branch_count||
       !retainer||!retainer->impl||!max_steps)
        return PRAKTOR_C11_PARALLEL_INVALID;
    /* Caller has already stopped producers, terminated/drained CFlow worker
     * callbacks and owns this exclusive turn. The real Process owner may
     * release callback payloads while driving, but cannot be reentered. */
    status=cflow_process_retainer_run_ready(retainer,max_steps,&reaped,&pending);
    if(out_pending)*out_pending=pending;
    if(status==SALTS_EBUSY||pending!=0u)
        return PRAKTOR_C11_PARALLEL_BUSY;
    if(status!=SALTS_OK)
        return PRAKTOR_C11_PARALLEL_RECOVERY;
    return praktor_c11_workflow_parallel_commit(
        registry,task_outputs,branch_values,branch_count);
}
