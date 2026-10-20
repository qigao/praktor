#ifndef PRAKTOR_ACTIONS_C11_TREE_PLAN_HPP
#define PRAKTOR_ACTIONS_C11_TREE_PLAN_HPP

#include "actions/core/ast.hpp"
#include "actions/core/executor.hpp"
#include "tree_adapter.h"

#include <vector>

namespace actions::c11 {

/*
 * A flattened, parent-first view of an actions::Tree suitable for
 * praktor_c11_bt_tree_init(). The source_nodes pointers are borrowed from the
 * original tree; keep that tree alive and unchanged until the adapter is
 * destroyed.
 *
 * This compiler only describes topology and typed node configuration. It does
 * not execute callbacks, parse YAML, expand SubTree references, or decide
 * Blackboard/Process ownership. SubTree is rejected until the Praktor plan
 * compiler can expand named trees into independent node instances.
 */
struct TreePlan {
  std::vector<praktor_c11_bt_node_desc> nodes;
  std::vector<const actions::Node *> source_nodes;
};

/*
 * Compile and preflight an already converted action AST without running leaves.
 * Throws std::invalid_argument for unsupported/malformed node definitions and
 * std::length_error when the supplied Salts tree limits are exceeded.
 */
TreePlan compileTreePlan(const actions::Tree &tree,
                         const actions::Blackboard &blackboard,
                         const salts_bt_tree_config &limits);

} // namespace actions::c11

#endif /* PRAKTOR_ACTIONS_C11_TREE_PLAN_HPP */
