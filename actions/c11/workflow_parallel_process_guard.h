#ifndef PRAKTOR_C11_WORKFLOW_PARALLEL_PROCESS_GUARD_H
#define PRAKTOR_C11_WORKFLOW_PARALLEL_PROCESS_GUARD_H

#include "workflow_parallel_commit.h"
#include <salts/process_retainer.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Source-only C11 owner-turn Process barrier for a Parallel transaction group.
 * The caller must first stop all new branch/process admission and wait for the
 * Salts CFlow Executor's run/cancel/finalize callbacks to settle. These steps
 * cannot be inferred from task status alone; the caller owns that evidence.
 *
 * Retainer ownership is borrowed. This call drives at most max_steps of each
 * retained Process's close/reap work through the canonical SaltsUtils API.
 * It refuses to commit unless the retainer reports SALTS_OK AND pending == 0,
 * including zero outstanding reservations. Failed NativeIO/ack or incomplete
 * cancellation retains all C11 transaction handles, typed task outputs and
 * native recovery leases for later single-owner turns.
 *
 * IMPORTANT: No other thread may reserve/adopt/drive/destroy this retainer or
 * mutate the registry/transactions between the zero-pending preflight and
 * publication. The owner MUST hold one exclusive turn and block producer
 * admission. A later external Process operation would invalidate the barrier.
 *
 * SALTS_EBUSY (or pending > 0) maps to PARALLEL_BUSY.
 * Other Process errors map to PARALLEL_RECOVERY and are NOT swallowed.
 * Unsupported/NULL owner and zero max_steps fail invalid before publication.
 * If out_pending is given it is always set to the latest observed count.
 * No new task executor, Process owner or fallback exists here. */
int praktor_c11_workflow_parallel_commit_after_process_drain(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,
    size_t branch_count,
    cflow_process_retainer *retainer,
    size_t max_steps,
    size_t *out_pending);

#ifdef __cplusplus
}
#endif
#endif
