#include "workflow_parallel_process_guard.h"
#include <salts/error_codes.h>
#include <stdint.h>

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
enum { PRAKTOR_C11_COHORT_GATE_MARKER = 0x50435231u };

int praktor_c11_process_cohort_recovery_init(
    praktor_c11_process_cohort_recovery *gate) {
    if(!gate||gate->marker!=0u||gate->first_native_error!=SALTS_OK)
        return PRAKTOR_C11_PARALLEL_INVALID;
    gate->marker=PRAKTOR_C11_COHORT_GATE_MARKER;
    gate->last_pending=SIZE_MAX; /* not a synthetic zero-pending ACK */
    return PRAKTOR_C11_PARALLEL_OK;
}

static bool valid_cohort_gate(const praktor_c11_process_cohort_recovery *gate) {
    return gate&&gate->marker==PRAKTOR_C11_COHORT_GATE_MARKER;
}

/* Per-worker retainers have NO internal cross-callback synchronization.
 * Validate the complete set before calling even the first retainer. In
 * particular two wrapper objects pointing to the same impl are not distinct
 * recovery owners. Owner alone drives this bounded, single-threaded sweep. */
static int validate_retainer_group(cflow_process_retainer *const *owners,
                                   size_t count,size_t max_steps) {
    if(!owners||!count||!max_steps||
       count>PRAKTOR_C11_PROCESS_COHORT_MAX_RETAINERS)
        return PRAKTOR_C11_PARALLEL_INVALID;
    for(size_t i=0u;i<count;++i) {
        if(!owners[i]||!owners[i]->impl)return PRAKTOR_C11_PARALLEL_INVALID;
        for(size_t j=0u;j<i;++j)
            if(owners[i]==owners[j]||owners[i]->impl==owners[j]->impl)
                return PRAKTOR_C11_PARALLEL_INVALID;
    }
    return PRAKTOR_C11_PARALLEL_OK;
}

/* No native Process operation is permitted until cohort owner identity has
 * been validated. Bind only on the first exclusive Owner drain turn; do not
 * bind an OPEN executor's transient preflight. */
static int validate_bound_owners(
    const praktor_c11_process_cohort_recovery *gate,
    cflow_process_retainer *const *owners,size_t count) {
    if(!gate->bound_count)return PRAKTOR_C11_PARALLEL_OK;
    if(count!=gate->bound_count)return PRAKTOR_C11_PARALLEL_INVALID;
    for(size_t i=0u;i<count;++i)
        if(owners[i]!=gate->bound_owners[i]||
           owners[i]->impl!=gate->bound_impls[i])
            return PRAKTOR_C11_PARALLEL_INVALID;
    return PRAKTOR_C11_PARALLEL_OK;
}

static int bind_cohort_owners(
    praktor_c11_process_cohort_recovery *gate,
    cflow_process_retainer *const *owners,size_t count) {
    if(validate_bound_owners(gate,owners,count)!=PRAKTOR_C11_PARALLEL_OK)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(gate->bound_count)return PRAKTOR_C11_PARALLEL_OK;
    for(size_t i=0u;i<count;++i) {
        gate->bound_owners[i]=owners[i];
        gate->bound_impls[i]=owners[i]->impl;
    }
    gate->bound_count=count;
    return PRAKTOR_C11_PARALLEL_OK;
}

int praktor_c11_workflow_parallel_drain_process_retainers(
    praktor_c11_process_cohort_recovery *gate,
    cflow_process_retainer *const *owners,size_t count,size_t max_steps,
    size_t *out_pending,size_t *out_reaped) {
    size_t pending_total=0u,reaped_total=0u;
    int result=PRAKTOR_C11_PARALLEL_OK;
    if(out_pending)*out_pending=0u;
    if(out_reaped)*out_reaped=0u;
    if(!valid_cohort_gate(gate)||
       validate_retainer_group(owners,count,max_steps)!=PRAKTOR_C11_PARALLEL_OK||
       bind_cohort_owners(gate,owners,count)!=PRAKTOR_C11_PARALLEL_OK)
        return PRAKTOR_C11_PARALLEL_INVALID;
    for(size_t i=0u;i<count;++i) {
        size_t pending=0u,reaped=0u;
        const int status=cflow_process_retainer_run_ready(
            owners[i],max_steps,&reaped,&pending);
        if(pending>SIZE_MAX-pending_total||reaped>SIZE_MAX-reaped_total)
            return PRAKTOR_C11_PARALLEL_INVALID;
        pending_total+=pending;
        reaped_total+=reaped;
        /* Keep draining independent owners after an error, but never
         * normalize that error into a BUSY retry or success. */
        if(status!=SALTS_OK&&status!=SALTS_EBUSY) {
            if(gate->first_native_error==SALTS_OK)
                gate->first_native_error=status; /* first provider error wins */
            result=PRAKTOR_C11_PARALLEL_RECOVERY;
        }
        else if(result==PRAKTOR_C11_PARALLEL_OK&&
                (status==SALTS_EBUSY||pending!=0u))
            result=PRAKTOR_C11_PARALLEL_BUSY;
    }
    gate->last_pending=pending_total; /* one full bounded sweep completed */
    if(out_pending)*out_pending=pending_total;
    if(out_reaped)*out_reaped=reaped_total;
    /* A later successful cleanup cannot revive an aborted cohort. */
    if(gate->first_native_error!=SALTS_OK)
        return PRAKTOR_C11_PARALLEL_RECOVERY;
    return result;
}

int praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,size_t branch_count,
    const bool *branch_succeeded,
    praktor_c11_process_cohort_recovery *gate,cflow_executor_control *control,
    cflow_process_retainer *const *owners,size_t count,
    size_t max_steps,size_t *out_pending) {
    cflow_executor_protocol_stats stats={0};
    cflow_executor_wait_status waited;
    int drained;
    if(out_pending)*out_pending=0u;
    if(!registry||!task_outputs||!branch_values||!branch_count||
       !branch_succeeded||!valid_cohort_gate(gate)||!control||branch_count!=count||
       validate_retainer_group(owners,count,max_steps)!=PRAKTOR_C11_PARALLEL_OK||
       validate_bound_owners(gate,owners,count)!=PRAKTOR_C11_PARALLEL_OK)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(!cflow_executor_control_get_stats(control,&stats))
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(stats.accepted!=branch_count||stats.rejected_full!=0u||
       stats.rejected_closed!=0u||stats.rejected_would_block!=0u)
        return PRAKTOR_C11_PARALLEL_INVALID;
    if(stats.cancelled!=0u)
        return PRAKTOR_C11_PARALLEL_CANCELLED;
    /* No retainer is even observed while any worker callback/finalizer
     * could still reserve, adopt, or close a native Process. */
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
    if(gate->first_native_error!=SALTS_OK) {
        /* No new Process poll occurs here. Reuse only the LAST actually
         * observed pending count; never invent a terminal zero. */
        if(out_pending)*out_pending=gate->last_pending;
        return PRAKTOR_C11_PARALLEL_RECOVERY;
    }
    /* CFlow callback completion is NOT a guarantee of successful Process.
     * Read semantic results only after all finalizers and idle have settled. */
    for(size_t i=0u;i<branch_count;++i)
        if(!branch_succeeded[i])return PRAKTOR_C11_PARALLEL_FAILED;
    drained=praktor_c11_workflow_parallel_drain_process_retainers(
        gate,owners,count,max_steps,out_pending,NULL);
    if(drained!=PRAKTOR_C11_PARALLEL_OK)return drained;
    return praktor_c11_workflow_parallel_commit(
        registry,task_outputs,branch_values,branch_count);
}
