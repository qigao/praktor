#ifndef PRAKTOR_C11_ORCH_SESSION_PLAN_H
#define PRAKTOR_C11_ORCH_SESSION_PLAN_H

#include "session.h"
#include "yml/orch_tree.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Qualification-only, C11-owned bridge from the canonical immutable Orch
 * AST to the already-existing C11 Session compiler. No second YAML parser,
 * scheduler, Process owner, C++ runtime or fallback.
 *
 * Initial admitted subset: Sequence, Fallback/Selector, Inverter, ForceSuccess,
 * ForceFailure, SetVariable, CheckExitCode, ParseKeyValue, ParseRegex,
 * ParseLines, Sleep. Shell/Parallel/SubTree, dynamic scheduler nodes and
 * all other unqualified operations explicitly return SALTS_ENOTSUP.
 *
 * All parameter text and sizes are copied; the result borrows neither the
 * Orch owner nor the input YAML. Input may be destroyed after create().
 * Session init deep-copies descriptors; plan may then be destroyed before
 * the first tick. Its tree shape remains the already-qualified preorder.
 *
 * Strong failure: caller's *out remains unchanged. One owner controls create,
 * Session init and destruction. Retain bounds from the canonical Orch owner
 * (4098 nodes, 8 parameters/node, 1 MiB aggregate parameter values) plus
 * salts_bt_tree_config max_nodes/max_depth. No recursive traversal.
 *
 * Returns SALTS_OK, SALTS_EINVAL, SALTS_ENOTSUP or SALTS_ENOMEM.
 */
typedef struct praktor_c11_orch_session_plan praktor_c11_orch_session_plan;

int praktor_c11_orch_session_plan_create(
    const praktor_orch_tree *tree, const salts_bt_tree_config *limits,
    praktor_c11_orch_session_plan **out,
    char *error, size_t error_capacity);
void praktor_c11_orch_session_plan_destroy(
    praktor_c11_orch_session_plan *plan);
size_t praktor_c11_orch_session_plan_node_count(
    const praktor_c11_orch_session_plan *plan);

/* Does not consume the plan. Session is caller-owned and must be initially
 * empty. The C11 Session clones descriptors/typed parameters on success.
 * No implicit Host selection, Process reservation or execution occurs here. */
int praktor_c11_orch_session_plan_init_session(
    const praktor_c11_orch_session_plan *plan,
    const praktor_c11_session_config *config,
    praktor_c11_session *session);

#ifdef __cplusplus
}
#endif
#endif
