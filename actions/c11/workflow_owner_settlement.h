#ifndef PRAKTOR_C11_WORKFLOW_OWNER_SETTLEMENT_H
#define PRAKTOR_C11_WORKFLOW_OWNER_SETTLEMENT_H

#include "workflow_parallel_process_guard.h"
#include "yml/workflow_dag_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Source-only Application Owner turn. No new scheduler, queue, Process engine,
 * persistence, installed ABI or production C++ cutover.
 *
 * All members are BORROWED. task_indices[] are existing DAG claim_ready()
 * reservations (in task_outputs[] order). Every accepted CFlow descriptor has
 * exactly one matching branch. The caller must not concurrently mutate DAG,
 * TaskRegistry, values, borrowed worker results or native retainers.
 *
 * branch_succeeded[] is a post-finalization semantic result, never merely
 * CFlow completed callback counts. cancelled_before_run[] is required for
 * ABORT_CANCELLED: true iff an accepted CFlow descriptor ran cancel() instead
 * of run(), so it has NO live worker Session and its DAG claim needs refuse().
 *
 * An in-progress cohort may call settle repeatedly from the single Owner,
 * with each turn bounded to max_steps per retainer. No callback can touch
 * native retainers after CLOSED+wait_idle. out_pending is the last observed
 * count or SIZE_MAX (unknown); never a false zero ACK.
 */
typedef struct praktor_c11_workflow_owner_cohort {
    praktor_workflow_dag_state *dag;
    praktor_workflow_task_registry *registry;
    const size_t *task_indices;
    const praktor_workflow_task_output_update *task_outputs; /* PUBLISH only */
    praktor_c11_value_txn *branch_values;
    const bool *branch_succeeded;
    const bool *cancelled_before_run; /* CANCELLED mode only */
    size_t branch_count;
    cflow_executor_control *control;
    praktor_c11_process_cohort_recovery *recovery;
    cflow_process_retainer *const *retainers;
} praktor_c11_workflow_owner_cohort;

typedef enum praktor_c11_workflow_owner_settle_mode {
    PRAKTOR_C11_WORKFLOW_OWNER_PUBLISH = 0,
    PRAKTOR_C11_WORKFLOW_OWNER_ABORT_FAILED,
    PRAKTOR_C11_WORKFLOW_OWNER_ABORT_CANCELLED
} praktor_c11_workflow_owner_settle_mode;

/* Close the SAME dedicated CFlow cohort used by settlement. CANCEL_PENDING
 * marks the DAG CANCELLED BEFORE allowing the worker to finish its cancel
 * callback. DRAIN leaves DAG ready state alone. No synchronous wait is made:
 * caller may release worker test gates or complete other Owner I/O afterward.
 * Returns PARALLEL_INVALID if CFlow shutdown cannot be admitted. */
int praktor_c11_workflow_owner_close_admission(
    praktor_workflow_dag_state *dag, cflow_executor_control *control,
    cflow_executor_shutdown_policy policy);

/* PUBLISH: preflight every DAG RESERVED index + nonterminal TaskRegistry slot
 * before any text/typed publication; use the existing CFlow/Process barrier
 * and the canonical atomic typed/text commit; after success mark each task
 * SUCCESS and finish its DAG reservation in one uninterrupted Owner turn.
 *
 * ABORT_FAILED: after all worker callbacks settle, first stop DAG FAILED,
 * continue bounded NativeIO recovery, then abort staged transactions and mark
 * each executed task FAILED. Do not replace already committed prior cohorts.
 *
 * ABORT_CANCELLED: require the DAG was stopped CANCELLED at admission close;
 * after CFlow callbacks/finalizers and Process recovery settle, abort staged
 * outputs, fail executed tasks and refuse queued/cancelled-before-run tasks.
 *
 * Both abort modes preserve first NativeIO error (RECOVERY), even when native
 * cleanup reaches pending=0. BUSY leaves staged output handles untouched.
 * The return status NEVER claims DAG transitions + typed/text root changes
 * are crash-atomic across cohorts. An impossible post-publication failure is
 * INVARIANT and permanently stops new DAG admission; no invented rollback.
 * The same sealed recovery gate can never be reused for another cohort.
 */
int praktor_c11_workflow_owner_settle_cohort(
    const praktor_c11_workflow_owner_cohort *cohort,
    praktor_c11_workflow_owner_settle_mode mode,
    size_t max_steps, size_t *out_pending);

#ifdef __cplusplus
}
#endif
#endif
