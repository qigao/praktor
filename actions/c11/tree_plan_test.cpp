#include "tree_plan.hpp"

#include <cassert>
#include <stdexcept>
#include <string>

using actions::Blackboard;
using actions::Node;
using actions::Tree;
using actions::Value;
using actions::c11::TreePlan;
using actions::c11::compileTreePlan;

namespace {

salts_bt_tree_config testLimits() {
  salts_bt_tree_config limits{};
  salts_bt_tree_config_init(&limits);
  limits.max_nodes = 16u;
  limits.max_depth = 8u;
  limits.max_control_iterations = 32u;
  return limits;
}

void testParentFirstSequence() {
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
  assert(plan.nodes.size() == 3u);
  assert(plan.source_nodes.size() == 3u);
  assert(plan.nodes[0].parent_index == PRAKTOR_C11_BT_NO_PARENT);
  assert(plan.nodes[0].kind == SALTS_BT_NODE_SEQUENCE);
  assert(plan.nodes[1].parent_index == 0u);
  assert(plan.nodes[2].parent_index == 0u);
  assert(plan.source_nodes[1] == &tree.root.children[0]);
  assert(plan.source_nodes[2] == &tree.root.children[1]);
}

void testDurationAndRepeatParameters() {
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
  assert(plan.nodes.size() == 3u);
  assert(plan.nodes[1].kind == SALTS_BT_NODE_REPEAT);
  assert(plan.nodes[1].limit == 3u);
  assert(plan.nodes[2].kind == SALTS_BT_NODE_DELAY);
  assert(plan.nodes[2].duration_ns == UINT64_C(12000000));
}

void testRejectsInvalidInputs() {
  Blackboard blackboard;
  {
    Tree tree;
    tree.root.id = "Repeat";
    tree.root.params.emplace("num_cycles", Value{-1.0});
    bool rejected = false;
    try { (void)compileTreePlan(tree, blackboard, testLimits()); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
  }
  {
    Tree tree;
    tree.root.id = "SubTree";
    bool rejected = false;
    try { (void)compileTreePlan(tree, blackboard, testLimits()); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
  }
  {
    Tree tree;
    tree.root.id = "UnknownControl";
    Node child;
    child.id = "Task";
    tree.root.children.push_back(child);
    bool rejected = false;
    try { (void)compileTreePlan(tree, blackboard, testLimits()); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
  }
}

} // namespace

int main() {
  testParentFirstSequence();
  testDurationAndRepeatParameters();
  testRejectsInvalidInputs();
  return 0;
}
