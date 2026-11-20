#ifndef PRAKTOR_WORKFLOW_SNAPSHOT_H
#define PRAKTOR_WORKFLOW_SNAPSHOT_H

#include "yml/workflow_dependencies.h"
#include "yml/orch_tree.h"
#include "yml/yaml_document.h"
#include <json_parser.h>
#include <stddef.h>
#include <core/vstr.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Immutable in-memory C11 workflow aggregate. Composes canonical C11 workflow
 * metadata, task metadata, command desugaring, named/flat Orch AST and graph.
 *
 * Each materialized task has a Command, Tool or Orchestration runner. Service
 * file references stay immutable metadata; execution loads their declarations.
 *
 * Unique owner. Copies/owns all task metadata JSON and Orchestration AST nodes.
 * The embedded graph copies task names/edges. When a task declares a
 * Statechart, the snapshot owns the parsed YAML behind its declaration. Views
 * remain valid until destroy, including after caller input is freed.
 * Concurrent read-only access is allowed; destruction must be serialized.
 *
 * Failure does not alter *out. Capacity: at most 4096 tasks, 16384 dependency
 * edges, plus the limits of each canonical C metadata/AST constructor.
 * Return SALTS_OK, SALTS_EINVAL for invalid input/capacity, SALTS_ENOTSUP for
 * unsupported workflow fields or SALTS_ENOMEM for allocation. Diagnostics use
 * a borrowed optional buffer, NUL terminated when capacity>0; NULL error
 * requires zero error_capacity.
 */
typedef struct praktor_workflow_snapshot praktor_workflow_snapshot;
typedef enum praktor_workflow_snapshot_task_kind {
    PRAKTOR_WORKFLOW_SNAPSHOT_COMMAND,
    PRAKTOR_WORKFLOW_SNAPSHOT_ORCHESTRATION
} praktor_workflow_snapshot_task_kind;

int praktor_workflow_snapshot_create_inline(vstr yaml_source,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity);
/* Execution admission reads local includes from the working directory and
 * expands dynamic task arrays from metadata and the inherited environment.
 * Limit includes generated tasks and their join tasks. Total YAML <= 1 MiB.
 * The inline entry point only accepts self-contained task declarations. */
int praktor_workflow_snapshot_create_for_run(vstr yaml_source,size_t limit,
    praktor_workflow_snapshot **out,char *error,size_t error_capacity);
void praktor_workflow_snapshot_destroy(praktor_workflow_snapshot *snapshot);

size_t praktor_workflow_snapshot_task_count(const praktor_workflow_snapshot *snapshot);
const json_value_t *praktor_workflow_snapshot_metadata(
    const praktor_workflow_snapshot *snapshot);
const json_value_t *praktor_workflow_snapshot_task_metadata(
    const praktor_workflow_snapshot *snapshot,size_t index);
const praktor_orch_tree *praktor_workflow_snapshot_task_tree(
    const praktor_workflow_snapshot *snapshot,size_t index);
praktor_yaml_node praktor_workflow_snapshot_task_statechart(
    const praktor_workflow_snapshot *snapshot,size_t index);
praktor_yaml_node praktor_workflow_snapshot_statechart(
    const praktor_workflow_snapshot *snapshot);
int praktor_workflow_snapshot_task_kind_at(
    const praktor_workflow_snapshot *snapshot,size_t index,
    praktor_workflow_snapshot_task_kind *out);
const praktor_workflow_dependencies *praktor_workflow_snapshot_dependencies(
    const praktor_workflow_snapshot *snapshot);

#ifdef __cplusplus
}
#endif
#endif
