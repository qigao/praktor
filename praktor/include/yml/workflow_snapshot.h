#ifndef PRAKTOR_WORKFLOW_SNAPSHOT_H
#define PRAKTOR_WORKFLOW_SNAPSHOT_H

#include "yml/workflow_dependencies.h"
#include "yml/orch_tree.h"
#include <json_parser.h>
#include <stddef.h>
#include <vstr.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Qualification-only in-memory C11 workflow aggregate, not the reviewed
 * WorkflowPlan or an executor. Composes the existing canonical C11 workflow
 * metadata, task metadata, command desugaring, named/flat Orch AST and graph.
 *
 * Admission currently requires every task to be a Command or Orchestration
 * runner; unsupported runner and script forms are explicitly rejected, never
 * admitted with an absent execution body. No includes, file IO, script engine,
 * executable workflow plan identity, source provenance, or scheduler.
 *
 * Unique owner. Copies/owns all task metadata JSON and Orchestration AST nodes.
 * The embedded graph copies task names/edges; borrowed input YAML and all
 * temporary action parameter owners are destroyed before publication. Views
 * remain valid until destroy, including after caller input is freed.
 * Concurrent read-only access is allowed; destruction must be serialized.
 *
 * Failure does not alter *out. Capacity: at most 4096 tasks, 16384 dependency
 * edges, plus the limits of each canonical C metadata/AST constructor.
 * Return SALTS_OK, SALTS_EINVAL for input/unsupported/capacity, SALTS_ENOMEM
 * for allocation. Diagnostics use a borrowed optional buffer, NUL terminated
 * when capacity>0; NULL error requires zero error_capacity.
 */
typedef struct praktor_workflow_snapshot praktor_workflow_snapshot;
typedef enum praktor_workflow_snapshot_task_kind {
    PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND,
    PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION
} praktor_workflow_snapshot_task_kind;

int praktor_workflow_snapshot_create_inline(vstr yaml_source,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity);
void praktor_workflow_snapshot_destroy(praktor_workflow_snapshot *snapshot);

size_t praktor_workflow_snapshot_task_count(const praktor_workflow_snapshot *snapshot);
const json_value_t *praktor_workflow_snapshot_metadata(
    const praktor_workflow_snapshot *snapshot);
const json_value_t *praktor_workflow_snapshot_task_metadata(
    const praktor_workflow_snapshot *snapshot,size_t index);
const praktor_orch_tree *praktor_workflow_snapshot_task_tree(
    const praktor_workflow_snapshot *snapshot,size_t index);
int praktor_workflow_snapshot_task_kind_at(
    const praktor_workflow_snapshot *snapshot,size_t index,
    praktor_workflow_snapshot_task_kind *out);
const praktor_workflow_dependencies *praktor_workflow_snapshot_dependencies(
    const praktor_workflow_snapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
