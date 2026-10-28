#ifndef PRAKTOR_WORKFLOW_DAG_STATE_H
#define PRAKTOR_WORKFLOW_DAG_STATE_H

#include "yml/workflow_dependencies.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Qualification-only, single-owner DAG readiness counter over the existing
 * immutable C11 workflow_dependencies projection. It DOES NOT own or mutate
 * that graph, execute tasks, create workers, publish values, or implement a
 * second queue. The graph must outlive the state; all mutations happen on the
 * one application owner turn, never inside a CFlow WorkerExecutor callback.
 *
 * claim_ready() reserves task indices in declaration order, bounded by both
 * max_active and caller buffer capacity. Every claimed task MUST receive
 * exactly one owner-side finish() (after CFlow run/cancel/finalize settlement)
 * or refuse() (if CFlow did not admit it and will invoke NO callback).
 * Cancellation/deadline/failure stops further claims; other admitted tasks
 * keep their reservations until their callbacks settle.
 *
 * Dependency edges retain their original cardinality: two identical edges
 * count twice and must be retired twice on predecessor success. Terminal
 * failure NEVER unblocks descendants. No implicit fallback or retry. */
typedef struct praktor_workflow_dag_state praktor_workflow_dag_state;

typedef enum praktor_workflow_dag_terminal {
    PRAKTOR_WORKFLOW_DAG_CONTINUE = 0,
    PRAKTOR_WORKFLOW_DAG_FAILED,
    PRAKTOR_WORKFLOW_DAG_CANCELLED,
    PRAKTOR_WORKFLOW_DAG_DEADLINE,
    PRAKTOR_WORKFLOW_DAG_ADMISSION_REFUSED
} praktor_workflow_dag_terminal;

typedef struct praktor_workflow_dag_stats {
    size_t task_count;
    size_t active;
    size_t completed;
    size_t ready;
    praktor_workflow_dag_terminal terminal;
    bool quiescent;
    bool all_succeeded;
} praktor_workflow_dag_stats;

/* No partial owner on failure; *out remains unchanged. Returns SALTS_OK,
 * SALTS_EINVAL or SALTS_ENOMEM. Graph's explicit task/edge budget is reused. */
int praktor_workflow_dag_state_create(
    const praktor_workflow_dependencies *graph, size_t max_active,
    praktor_workflow_dag_state **out);
void praktor_workflow_dag_state_destroy(praktor_workflow_dag_state *state);

/* Caller receives only RESERVED indices. A capacity of zero is invalid.
 * On valid calls *out_count is initialized to zero. If stopped, returns
 * SALTS_EBUSY, never newly claims a task. No heap allocation or callbacks. */
int praktor_workflow_dag_claim_ready(praktor_workflow_dag_state *state,
    size_t *out_indices, size_t capacity, size_t *out_count);

/* Non-mutating, owner-only admission preflight. Returns true ONLY if an
 * immutable DAG task is presently RESERVED by claim_ready. This permits
 * publication adapters to reject an incorrect cohort/task mapping BEFORE
 * committing typed/text outputs. Valid lookup SALTS_OK; invalid index or
 * out pointer SALTS_EINVAL, leaving out unchanged on invalid arguments. */
int praktor_workflow_dag_is_reserved(const praktor_workflow_dag_state *state,
    size_t task_index, bool *out_reserved);

/* One terminal callback settlement. SUCCESS decrements canonical outgoing
 * edges only when the overall run is still live; FAILURE closes admission.
 * Caller must settle CFlow's finalize BEFORE invoking this owner-side API. */
int praktor_workflow_dag_finish(praktor_workflow_dag_state *state,
    size_t task_index, bool success);

/* CFlow rejected a claimed task at submission: no run/cancel/finalize will
 * arrive. Record refusal once; caller must still settle accepted siblings. */
int praktor_workflow_dag_refuse(praktor_workflow_dag_state *state,
    size_t task_index);

/* External stop reason (including cancellation/deadline) before publication.
 * First terminal reason wins; a reason must not be CONTINUE. */
int praktor_workflow_dag_stop(praktor_workflow_dag_state *state,
    praktor_workflow_dag_terminal reason);

/* Read-only single-owner snapshot; no mutation or heap allocation. */
int praktor_workflow_dag_get_stats(const praktor_workflow_dag_state *state,
    praktor_workflow_dag_stats *out);

#ifdef __cplusplus
}
#endif
#endif
