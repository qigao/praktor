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

#ifdef __cplusplus
}
#endif
#endif
