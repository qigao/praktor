#include "tree_plan.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

using actions::Blackboard;
using actions::Node;
using actions::Tree;
using actions::Value;
using actions::c11::compileTreePlan;

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      return 1;                                                                \
    }                                                                          \
  } while (0)

namespace {

salts_bt_tree_config testLimits() {
  salts_bt_tree_config limits{};
  salts_bt_tree_config_init(&limits);
  limits.max_nodes = 16u;
  limits.max_depth = 8u;
  limits.max_control_iterations = 32u;
  return limits;
}

int testParentFirstSequence() {
  Tree tree;
  tree.root.id = "Sequence";
  Node first;
  first.id = "WriteValue";
  Node second;
  second.id = "CheckValue";
  tree.root.children.push_back(first);
  tree.root.children.push_back(second);

  Blackboard blackboard;
  const auto plan = compileTreePlan(tree, blackboard, testLimits());
  CHECK(plan.nodes.size() == 3u);
  CHECK(plan.source_nodes.size() == 3u);
  CHECK(plan.nodes[0].parent_index == PRAKTOR_C11_BT_NO_PARENT);
  CHECK(plan.nodes[0].kind == SALTS_BT_NODE_SEQUENCE);
  CHECK(plan.nodes[1].parent_index == 0u);
  CHECK(plan.nodes[2].parent_index == 0u);
  CHECK(plan.source_nodes[1] == &tree.root.children[0]);
  CHECK(plan.source_nodes[2] == &tree.root.children[1]);
  return 0;
}

int testDurationAndRepeatParameters() {
  Tree tree;
  tree.root.id = "Sequence";

  Node repeat;
  repeat.id = "Repeat";
  repeat.params.emplace("num_cycles", Value{3.0});
  Node delay;
  delay.id = "Delay";
  delay.params.emplace("delay_ms", Value{12.0});
  repeat.children.push_back(delay);
  tree.root.children.push_back(repeat);

  Blackboard blackboard;
  const auto plan = compileTreePlan(tree, blackboard, testLimits());
  CHECK(plan.nodes.size() == 3u);
  CHECK(plan.nodes[1].kind == SALTS_BT_NODE_REPEAT);
  CHECK(plan.nodes[1].limit == 3u);
  CHECK(plan.nodes[2].kind == SALTS_BT_NODE_DELAY);
  CHECK(plan.nodes[2].duration_ns == UINT64_C(12000000));
  return 0;
}

int testZeroBudgetWhileDoLowersToNoOpLeaf() {
  Tree tree;
  tree.root.id = "WhileDo";
  tree.root.params.emplace("condition", Value{true});
  tree.root.params.emplace("max_iterations", Value{0.0});
  Node body;
  body.id = "Body";
  tree.root.children.push_back(body);

  Blackboard blackboard;
  const auto plan = compileTreePlan(tree, blackboard, testLimits());
  CHECK(plan.nodes.size() == 1u);
  CHECK(plan.source_nodes.size() == 1u);
  CHECK(plan.terminal_actions.size() == 1u);
  CHECK(plan.terminal_actions[0] == actions::c11::TreePlanTerminalAction::Success);
  CHECK(plan.nodes[0].kind == SALTS_BT_NODE_LEAF);
  CHECK(plan.nodes[0].parent_index == PRAKTOR_C11_BT_NO_PARENT);
  return 0;
}

int testZeroBudgetKeepRunningLowersToNoOpFailureLeaf() {
  Tree tree;
  tree.root.id = "KeepRunningUntilFailure";
  tree.root.params.emplace("max_iterations", Value{0.0});
  Node body;
  body.id = "Body";
  tree.root.children.push_back(body);

  Blackboard blackboard;
  const auto plan = compileTreePlan(tree, blackboard, testLimits());
  CHECK(plan.nodes.size() == 1u);
  CHECK(plan.source_nodes.size() == 1u);
  CHECK(plan.terminal_actions.size() == 1u);
  CHECK(plan.terminal_actions[0] == actions::c11::TreePlanTerminalAction::Failure);
  CHECK(plan.nodes[0].kind == SALTS_BT_NODE_LEAF);
  return 0;
}

int testBlackboardIntegralFloatParameters() {
  Tree tree;
  tree.root.id = "Sequence";

  Node repeat;
  repeat.id = "Repeat";
  repeat.params.emplace("num_cycles", Value{std::string("{cycles}")});
  Node delay;
  delay.id = "Delay";
  delay.params.emplace("delay_ms", Value{std::string("{delay}")});
  repeat.children.push_back(delay);
  tree.root.children.push_back(repeat);

  Blackboard blackboard;
  blackboard.set("cycles", "3.0");
  blackboard.set("delay", "12.0");
  const auto plan = compileTreePlan(tree, blackboard, testLimits());
  CHECK(plan.nodes[1].limit == 3u);
  CHECK(plan.nodes[2].duration_ns == UINT64_C(12000000));
  return 0;
}

int expectInvalid(Tree& tree, Blackboard& blackboard) {
  try {
    (void)compileTreePlan(tree, blackboard, testLimits());
  } catch (const std::invalid_argument&) {
    return 0;
  } catch (const std::length_error&) {
    return 0;
  }
  std::fprintf(stderr, "expected invalid tree plan to be rejected\n");
  return 1;
}

int testRejectsInvalidInputs() {
  Blackboard blackboard;
  {
    Tree tree;
    tree.root.id = "Repeat";
    tree.root.params.emplace("num_cycles", Value{-1.0});
    CHECK(expectInvalid(tree, blackboard) == 0);
  }
  {
    Tree tree;
    tree.root.id = "Repeat";
    tree.root.params.emplace("num_cycles", Value{1.5});
    CHECK(expectInvalid(tree, blackboard) == 0);
  }
  {
    Tree tree;
    tree.root.id = "Repeat";
    tree.root.params.emplace("num_cycles", Value{2147483648.0});
    CHECK(expectInvalid(tree, blackboard) == 0);
  }
  {
    Tree tree;
    tree.root.id = "SubTree";
    CHECK(expectInvalid(tree, blackboard) == 0);
  }
  {
    Tree tree;
    tree.root.id = "UnknownControl";
    Node child;
    child.id = "Task";
    tree.root.children.push_back(child);
    CHECK(expectInvalid(tree, blackboard) == 0);
  }
  return 0;
}

} // namespace

int main() {
  if (testParentFirstSequence() != 0) return 1;
  if (testDurationAndRepeatParameters() != 0) return 1;
  if (testZeroBudgetWhileDoLowersToNoOpLeaf() != 0) return 1;
  if (testZeroBudgetKeepRunningLowersToNoOpFailureLeaf() != 0) return 1;
  if (testBlackboardIntegralFloatParameters() != 0) return 1;
  if (testRejectsInvalidInputs() != 0) return 1;
  return 0;
}
