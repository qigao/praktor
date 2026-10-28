#ifndef PRAKTOR_WORKFLOW_DEPENDENCIES_H
#define PRAKTOR_WORKFLOW_DEPENDENCIES_H
#include <json_parser.h>
#include <salts/error_codes.h>
#include <vstr.h>
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Qualification-only task dependency projection, not an executor/scheduler.
 * Input elements are existing C11 YAML task-metadata JSON objects in task
 * declaration order. No YAML document, metadata JSON or runtime owner is
 * retained. Output uniquely owns copied names and ordered directed edges.
 *
 * Validation order deliberately matches TaskParser::buildGraph:
 * duplicate task names -> unknown static trigger targets -> unknown
 * depends_on targets -> cyclic dependencies. Empty/dynamic trigger targets
 * are not static graph edges. Duplicate dependencies remain duplicate edges.
 * No shared mutable state; caller destroys a graph only after its readers
 * have stopped. The output pointer is unchanged on any failure.
 *
 * Bounded admission: <=4096 task names, <=16384 explicit dependency edges,
 * <=1MiB copied name bytes including NULs. These are qualification bounds;
 * this graph is not a replacement for the production DAG or atomic Parallel.
 * Returns SALTS_OK, SALTS_EINVAL, SALTS_ENOMEM. On semantic failure the
 * optional diagnostic uses the same unprefixed message as buildGraph(). */
#define PRAKTOR_WORKFLOW_MAX_TASKS 4096u
#define PRAKTOR_WORKFLOW_MAX_EDGES 16384u
#define PRAKTOR_WORKFLOW_MAX_NAME_BYTES (1024u * 1024u)
typedef struct praktor_workflow_dependencies praktor_workflow_dependencies;

int praktor_workflow_dependencies_create(
    const json_value_t *const *task_metadata, size_t count,
    praktor_workflow_dependencies **out, char *error, size_t error_capacity);
void praktor_workflow_dependencies_destroy(praktor_workflow_dependencies *graph);
size_t praktor_workflow_dependencies_task_count(const praktor_workflow_dependencies *graph);
size_t praktor_workflow_dependencies_edge_count(const praktor_workflow_dependencies *graph);
int praktor_workflow_dependencies_task_name(const praktor_workflow_dependencies *graph,
    size_t index, vstr *out);
int praktor_workflow_dependencies_edge_at(const praktor_workflow_dependencies *graph,
    size_t index, size_t *predecessor, size_t *dependent);
int praktor_workflow_dependencies_has_edge(const praktor_workflow_dependencies *graph,
    size_t predecessor, size_t dependent, bool *out);

#ifdef __cplusplus
}
#endif
#endif
