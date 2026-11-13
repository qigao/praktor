#ifndef PRAKTOR_WORKFLOW_PARALLEL_COMMIT_H
#define PRAKTOR_WORKFLOW_PARALLEL_COMMIT_H

#include "actions/value_store.h"
#include "yml/workflow_task_registry.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Owner-turn publication for completed Parallel branches.
 * Each branch has exactly one distinct task output slot and exactly one
 * ACTIVE optimistic transaction on the SAME caller-owned root value store.
 *
 * 1. Deep-copy/validate ALL typed task output JSONs using the existing C11
 *    TaskRegistry's prepare API, without publishing anything.
 * 2. Commit all branch text-value transactions via the existing
 *    praktor_value_txn_commit_group() (conflict/capacity preflight,
 *    all-or-nothing); NO hand-rolled value map/merge implementation.
 * 3. Publish the pre-staged typed JSON outputs via allocation-free swaps.
 *
 * A successful call consumes/aborts the input transactions (their impls become
 * NULL) and commits all task outputs. Failure leaves registry and root values
 * untouched; transactions remain active and caller must abort/retry them.
 * Update JSON is borrowed only for this call; committed JSON is independent.
 *
 * CRITICAL single-driver qualification: stop/cancel/join all branch producers
 * BEFORE invoking this method; no mutation of the registry/root/transactions,
 * callback reentry or concurrent destruction is permitted for the entire
 * call. The underlying value txn group never calls registry callbacks. Thus
 * no registry state can change between prepare and allocation-free publish.
 *
 * Does not schedule, execute, cancel or await any branches, and does not
 * substitute for execution-level atomic Parallel qualification. This is
 * an internal ABI, not an installed public SDK contract. */
typedef enum praktor_parallel_commit_status {
    PRAKTOR_PARALLEL_OK = 0,
    PRAKTOR_PARALLEL_INVALID,
    PRAKTOR_PARALLEL_CONFLICT,
    PRAKTOR_PARALLEL_CAPACITY,
    PRAKTOR_PARALLEL_MEMORY,
    PRAKTOR_PARALLEL_BUSY,
    PRAKTOR_PARALLEL_INVARIANT,
    PRAKTOR_PARALLEL_RECOVERY, /* Native Process/ack failure; retain owner */
    PRAKTOR_PARALLEL_CANCELLED, /* CFlow canceled any accepted task; abort group */
    PRAKTOR_PARALLEL_FAILED /* Callback settled but leaf failed; abort group */
} praktor_parallel_commit_status;

int praktor_workflow_parallel_commit(
    praktor_workflow_task_registry *registry,
    const praktor_workflow_task_output_update *task_outputs,
    praktor_value_txn *branch_values,size_t branch_count);

#ifdef __cplusplus
}
#endif
#endif
