#ifndef PRAKTOR_WORKFLOW_TASK_REGISTRY_H
#define PRAKTOR_WORKFLOW_TASK_REGISTRY_H

#include "yml/workflow_snapshot.h"
#include <json_parser.h>
#include <salts/error_codes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vstr.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Qualification-only typed task outputs and status owner.
 * This does not replace the production C++ WorkflowContext/TaskRegistry,
 * the shared execution context, or the Scheduler. Create copies all task
 * names from the C-owned snapshot; the result survives snapshot destruction.
 * One owner thread must serialize all access. No callback, global cache,
 * dynamic task creation or C++ runtime is retained. */
typedef struct praktor_workflow_task_registry praktor_workflow_task_registry;
typedef enum praktor_workflow_task_state {
    PRAKTOR_WORKFLOW_TASK_PENDING = 0,
    PRAKTOR_WORKFLOW_TASK_RUNNING,
    PRAKTOR_WORKFLOW_TASK_SUCCESS,
    PRAKTOR_WORKFLOW_TASK_FAILED,
    PRAKTOR_WORKFLOW_TASK_SKIPPED
} praktor_workflow_task_state;

#define PRAKTOR_WORKFLOW_TASK_MAX_OUTPUTS 128u
#define PRAKTOR_WORKFLOW_TASK_MAX_JSON_NODES 4096u
#define PRAKTOR_WORKFLOW_TASK_MAX_JSON_DEPTH 64u
#define PRAKTOR_WORKFLOW_TASK_MAX_JSON_BYTES (1024u * 1024u)
#define PRAKTOR_WORKFLOW_TASK_MAX_REASON_BYTES 65536u

/* Strong output ownership: on create failure caller's *out is unchanged. */
int praktor_workflow_task_registry_create(
    const praktor_workflow_snapshot *snapshot,
    praktor_workflow_task_registry **out);
void praktor_workflow_task_registry_destroy(
    praktor_workflow_task_registry *owner);

size_t praktor_workflow_task_registry_count(
    const praktor_workflow_task_registry *owner);
int praktor_workflow_task_registry_name(
    const praktor_workflow_task_registry *owner,size_t index,vstr *out);
int praktor_workflow_task_registry_state(
    const praktor_workflow_task_registry *owner,size_t index,
    praktor_workflow_task_state *out);

/* Start/re-enter clears old outputs and failure details. This is the explicit
 * re-entry used for triggers and each. No old output view may remain in use.
 * Complete/fail/skip may follow Pending or Running; finalized states reject
 * further writes/finalization until explicitly started again. */
int praktor_workflow_task_registry_start(
    praktor_workflow_task_registry *owner,size_t index);
int praktor_workflow_task_registry_complete(
    praktor_workflow_task_registry *owner,size_t index);
int praktor_workflow_task_registry_fail(
    praktor_workflow_task_registry *owner,size_t index,vstr reason);
int praktor_workflow_task_registry_skip(
    praktor_workflow_task_registry *owner,size_t index);

/* Deep-copy a typed JSON subtree and stage the complete output object before
 * publishing. Pending auto-starts only if publication succeeds. Existing output
 * values are replaceable while Running; finalized values are immutable.
 * Entire aggregate must satisfy bounded node/depth/text budgets. */
int praktor_workflow_task_registry_set_output(
    praktor_workflow_task_registry *owner,size_t index,
    vstr key,const json_value_t *value);

/* Atomic multi-output publication on one task. The source must be a JSON
 * object with nonempty NUL-free keys, and is borrowed only during this call.
 * Clones the entire candidate aggregate and admits it against the same
 * task-level JSON budgets before one swap; invalid/oversize/OOM leaves
 * previously published outputs and status untouched. This is NOT a Parallel
 * branch group commit or a task-scheduler transition. */
int praktor_workflow_task_registry_merge_outputs(
    praktor_workflow_task_registry *owner,size_t index,
    const json_value_t *updates);

/* Borrowed immutable views; valid until start/re-entry/destroy. Get output
 * succeeds only for completed/failed tasks; no mutable/working JSON exposure.
 * Failure leaves outputs unchanged. ENOENT = absent key, EBUSY = not terminal.
 * SKIPPED remains non-readable unless a new run produces final outputs. */
int praktor_workflow_task_registry_get_output(
    const praktor_workflow_task_registry *owner,size_t index,
    vstr key,const json_value_t **out);
int praktor_workflow_task_registry_get_outputs(
    const praktor_workflow_task_registry *owner,size_t index,
    const json_value_t **out);
int praktor_workflow_task_registry_failure_reason(
    const praktor_workflow_task_registry *owner,size_t index,vstr *out);

#ifdef __cplusplus
}
#endif
#endif
