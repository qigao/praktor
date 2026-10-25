#ifndef PRAKTOR_C11_WORKFLOW_SNAPSHOT_VALUES_H
#define PRAKTOR_C11_WORKFLOW_SNAPSHOT_VALUES_H

#include "value_store.h"
#include "yml/workflow_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Qualifies one narrow WorkflowContext admission boundary: root workflow
 * string variables override already-seeded system variables in a pre-existing
 * C11 value store. Task-local vars, env, TaskRegistry, typed WorkflowValue,
 * execution observers, fork/merge and Parallel remain separate contracts.
 *
 * Reuses the canonical C-owned Snapshot and the existing fixed-capacity C11
 * value_store_set_many() preflight/publication. Does not retain the Snapshot,
 * create another context store, perform IO, or execute workflow tasks.
 * No fallback or partial set on failure. The caller owns the store, must
 * serialize access, and must keep it alive for the duration of this call.
 *
 * Return PRAKTOR_C11_VALUE_OK, EINVAL (unsupported/invalid values),
 * ENOBUFS (field too large), ENOSPC (capacity), or ENOMEM (scratch).
 * The unchanged pre-existing store is usable after any failure.
 */
#define PRAKTOR_C11_WORKFLOW_MAX_ROOT_VARIABLES 4096u
int praktor_c11_workflow_seed_root_text_variables(
    const praktor_workflow_snapshot *snapshot,
    praktor_c11_value_store *store);

/* Qualification-only task-local text snapshot.
 * The root/base store is borrowed and never modified. Child must be a
 * zero-initialized, distinct handle; success uniquely owns an independent
 * clone of all visible root text values with task.vars overlaid atomically.
 * Failure destroys the provisional clone and leaves child (including its
 * prior zero state), root, and snapshot unchanged. The clone is a bounded
 * materialized view, not the final hierarchical WorkflowContext: it does
 * not track a local-write delta or permit merging inherited values back.
 * Reuse existing value_store transactions for a future owner-controlled
 * per-task/Parallel commit instead of merging this map wholesale. */
int praktor_c11_workflow_fork_task_text_variables(
    const praktor_workflow_snapshot *snapshot, size_t task_index,
    const praktor_c11_value_store *root,
    praktor_c11_value_store *child);

#ifdef __cplusplus
}
#endif
#endif
