#include "tree_executor_session.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      return 1;                                                                \
    }                                                                          \
  } while (0)

namespace {

salts_bt_tree_config limits() {
  salts_bt_tree_config value{};
  salts_bt_tree_config_init(&value);
  value.max_nodes = 16u;
  value.max_depth = 8u;
  value.max_control_iterations = 32u;
  return value;
}

int testRegisteredLeafResumesRunningState() {
  actions::Tree tree;
  tree.root.id = "Sequence";
  actions::Node step;
  step.id = "Step";
  tree.root.children.push_back(step);

  actions::Executor executor;
  int invocations = 0;
  executor.registerTask("Step", [&invocations](const auto&, actions::Blackboard& bb) {
    ++invocations;
    bb.set("step_invocations", std::to_string(invocations));
    return invocations == 1 ? actions::NodeStatus::RUNNING
                             : actions::NodeStatus::SUCCESS;
  });

  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard, context,
                                            limits());
  CHECK(session.validate() == SALTS_OK);
  CHECK(session.plan().nodes.size() == 2u);
  CHECK(session.sourceNode(1u) != nullptr);
  CHECK(session.sourceNode(1u)->id == "Step");

  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(invocations == 1);

  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(invocations == 2);
  CHECK(blackboard.get("step_invocations") == "2");
  return 0;
}

struct HostLeafState {
  int tick_calls = 0;
  int halt_calls = 0;
  int legacy_calls = 0;
};

int customShellTick(void* user, size_t node_index,
                    salts_bt_node_status* out_status) {
  auto* state = static_cast<HostLeafState*>(user);
  if (state == nullptr || out_status == nullptr || node_index != 0u) {
    return SALTS_EINVAL;
  }
  ++state->tick_calls;
  *out_status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}

void customShellHalt(void* user, size_t node_index) {
  auto* state = static_cast<HostLeafState*>(user);
  if (state != nullptr && node_index == 0u) ++state->halt_calls;
}

int testShellRequiresExplicitManagedHost() {
  actions::Tree tree;
  tree.root.id = "Shell";
  actions::Executor executor;
  int legacy_shell_calls = 0;
  executor.registerTask("Shell", [&legacy_shell_calls](const auto&, actions::Blackboard&) {
    ++legacy_shell_calls;
    return actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard blackboard;
  actions::ExecutionContext context;

  bool rejected = false;
  try {
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());
    (void)session;
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  CHECK(rejected);
  CHECK(legacy_shell_calls == 0);

  HostLeafState state;
  praktor_c11_bt_host hooks{};
  hooks.tick_leaf = &customShellTick;
  hooks.halt_leaf = &customShellHalt;
  hooks.user = &state;
  {
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits(), &hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(state.tick_calls == 1);
    CHECK(legacy_shell_calls == 0);
  }
  return 0;
}


int testScalarConditionAndSwitchUseBlackboard() {
  {
    actions::Tree tree;
    tree.root.id = "IfThenElse";
    tree.root.params.emplace("condition", actions::Value{true});
    actions::Node then_node;
    then_node.id = "Then";
    actions::Node else_node;
    else_node.id = "Else";
    tree.root.children.push_back(then_node);
    tree.root.children.push_back(else_node);

    actions::Executor executor;
    int then_calls = 0;
    int else_calls = 0;
    executor.registerTask("Then", [&then_calls](const auto&, actions::Blackboard&) {
      ++then_calls;
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("Else", [&else_calls](const auto&, actions::Blackboard&) {
      ++else_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(then_calls == 1);
    CHECK(else_calls == 0);
  }

  {
    actions::Tree tree;
    tree.root.id = "Switch";
    tree.root.params.emplace("variable", actions::Value{std::string("{choice}")});
    actions::Node first;
    first.id = "First";
    actions::Node second;
    second.id = "Second";
    tree.root.children.push_back(first);
    tree.root.children.push_back(second);

    actions::Executor executor;
    int first_calls = 0;
    int second_calls = 0;
    executor.registerTask("First", [&first_calls](const auto&, actions::Blackboard&) {
      ++first_calls;
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("Second", [&second_calls](const auto&, actions::Blackboard&) {
      ++second_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    blackboard.set("choice", "1.0");
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(first_calls == 0);
    CHECK(second_calls == 1);
  }
  return 0;
}

int throwingParallelEnter(void*, size_t, size_t, bool) {
  throw std::runtime_error("test parallel enter exception");
}

int noOpParallelStep(void*, size_t, size_t, salts_bt_node_status) {
  return SALTS_OK;
}

int noOpParallelJoin(void*, size_t, salts_bt_node_status) {
  return SALTS_OK;
}

int noOpParallelFinish(void*, size_t, size_t, salts_bt_node_status) {
  return SALTS_OK;
}

int testHostExceptionsStayInsideCAbi() {
  actions::Tree tree;
  tree.root.id = "Parallel";
  actions::Node first;
  first.id = "First";
  actions::Node second;
  second.id = "Second";
  tree.root.children.push_back(first);
  tree.root.children.push_back(second);

  actions::Executor executor;
  executor.registerTask("First", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::SUCCESS;
  });
  executor.registerTask("Second", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::SUCCESS;
  });

  praktor_c11_bt_host hooks{};
  hooks.parallel_enter = &throwingParallelEnter;
  hooks.parallel_step = &noOpParallelStep;
  hooks.parallel_join = &noOpParallelJoin;
  hooks.parallel_finish = &noOpParallelFinish;
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits(), &hooks);

  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) != SALTS_OK);
  CHECK(session.lastError().find("Parallel enter hook failed") != std::string::npos);
  return 0;
}


} // namespace

int main() {
  if (testRegisteredLeafResumesRunningState() != 0) return 1;
  if (testShellRequiresExplicitManagedHost() != 0) return 1;
  if (testScalarConditionAndSwitchUseBlackboard() != 0) return 1;
  if (testHostExceptionsStayInsideCAbi() != 0) return 1;
  return 0;
}
