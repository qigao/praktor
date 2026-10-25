#ifndef PRAKTOR_C11_WORKFLOW_PARALLEL_PROCESS_GUARD_H
#define PRAKTOR_C11_WORKFLOW_PARALLEL_PROCESS_GUARD_H

#include "workflow_parallel_commit.h"
#include <salts/process_retainer.h>
#include <cflow/executor.h>
#include <stdbool.h>
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

/* Strict CFlow executor + native Process preflight for a fresh group-owned
 * Executor. Caller MUST have already selected DRAIN or CANCEL_PENDING and
 * ceased producer admission; OPEN and CLOSING Executors cannot publish.
 *
 * Requires exact accepted == branch_count, zero rejected admissions, no
 * cancelled callbacks, zero running/queued tasks, CLOSED lifecycle, and a
 * successful wait_idle acknowledgment before checking Process leases.
 * CANCELLED is a durable group refusal (caller aborts transaction handles).
 * Missing/admission-refused branches are INVALID. In-flight work is BUSY.
 *
 * This check verifies CFlow's callback settlement, not the application's
 * semantic success result: any callback-reported failure still requires
 * caller-side abort. A single owner turn must span both gates and publication.
 */
int praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values,
    size_t branch_count,
    cflow_executor_control *control,
    cflow_process_retainer *retainer,
    size_t max_steps,
    size_t *out_pending);

/* Per-worker Process barrier: each CFlow callback exclusively borrows its
 * own bounded retainer. Do not share an impl between array entries. Caller
 * MUST have joined *all* callbacks/finalizers and stopped admission before
 * driving ANY retainer from the Owner. No worker may resume or reenter the
 * retainer during this phase. max_steps bounds work PER retainer.
 *
 * A nonzero pending total (including unused live reservations) is BUSY;
 * an actual close/NativeIO error from ANY worker retainer is RECOVERY.
 * Both statuses preserve the caller's staged output group for explicit abort
 * or a later exclusive recovery turn. Reaped counts include only real
 * completed adopted Processes. Invalid/aliased retainers fail without a
 * single retainer operation.
 *
 * This drain-only API is for canceled/failed cohorts as well as success.
 * It does NOT prove CFlow quiescence itself; caller supplies that proof. */
int praktor_c11_workflow_parallel_drain_process_retainers(
    cflow_process_retainer *const *retainers, size_t retainer_count,
    size_t max_steps, size_t *out_pending, size_t *out_reaped);

/* Source-only owner-turn admission for one retainer PER branch (1:1).
 * The group must be CLOSED + finalized + idle, with no rejected/cancelled
 * callbacks; each retained Process must be fully reaped before the existing
 * optimistic typed/text group transaction commits. branch_succeeded is a
 * post-finalization immutable copy of actual branch semantic outcomes.
 * It is read ONLY after CLOSED + wait_idle; a false value returns
 * PARALLEL_FAILED before any retainer is driven or output is published.
 * Caller aborts the group and separately drains native leases.
 * All DAG/registry changes happen afterward on the application Owner.
 * No shared retainer, C++ production backend change, or public SDK switch. */
int praktor_c11_workflow_parallel_commit_after_cflow_and_process_drains(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_c11_value_txn *branch_values, size_t branch_count,
    const bool *branch_succeeded, cflow_executor_control *control,
    cflow_process_retainer *const *retainers, size_t retainer_count,
    size_t max_steps, size_t *out_pending);

#ifdef __cplusplus
}
#endif
#endif
