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
    /* Surface the first actual NativeIO/close/ack error even if adopted
     * processes are still pending; do not turn a permanent failure into an
     * ordinary EBUSY retry or an invented terminal acknowledgment. */
    if(status!=SALTS_OK&&status!=SALTS_EBUSY)
        return PRAKTOR_C11_PARALLEL_RECOVERY;
    if(status==SALTS_EBUSY||pending!=0u)
        return PRAKTOR_C11_PARALLEL_BUSY;
    return praktor_c11_workflow_parallel_commit(
        registry,task_outputs,branch_values,branch_count);
}


int praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,
    size_t branch_count,
    cflow_executor_control *control,
    cflow_process_retainer *retainer,
    size_t max_steps,
    size_t *out_pending) {
    cflow_executor_protocol_stats stats={0};
    cflow_executor_wait_status waited;
    if(out_pending)*out_pending=0u;
    if(!registry||!task_outputs||!branch_values||!branch_count||
       !control||!retainer||!retainer->impl||!max_steps)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(!cflow_executor_control_get_stats(control,&stats))
        return PRAKTOR_C11_PARALLEL_INVALID;
    /* A fresh Executor is exclusively assigned to exactly one Parallel
     * cohort. No rejection can be silently ignored and counted as success. */
    if(stats.accepted!=branch_count||stats.rejected_full!=0u||
       stats.rejected_closed!=0u||stats.rejected_would_block!=0u)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(stats.cancelled!=0u)
        return PRAKTOR_C11_PARALLEL_CANCELLED;
    /* Closing admission is a MUST. "All callbacks complete" while the queue
     * is OPEN is insufficient: a producer could post again before commit. */
    if(stats.lifecycle!=CFLOW_EXECUTOR_CLOSED||
       stats.queued!=0u||stats.running!=0u||
       stats.completed!=stats.accepted)
        return PRAKTOR_C11_PARALLEL_BUSY;
    waited=cflow_executor_control_wait_idle(control);
    if(waited==CFLOW_EXECUTOR_WAIT_PENDING||
       waited==CFLOW_EXECUTOR_WAIT_WOULD_BLOCK)
        return PRAKTOR_C11_PARALLEL_BUSY;
    if(waited!=CFLOW_EXECUTOR_WAIT_IDLE)
        return PRAKTOR_C11_PARALLEL_INVALID;
    return praktor_c11_workflow_parallel_commit_after_process_drain(
        registry,task_outputs,branch_values,branch_count,retainer,
        max_steps,out_pending);
}
