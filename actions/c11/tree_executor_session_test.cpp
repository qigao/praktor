#include "tree_executor_session.hpp"

#include <cstdio>
#include <chrono>
#include <stdexcept>
#include <thread>
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

void noOpHalt(void*, size_t) {}

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
  praktor_c11_bt_host hooks{};
  hooks.halt_leaf = &noOpHalt;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard, context,
                                            limits(), &hooks);
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


int testExecutorOwnedRunningLeafDoesNotReenterDeclinedHook() {
  actions::Tree tree;
  tree.root.id = "Sequence";
  actions::Node work;
  work.id = "Work";
  tree.root.children.push_back(work);

  actions::Executor executor;
  int executor_calls = 0;
  executor.registerTask("Work", [&executor_calls](const auto&, actions::Blackboard&) {
    ++executor_calls;
    return executor_calls < 2 ? actions::NodeStatus::RUNNING
                              : actions::NodeStatus::SUCCESS;
  });

  int dispatcher_calls = 0;
  praktor_c11_bt_host hooks{};
  hooks.tick_leaf = [](void* user, size_t, salts_bt_node_status*) -> int {
    ++*static_cast<int*>(user);
    return SALTS_ENOTSUP;
  };
  hooks.halt_leaf = &noOpHalt;
  hooks.user = &dispatcher_calls;

  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits(), &hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(dispatcher_calls == 1);
  CHECK(executor_calls == 1);

  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(dispatcher_calls == 1);
  CHECK(executor_calls == 2);
  return 0;
}

int testRunningRegisteredLeafRequiresHaltHook() {
  actions::Tree tree;
  tree.root.id = "Work";
  actions::Executor executor;
  executor.registerTask("Work", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::RUNNING;
  });
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_ENOTSUP);
  CHECK(session.lastError().find("without a halt_leaf hook") != std::string::npos);
  return 0;
}

struct HaltHookState {
  int halt_calls = 0;
  size_t last_node = static_cast<size_t>(-1);
};

void countHalt(void* user, size_t node_index) {
  auto* state = static_cast<HaltHookState*>(user);
  if (state != nullptr) {
    ++state->halt_calls;
    state->last_node = node_index;
  }
}

int testShellCancelTerminatesManagedProcess() {
  actions::Tree tree;
  tree.root.id = "Shell";
#if defined(_WIN32)
  tree.root.params.emplace(
      "cmd", actions::Value{std::string(
                 "powershell.exe -NoProfile -Command Start-Sleep -Seconds 30")});
#else
  tree.root.params.emplace("cmd", actions::Value{std::string("sleep 30")});
#endif
  tree.root.params.emplace("timeout", actions::Value{30000.0});
  tree.root.params.emplace("stream_output", actions::Value{false});

  actions::Executor executor;
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());

  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);

  const auto started = std::chrono::steady_clock::now();
  CHECK(session.cancel() == SALTS_OK);
  CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
  CHECK(session.validate() == SALTS_OK);
  CHECK(session.lastError().empty());
  return 0;
}

int testCancelCallsHaltForRegisteredRunningLeaf() {
  actions::Tree tree;
  tree.root.id = "Sequence";
  actions::Node step;
  step.id = "ManagedTask";
  tree.root.children.push_back(step);

  actions::Executor executor;
  executor.registerTask("ManagedTask", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::RUNNING;
  });

  HaltHookState state;
  praktor_c11_bt_host hooks{};
  hooks.halt_leaf = &countHalt;
  hooks.user = &state;

  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits(), &hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(state.halt_calls == 0);
  CHECK(session.cancel() == SALTS_OK);
  CHECK(state.halt_calls == 1);
  CHECK(state.last_node == 1u);
  return 0;
}

int testShellUsesSaltsProcessByDefaultAndAllowsExplicitOverride() {
  {
    actions::Tree tree;
    tree.root.id = "Shell";
    tree.root.params.emplace("cmd", actions::Value{std::string("echo c11-process")});
    tree.root.params.emplace("timeout", actions::Value{5000.0});
    tree.root.params.emplace("stream_output", actions::Value{false});

    actions::Executor executor;
    int legacy_shell_calls = 0;
    executor.registerTask("Shell", [&legacy_shell_calls](const auto&, actions::Blackboard&) {
      ++legacy_shell_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());

    const auto started = std::chrono::steady_clock::now();
    salts_bt_node_status status = SALTS_BT_RUNNING;
    do {
      CHECK(session.tick(&status) == SALTS_OK);
      if (status == SALTS_BT_RUNNING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    } while (status == SALTS_BT_RUNNING);

    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(blackboard.has("shell_output"));
    CHECK(blackboard.get("shell_output").find("c11-process") != std::string::npos);
    CHECK(blackboard.get("shell_exit_code") == "0");
    CHECK(legacy_shell_calls == 0);
  }

  {
    actions::Tree tree;
    tree.root.id = "Shell";
    tree.root.params.emplace("cmd", actions::Value{std::string("exit 7")});
    tree.root.params.emplace("timeout", actions::Value{5000.0});
    tree.root.params.emplace("stream_output", actions::Value{false});

    actions::Executor executor;
    int legacy_shell_calls = 0;
    executor.registerTask("Shell", [&legacy_shell_calls](const auto&, actions::Blackboard&) {
      ++legacy_shell_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());

    const auto started = std::chrono::steady_clock::now();
    salts_bt_node_status status = SALTS_BT_RUNNING;
    do {
      CHECK(session.tick(&status) == SALTS_OK);
      if (status == SALTS_BT_RUNNING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    } while (status == SALTS_BT_RUNNING);

    CHECK(status == SALTS_BT_FAILURE);
    CHECK(blackboard.get("shell_exit_code") == "7");
    CHECK(blackboard.get("shell_error").empty());
    CHECK(session.lastError().find("unsuccessfully") != std::string::npos);
    CHECK(legacy_shell_calls == 0);
  }

  {
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
    HostLeafState state;
    praktor_c11_bt_host hooks{};
    hooks.tick_leaf = &customShellTick;
    hooks.halt_leaf = &customShellHalt;
    hooks.user = &state;

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


int testShellRejectsZeroTimeoutBeforeSpawningProcess() {
  actions::Tree tree;
  tree.root.id = "Shell";
  tree.root.params.emplace("cmd", actions::Value{std::string("echo must-not-run")});
  tree.root.params.emplace("timeout", actions::Value{0.0});
  tree.root.params.emplace("stream_output", actions::Value{false});

  actions::Executor executor;
  int legacy_shell_calls = 0;
  executor.registerTask("Shell", [&legacy_shell_calls](const auto&, actions::Blackboard&) {
    ++legacy_shell_calls;
    return actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_RUNNING;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_FAILURE);
  CHECK(session.lastError().find("timeout must be positive") != std::string::npos);
  CHECK(blackboard.get("shell_exit_code") == "-1");
  CHECK(legacy_shell_calls == 0);
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

int testInTreeIfThenElseConditionMatchesExecutor() {
  const auto makeTree = [] {
    actions::Tree tree;
    tree.root.id = "IfThenElse";
    actions::Node condition;
    condition.id = "Condition";
    actions::Node then_node;
    then_node.id = "Then";
    actions::Node else_node;
    else_node.id = "Else";
    tree.root.children.push_back(condition);
    tree.root.children.push_back(then_node);
    tree.root.children.push_back(else_node);
    return tree;
  };

  // SUCCESS condition selects child 1, and the C11 session must agree with
  // the legacy Executor's in-tree condition form.
  {
    actions::Tree tree = makeTree();
    actions::Executor legacy;
    int legacy_then = 0;
    int legacy_else = 0;
    legacy.registerTask("Condition", [](const auto&, actions::Blackboard&) {
      return actions::NodeStatus::SUCCESS;
    });
    legacy.registerTask("Then", [&legacy_then](const auto&, actions::Blackboard&) {
      ++legacy_then;
      return actions::NodeStatus::SUCCESS;
    });
    legacy.registerTask("Else", [&legacy_else](const auto&, actions::Blackboard&) {
      ++legacy_else;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(legacy_then == 1);
    CHECK(legacy_else == 0);

    actions::Executor c11;
    int c11_then = 0;
    int c11_else = 0;
    c11.registerTask("Condition", [](const auto&, actions::Blackboard&) {
      return actions::NodeStatus::SUCCESS;
    });
    c11.registerTask("Then", [&c11_then](const auto&, actions::Blackboard&) {
      ++c11_then;
      return actions::NodeStatus::SUCCESS;
    });
    c11.registerTask("Else", [&c11_else](const auto&, actions::Blackboard&) {
      ++c11_else;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(c11_then == legacy_then);
    CHECK(c11_else == legacy_else);
  }

  // FAILURE condition selects the optional else branch.
  {
    actions::Tree tree = makeTree();
    actions::Executor executor;
    int then_calls = 0;
    int else_calls = 0;
    executor.registerTask("Condition", [](const auto&, actions::Blackboard&) {
      return actions::NodeStatus::FAILURE;
    });
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
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(then_calls == 0);
    CHECK(else_calls == 1);
  }
  return 0;
}

int testWhileDoConditionFormsMatchTerminalEffects() {
  // Legacy WhileDo evaluates the in-tree condition repeatedly in one call.
  // The C11 runtime cooperatively yields after each successful body iteration;
  // its eventual result and observable body effects must match.
  {
    actions::Tree tree;
    tree.root.id = "WhileDo";
    tree.root.params.emplace("max_iterations", actions::Value{8.0});
    actions::Node condition;
    condition.id = "Condition";
    actions::Node body;
    body.id = "Body";
    tree.root.children.push_back(condition);
    tree.root.children.push_back(body);

    int legacy_condition_calls = 0;
    int legacy_body_calls = 0;
    actions::Executor legacy;
    legacy.registerTask("Condition", [&legacy_condition_calls](
                                      const auto&, actions::Blackboard&) {
      ++legacy_condition_calls;
      return legacy_condition_calls <= 2 ? actions::NodeStatus::SUCCESS
                                         : actions::NodeStatus::FAILURE;
    });
    legacy.registerTask("Body", [&legacy_body_calls](const auto&, actions::Blackboard&) {
      ++legacy_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(legacy_condition_calls == 3);
    CHECK(legacy_body_calls == 2);

    int c11_condition_calls = 0;
    int c11_body_calls = 0;
    actions::Executor c11;
    c11.registerTask("Condition", [&c11_condition_calls](
                                   const auto&, actions::Blackboard&) {
      ++c11_condition_calls;
      return c11_condition_calls <= 2 ? actions::NodeStatus::SUCCESS
                                      : actions::NodeStatus::FAILURE;
    });
    c11.registerTask("Body", [&c11_body_calls](const auto&, actions::Blackboard&) {
      ++c11_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(c11_condition_calls == legacy_condition_calls);
    CHECK(c11_body_calls == legacy_body_calls);
  }

  // A zero iteration budget is a no-op SUCCESS in the legacy Executor.
  // The C11 plan must preserve that result and must not evaluate condition/body.
  {
    actions::Tree tree;
    tree.root.id = "WhileDo";
    tree.root.params.emplace("condition", actions::Value{true});
    tree.root.params.emplace("max_iterations", actions::Value{0.0});
    actions::Node body;
    body.id = "Body";
    tree.root.children.push_back(body);

    actions::Executor legacy;
    int legacy_body_calls = 0;
    legacy.registerTask("Body", [&legacy_body_calls](const auto&, actions::Blackboard&) {
      ++legacy_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(legacy_body_calls == 0);

    actions::Executor c11;
    int c11_body_calls = 0;
    c11.registerTask("Body", [&c11_body_calls](const auto&, actions::Blackboard&) {
      ++c11_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(c11_body_calls == 0);
  }

  // Zero-budget KeepRunningUntilFailure returns FAILURE without ticking body.
  {
    actions::Tree tree;
    tree.root.id = "KeepRunningUntilFailure";
    tree.root.params.emplace("max_iterations", actions::Value{0.0});
    actions::Node body;
    body.id = "Body";
    tree.root.children.push_back(body);

    actions::Executor legacy;
    int legacy_body_calls = 0;
    legacy.registerTask("Body", [&legacy_body_calls](const auto&, actions::Blackboard&) {
      ++legacy_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::FAILURE);
    CHECK(legacy_body_calls == 0);

    actions::Executor c11;
    int c11_body_calls = 0;
    c11.registerTask("Body", [&c11_body_calls](const auto&, actions::Blackboard&) {
      ++c11_body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_FAILURE);
    CHECK(c11_body_calls == 0);
  }

  // Scalar condition form counts iterations against the configured budget.
  {
    actions::Tree tree;
    tree.root.id = "WhileDo";
    tree.root.params.emplace("condition", actions::Value{true});
    tree.root.params.emplace("max_iterations", actions::Value{2.0});
    actions::Node body;
    body.id = "Body";
    tree.root.children.push_back(body);

    actions::Executor executor;
    int body_calls = 0;
    executor.registerTask("Body", [&body_calls](const auto&, actions::Blackboard&) {
      ++body_calls;
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                              context, limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(body_calls == 2);
  }
  return 0;
}

int testReactiveFallbackRechecksPriorityAfterRunning() {
  // A lower-priority child returns RUNNING. On the next tick, a newly
  // successful high-priority child must preempt it instead of resuming it.
  actions::Tree tree;
  tree.root.id = "ReactiveFallback";
  actions::Node priority;
  priority.id = "Priority";
  actions::Node lower_priority;
  lower_priority.id = "LowerPriority";
  tree.root.children.push_back(priority);
  tree.root.children.push_back(lower_priority);

  actions::Executor legacy;
  int legacy_priority_calls = 0;
  int legacy_lower_calls = 0;
  legacy.registerTask("Priority", [&legacy_priority_calls](const auto&,
                                                            actions::Blackboard&) {
    ++legacy_priority_calls;
    return legacy_priority_calls == 1 ? actions::NodeStatus::FAILURE
                                      : actions::NodeStatus::SUCCESS;
  });
  legacy.registerTask("LowerPriority", [&legacy_lower_calls](const auto&,
                                                              actions::Blackboard&) {
    ++legacy_lower_calls;
    return legacy_lower_calls == 1 ? actions::NodeStatus::RUNNING
                                   : actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard legacy_blackboard;
  actions::ExecutionContext legacy_context;
  CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
        actions::NodeStatus::RUNNING);
  CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
        actions::NodeStatus::SUCCESS);
  CHECK(legacy_priority_calls == 2);
  CHECK(legacy_lower_calls == 1);

  actions::Executor c11;
  int c11_priority_calls = 0;
  int c11_lower_calls = 0;
  c11.registerTask("Priority", [&c11_priority_calls](const auto&,
                                                      actions::Blackboard&) {
    ++c11_priority_calls;
    return c11_priority_calls == 1 ? actions::NodeStatus::FAILURE
                                   : actions::NodeStatus::SUCCESS;
  });
  c11.registerTask("LowerPriority", [&c11_lower_calls](const auto&,
                                                        actions::Blackboard&) {
    ++c11_lower_calls;
    return c11_lower_calls == 1 ? actions::NodeStatus::RUNNING
                                : actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard c11_blackboard;
  actions::ExecutionContext c11_context;
  praktor_c11_bt_host hooks{};
  hooks.halt_leaf = &noOpHalt;
  actions::c11::TreeExecutorSession session(tree, c11, c11_blackboard,
                                            c11_context, limits(), &hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(c11_priority_calls == legacy_priority_calls);
  CHECK(c11_lower_calls == legacy_lower_calls);
  return 0;
}

int testEmptyReactiveFallbackMatchesFailure() {
  actions::Tree tree;
  tree.root.id = "ReactiveFallback";

  actions::Executor legacy;
  actions::Blackboard legacy_blackboard;
  actions::ExecutionContext legacy_context;
  CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
        actions::NodeStatus::FAILURE);

  actions::Executor c11;
  actions::Blackboard c11_blackboard;
  actions::ExecutionContext c11_context;
  actions::c11::TreeExecutorSession session(tree, c11, c11_blackboard,
                                            c11_context, limits());
  salts_bt_node_status status = SALTS_BT_SUCCESS;
  CHECK(session.validate() == SALTS_OK);
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_FAILURE);
  return 0;
}

int testEmptySwitchResolvesSelectorThenFails() {
  {
    actions::Tree tree;
    tree.root.id = "Switch";
    tree.root.params.emplace("variable", actions::Value{0.0});

    actions::Executor legacy;
    actions::Blackboard legacy_blackboard;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
          actions::NodeStatus::FAILURE);

    actions::Executor c11;
    actions::Blackboard c11_blackboard;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_blackboard,
                                              c11_context, limits());
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_FAILURE);
  }

  {
    actions::Tree tree;
    tree.root.id = "Switch";
    tree.root.params.emplace("variable",
                             actions::Value{std::string("{choice}")});

    actions::Executor legacy;
    actions::Blackboard legacy_blackboard;
    legacy_blackboard.set("choice", "0.0");
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
          actions::NodeStatus::FAILURE);

    actions::Executor c11;
    actions::Blackboard c11_blackboard;
    c11_blackboard.set("choice", "0.0");
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_blackboard,
                                              c11_context, limits());
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_FAILURE);
  }

  {
    actions::Tree tree;
    tree.root.id = "Switch";
    tree.root.params.emplace("variable",
                             actions::Value{std::string("{missing}")});

    actions::Executor legacy;
    actions::Blackboard legacy_blackboard;
    actions::ExecutionContext legacy_context;
    bool legacy_threw = false;
    try {
      (void)legacy.execute(tree.root, legacy_blackboard, legacy_context);
    } catch (const std::exception&) {
      legacy_threw = true;
    }
    CHECK(legacy_threw);

    actions::Executor c11;
    actions::Blackboard c11_blackboard;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_blackboard,
                                              c11_context, limits());
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) != SALTS_OK);
    CHECK(session.lastError().find("empty Switch selector failed") !=
          std::string::npos);
  }
  return 0;
}

int testEmptyParallelAndReactiveSequenceMatchExecutor() {
  const char* const node_kinds[] = {"Parallel", "ReactiveSequence"};
  for (const char* node_kind : node_kinds) {
    actions::Tree tree;
    tree.root.id = node_kind;

    actions::Executor legacy;
    actions::Blackboard legacy_blackboard;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_blackboard, legacy_context) ==
          actions::NodeStatus::SUCCESS);

    actions::Executor c11;
    actions::Blackboard c11_blackboard;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(
        tree, c11, c11_blackboard, c11_context, limits());
    CHECK(session.validate() == SALTS_OK);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
  }
  return 0;
}

int testRunOnceAndKeepRunningStateMatchExecutor() {
  {
    actions::Tree tree;
    tree.root.id = "RunOnce";
    actions::Node child;
    child.id = "CachedFailure";
    tree.root.children.push_back(child);

    actions::Executor legacy;
    int legacy_calls = 0;
    legacy.registerTask("CachedFailure", [&legacy_calls](const auto&,
                                                          actions::Blackboard&) {
      ++legacy_calls;
      return actions::NodeStatus::FAILURE;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::FAILURE);
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::FAILURE);
    CHECK(legacy_calls == 1);

    actions::Executor c11;
    int c11_calls = 0;
    c11.registerTask("CachedFailure", [&c11_calls](const auto&,
                                                    actions::Blackboard&) {
      ++c11_calls;
      return actions::NodeStatus::FAILURE;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_FAILURE);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_FAILURE);
    CHECK(c11_calls == legacy_calls);
  }

  {
    actions::Tree tree;
    tree.root.id = "KeepRunningUntilFailure";
    tree.root.params.emplace("max_iterations", actions::Value{3.0});
    actions::Node child;
    child.id = "RepeatUntilFailure";
    tree.root.children.push_back(child);

    actions::Executor legacy;
    int legacy_calls = 0;
    legacy.registerTask("RepeatUntilFailure", [&legacy_calls](const auto&,
                                                               actions::Blackboard&) {
      ++legacy_calls;
      return legacy_calls < 3 ? actions::NodeStatus::SUCCESS
                              : actions::NodeStatus::FAILURE;
    });
    actions::Blackboard legacy_bb;
    actions::ExecutionContext legacy_context;
    CHECK(legacy.execute(tree.root, legacy_bb, legacy_context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(legacy_calls == 3);

    actions::Executor c11;
    int c11_calls = 0;
    c11.registerTask("RepeatUntilFailure", [&c11_calls](const auto&,
                                                         actions::Blackboard&) {
      ++c11_calls;
      return c11_calls < 3 ? actions::NodeStatus::SUCCESS
                           : actions::NodeStatus::FAILURE;
    });
    actions::Blackboard c11_bb;
    actions::ExecutionContext c11_context;
    actions::c11::TreeExecutorSession session(tree, c11, c11_bb, c11_context,
                                              limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(c11_calls == legacy_calls);
  }
  return 0;
}

int testSubTreeExpansionAndCycleRejection() {
  {
    actions::Tree registered;
    registered.name = "inner";
    registered.root.id = "InnerTask";

    actions::Tree outer;
    outer.name = "outer";
    outer.root.id = "Sequence";
    actions::Node subtree;
    subtree.id = "SubTree";
    subtree.params.emplace("tree", actions::Value{std::string("inner")});
    actions::Node after;
    after.id = "AfterSubTree";
    outer.root.children.push_back(subtree);
    outer.root.children.push_back(after);

    actions::Executor executor;
    executor.registerTree(registered);
    int inner_calls = 0;
    int after_calls = 0;
    executor.registerTask("InnerTask", [&inner_calls](const auto&, actions::Blackboard&) {
      ++inner_calls;
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("AfterSubTree", [&after_calls](const auto&, actions::Blackboard&) {
      ++after_calls;
      return actions::NodeStatus::SUCCESS;
    });

    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(outer, executor, blackboard,
                                              context, limits());
    CHECK(session.plan().nodes.size() == 3u);
    CHECK(session.sourceNode(1u)->id == "InnerTask");
    CHECK(session.sourceNode(2u)->id == "AfterSubTree");
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(inner_calls == 1);
    CHECK(after_calls == 1);
  }

  {
    actions::Tree recursive;
    recursive.name = "loop";
    recursive.root.id = "Sequence";
    actions::Node self;
    self.id = "SubTree";
    self.params.emplace("tree", actions::Value{std::string("loop")});
    recursive.root.children.push_back(self);

    actions::Tree outer;
    outer.root.id = "SubTree";
    outer.root.params.emplace("tree", actions::Value{std::string("loop")});

    actions::Executor executor;
    executor.registerTree(recursive);
    actions::Blackboard blackboard;
    actions::ExecutionContext context;
    bool rejected = false;
    try {
      actions::c11::TreeExecutorSession session(outer, executor, blackboard,
                                                context, limits());
      (void)session;
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    CHECK(rejected);
  }
  return 0;
}


int testPipelineSequencePreservesInputMutationWithinStage() {
  const auto makeTree = [] {
    actions::Tree tree;
    tree.root.id = "PipelineSequence";
    tree.root.params.emplace("input_key", actions::Value{std::string("seed")});
    tree.root.params.emplace("output_key", actions::Value{std::string("result")});
    actions::Node sequence;
    sequence.id = "Sequence";
    actions::Node mutate;
    mutate.id = "MutatePipelineInput";
    actions::Node observe;
    observe.id = "ObservePipelineInput";
    sequence.children.push_back(mutate);
    sequence.children.push_back(observe);
    tree.root.children.push_back(sequence);
    return tree;
  };
  const auto registerTasks = [](actions::Executor& executor) {
    executor.registerTask("MutatePipelineInput")(
        [](const auto&, actions::Blackboard& bb) {
          bb.set("pipeline_input", "mutated");
          return actions::NodeStatus::SUCCESS;
        });
    executor.registerTask("ObservePipelineInput")(
        [](const auto&, actions::Blackboard& bb) {
          if (bb.get("pipeline_input") != "mutated")
            return actions::NodeStatus::FAILURE;
          bb.set("pipeline_output", "done");
          return actions::NodeStatus::SUCCESS;
        });
  };

  {
    auto tree = makeTree();
    actions::Executor executor;
    registerTasks(executor);
    actions::Blackboard blackboard;
    blackboard.set("seed", "start");
    actions::ExecutionContext context;
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(blackboard.get("result") == "done");
  }
  {
    auto tree = makeTree();
    actions::Executor executor;
    registerTasks(executor);
    actions::Blackboard blackboard;
    blackboard.set("seed", "start");
    actions::ExecutionContext context;
    actions::c11::TreeExecutorSession session(
        tree, executor, blackboard, context, limits());
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(blackboard.get("result") == "done");
  }
  return 0;
}

int testPipelineSequenceRestoresInputBeforeNestedSwitchSelection() {
  const auto makeTree = [] {
    actions::Tree tree;
    tree.root.id = "PipelineSequence";
    tree.root.params.emplace("input_key", actions::Value{std::string("seed")});
    tree.root.params.emplace("output_key", actions::Value{std::string("result")});
    actions::Node selector;
    selector.id = "Switch";
    selector.params.emplace("variable",
                            actions::Value{std::string("{pipeline_input}")});
    actions::Node selected;
    selected.id = "SelectedRunningBranch";
    actions::Node unselected;
    unselected.id = "UnselectedBranch";
    selector.children.push_back(selected);
    selector.children.push_back(unselected);
    tree.root.children.push_back(selector);
    return tree;
  };
  const auto registerTasks = [](actions::Executor& executor, int& selected_calls,
                                int& unselected_calls) {
    executor.registerTask("SelectedRunningBranch", [&selected_calls](
        const auto&, actions::Blackboard& bb) {
      ++selected_calls;
      if (bb.get("pipeline_input") != "0")
        return actions::NodeStatus::FAILURE;
      if (selected_calls == 1) {
        bb.set("pipeline_input", "corrupted");
        return actions::NodeStatus::RUNNING;
      }
      bb.set("pipeline_output", "done");
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("UnselectedBranch", [&unselected_calls](
        const auto&, actions::Blackboard&) {
      ++unselected_calls;
      return actions::NodeStatus::SUCCESS;
    });
  };

  {
    auto tree = makeTree();
    actions::Executor executor;
    int selected_calls = 0, unselected_calls = 0;
    registerTasks(executor, selected_calls, unselected_calls);
    actions::Blackboard blackboard;
    blackboard.set("seed", "0");
    actions::ExecutionContext context;
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::RUNNING);
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(selected_calls == 2);
    CHECK(unselected_calls == 0);
    CHECK(blackboard.get("result") == "done");
  }

  {
    auto tree = makeTree();
    actions::Executor executor;
    int selected_calls = 0, unselected_calls = 0;
    registerTasks(executor, selected_calls, unselected_calls);
    actions::Blackboard blackboard;
    blackboard.set("seed", "0");
    actions::ExecutionContext context;
    praktor_c11_bt_host hooks{};
    hooks.halt_leaf = &noOpHalt;
    actions::c11::TreeExecutorSession session(
        tree, executor, blackboard, context, limits(), &hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(selected_calls == 2);
    CHECK(unselected_calls == 0);
    CHECK(blackboard.get("result") == "done");
  }
  return 0;
}

int testPipelineSequenceResumesActiveStageAndRestoresInput() {
  const auto makeTree = [] {
    actions::Tree tree;
    tree.root.id = "PipelineSequence";
    tree.root.params.emplace("input_key", actions::Value{std::string("seed")});
    tree.root.params.emplace("output_key", actions::Value{std::string("result")});
    actions::Node first;
    first.id = "FirstStage";
    actions::Node second;
    second.id = "RunningStage";
    tree.root.children.push_back(first);
    tree.root.children.push_back(second);
    return tree;
  };

  {
    auto tree = makeTree();
    actions::Executor executor;
    int first_calls = 0;
    int second_calls = 0;
    executor.registerTask("FirstStage", [&first_calls](
        const auto&, actions::Blackboard& bb) {
      ++first_calls;
      if (bb.get("pipeline_input") != "start")
        return actions::NodeStatus::FAILURE;
      bb.set("pipeline_output", "stage-one");
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("RunningStage", [&second_calls](
        const auto&, actions::Blackboard& bb) {
      ++second_calls;
      if (bb.get("pipeline_input") != "stage-one")
        return actions::NodeStatus::FAILURE;
      if (second_calls == 1) {
        bb.set("pipeline_input", "corrupted");
        bb.set("pipeline_output", "intermediate");
        return actions::NodeStatus::RUNNING;
      }
      bb.set("pipeline_output", "stage-two");
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    blackboard.set("seed", "start");
    actions::ExecutionContext context;
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::RUNNING);
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(first_calls == 1);
    CHECK(second_calls == 2);
    CHECK(blackboard.get("result") == "stage-two");
  }

  {
    auto tree = makeTree();
    actions::Executor executor;
    int first_calls = 0;
    int second_calls = 0;
    executor.registerTask("FirstStage", [&first_calls](
        const auto&, actions::Blackboard& bb) {
      ++first_calls;
      if (bb.get("pipeline_input") != "start")
        return actions::NodeStatus::FAILURE;
      bb.set("pipeline_output", "stage-one");
      return actions::NodeStatus::SUCCESS;
    });
    executor.registerTask("RunningStage", [&second_calls](
        const auto&, actions::Blackboard& bb) {
      ++second_calls;
      if (bb.get("pipeline_input") != "stage-one")
        return actions::NodeStatus::FAILURE;
      if (second_calls == 1) {
        bb.set("pipeline_input", "corrupted");
        bb.set("pipeline_output", "intermediate");
        return actions::NodeStatus::RUNNING;
      }
      bb.set("pipeline_output", "stage-two");
      return actions::NodeStatus::SUCCESS;
    });
    actions::Blackboard blackboard;
    blackboard.set("seed", "start");
    actions::ExecutionContext context;
    praktor_c11_bt_host hooks{};
    hooks.halt_leaf = &noOpHalt;
    actions::c11::TreeExecutorSession session(
        tree, executor, blackboard, context, limits(), &hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    const int first_tick_status = session.tick(&status);
    if (first_tick_status != SALTS_OK) {
      std::fprintf(stderr, "PipelineSequence first tick failed with status %d: %s\\n",
                   first_tick_status, session.lastError().c_str());
    }
    CHECK(first_tick_status == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    const int second_tick_status = session.tick(&status);
    if (second_tick_status != SALTS_OK) {
      std::fprintf(stderr, "PipelineSequence second tick failed with status %d: %s\\n",
                   second_tick_status, session.lastError().c_str());
    }
    CHECK(second_tick_status == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(first_calls == 1);
    CHECK(second_calls == 2);
    CHECK(blackboard.get("result") == "stage-two");
  }
  return 0;
}

int testBuiltInPipelineSequenceHooks() {
  actions::Tree tree;
  tree.root.id = "PipelineSequence";
  tree.root.params.emplace("input_key", actions::Value{std::string("seed")});
  tree.root.params.emplace("output_key", actions::Value{std::string("result")});
  actions::Node first;
  first.id = "PipelineFirst";
  actions::Node second;
  second.id = "PipelineSecond";
  tree.root.children.push_back(first);
  tree.root.children.push_back(second);

  actions::Executor executor;
  executor.registerTask("PipelineFirst", [](const auto&, actions::Blackboard& bb) {
    if (bb.get("pipeline_input") != "start") return actions::NodeStatus::FAILURE;
    bb.set("pipeline_output", "stage-one");
    return actions::NodeStatus::SUCCESS;
  });
  executor.registerTask("PipelineSecond", [](const auto&, actions::Blackboard& bb) {
    if (bb.get("pipeline_input") != "stage-one") return actions::NodeStatus::FAILURE;
    bb.set("pipeline_output", "stage-two");
    return actions::NodeStatus::SUCCESS;
  });

  actions::Blackboard blackboard;
  blackboard.set("seed", "start");
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(blackboard.get("result") == "stage-two");
  CHECK(blackboard.get("pipeline_input") == "stage-one");
  return 0;
}

int testConsumeQueueResumesCurrentItemAcrossRunning() {
  const auto makeTree = [] {
    actions::Tree tree;
    tree.root.id = "ConsumeQueue";
    tree.root.params.emplace("queue_key", actions::Value{std::string("queue")});
    tree.root.params.emplace("item_key", actions::Value{std::string("item")});
    actions::Node child;
    child.id = "ProcessItem";
    tree.root.children.push_back(child);
    return tree;
  };
  const auto install = [](actions::Executor& executor, int& calls,
                          std::string& first, std::string& second,
                          std::string& third) {
    executor.registerTask("ProcessItem", [&calls, &first, &second, &third](
        const auto&, actions::Blackboard& bb) {
      ++calls;
      if (calls == 1) {
        first = bb.get("item");
        return actions::NodeStatus::RUNNING;
      }
      if (calls == 2) {
        second = bb.get("item");
        return actions::NodeStatus::SUCCESS;
      }
      third = bb.get("item");
      return actions::NodeStatus::SUCCESS;
    });
  };

  {
    auto tree = makeTree();
    actions::Executor executor;
    int calls = 0;
    std::string first, second, third;
    install(executor, calls, first, second, third);
    actions::Blackboard blackboard;
    blackboard.set("queue", "[\"one\",\"two\"]");
    actions::ExecutionContext context;
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::RUNNING);
    CHECK(blackboard.get("queue") == "[\"two\"]");
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::RUNNING);
    CHECK(blackboard.get("queue") == "[\"two\"]");
    CHECK(executor.execute(tree.root, blackboard, context) ==
          actions::NodeStatus::SUCCESS);
    CHECK(calls == 3);
    CHECK(first == "\"one\"");
    CHECK(second == "\"one\"");
    CHECK(third == "\"two\"");
    CHECK(blackboard.get("queue") == "[]");
  }

  {
    auto tree = makeTree();
    actions::Executor executor;
    int calls = 0;
    std::string first, second, third;
    install(executor, calls, first, second, third);
    actions::Blackboard blackboard;
    blackboard.set("queue", "[\"one\",\"two\"]");
    actions::ExecutionContext context;
    praktor_c11_bt_host hooks{};
    hooks.halt_leaf = &noOpHalt;
    actions::c11::TreeExecutorSession session(
        tree, executor, blackboard, context, limits(), &hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(blackboard.get("queue") == "[\"two\"]");
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_RUNNING);
    CHECK(blackboard.get("queue") == "[\"two\"]");
    CHECK(session.tick(&status) == SALTS_OK);
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(calls == 3);
    CHECK(first == "\"one\"");
    CHECK(second == "\"one\"");
    CHECK(third == "\"two\"");
    CHECK(blackboard.get("queue") == "[]");
  }
  return 0;
}

int testBuiltInConsumeQueue() {
  actions::Tree tree;
  tree.root.id = "ConsumeQueue";
  tree.root.params.emplace("queue_key", actions::Value{std::string("queue")});
  tree.root.params.emplace("item_key", actions::Value{std::string("item")});
  actions::Node child;
  child.id = "ProcessItem";
  tree.root.children.push_back(child);

  actions::Executor executor;
  int calls = 0;
  std::string last_item;
  executor.registerTask("ProcessItem", [&calls, &last_item](const auto&,
                                                            actions::Blackboard& bb) {
    ++calls;
    last_item = bb.get("item");
    return actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard blackboard;
  blackboard.set("queue", "[\"one\",\"two\"]");
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(calls == 1);
  CHECK(last_item == "\"one\"");
  CHECK(blackboard.get("queue") == "[\"two\"]");
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(calls == 2);
  CHECK(last_item == "\"two\"");
  CHECK(blackboard.get("queue") == "[]");
  return 0;
}

int testConsumeQueueRejectsMalformedQueue() {
  actions::Tree tree;
  tree.root.id = "ConsumeQueue";
  tree.root.params.emplace("queue_key", actions::Value{std::string("queue")});
  actions::Node child;
  child.id = "ProcessItem";
  tree.root.children.push_back(child);

  actions::Executor executor;
  executor.registerTask("ProcessItem", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard blackboard;
  blackboard.set("queue", "{not-an-array}");
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_FAILURE);
  CHECK(session.lastError().find("valid JSON") != std::string::npos);
  return 0;
}

int testBuiltInEntryUpdatedHook() {
  actions::Tree tree;
  tree.root.id = "EntryUpdated";
  tree.root.params.emplace("watch_key", actions::Value{std::string("watched")});
  actions::Node child;
  child.id = "Observe";
  tree.root.children.push_back(child);

  actions::Executor executor;
  int calls = 0;
  executor.registerTask("Observe", [&calls](const auto&, actions::Blackboard&) {
    ++calls;
    return actions::NodeStatus::SUCCESS;
  });
  actions::Blackboard blackboard;
  blackboard.set("watched", "first");
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits());
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(calls == 1);
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(calls == 1);
  blackboard.set("watched", "second");
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_SUCCESS);
  CHECK(calls == 2);
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

void noOpParallelAbort(void*, size_t, size_t) {}


struct FailingHostLeafState {
  int tick_calls = 0;
  int halt_calls = 0;
};

int failingHostLeafTick(void* user, size_t, salts_bt_node_status*) {
  auto* state = static_cast<FailingHostLeafState*>(user);
  ++state->tick_calls;
  return SALTS_EIO;
}

void failingHostLeafHalt(void* user, size_t) {
  auto* state = static_cast<FailingHostLeafState*>(user);
  ++state->halt_calls;
}

struct ThrowingHaltState {
  int ticks = 0;
  int halts = 0;
};

int runningHostTick(void* user, size_t, salts_bt_node_status* out_status) {
  auto* state = static_cast<ThrowingHaltState*>(user);
  ++state->ticks;
  *out_status = SALTS_BT_RUNNING;
  return SALTS_OK;
}

void throwingHalt(void* user, size_t) {
  auto* state = static_cast<ThrowingHaltState*>(user);
  ++state->halts;
  throw std::runtime_error("expected halt cleanup failure");
}

int testCancelReportsHostHaltFailure() {
  actions::Tree tree;
  tree.root.id = "HostAsync";
  actions::Executor executor;
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  ThrowingHaltState state;
  praktor_c11_bt_host hooks{};
  hooks.tick_leaf = &runningHostTick;
  hooks.halt_leaf = &throwingHalt;
  hooks.user = &state;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits(), &hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK);
  CHECK(status == SALTS_BT_RUNNING);
  CHECK(state.ticks == 1);
  CHECK(session.cancel() == SALTS_EIO);
  CHECK(state.halts == 1);
  CHECK(session.lastError().find("C11 host leaf halt failed") != std::string::npos);
  return 0;
}

int testFailedInitialHostTickRunsCleanupExactlyOnce() {
  actions::Tree tree;
  tree.root.id = "HostManagedTask";
  actions::Executor executor;
  int executor_calls = 0;
  executor.registerTask("HostManagedTask", [&executor_calls](const auto&,
                                                              actions::Blackboard&) {
    ++executor_calls;
    return actions::NodeStatus::SUCCESS;
  });

  FailingHostLeafState state;
  praktor_c11_bt_host hooks{};
  hooks.tick_leaf = &failingHostLeafTick;
  hooks.halt_leaf = &failingHostLeafHalt;
  hooks.user = &state;
  actions::Blackboard blackboard;
  actions::ExecutionContext context;
  actions::c11::TreeExecutorSession session(tree, executor, blackboard,
                                            context, limits(), &hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_EIO);
  CHECK(state.tick_calls == 1);
  CHECK(state.halt_calls == 1);
  CHECK(executor_calls == 0);
  CHECK(session.lastError().find("host leaf hook returned status") != std::string::npos);
  return 0;
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
  hooks.parallel_abort = &noOpParallelAbort;
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
  if (testRunningRegisteredLeafRequiresHaltHook() != 0) return 1;
  if (testExecutorOwnedRunningLeafDoesNotReenterDeclinedHook() != 0) return 1;
  if (testShellUsesSaltsProcessByDefaultAndAllowsExplicitOverride() != 0) return 1;
  if (testShellRejectsZeroTimeoutBeforeSpawningProcess() != 0) return 1;
  if (testShellCancelTerminatesManagedProcess() != 0) return 1;
  if (testCancelCallsHaltForRegisteredRunningLeaf() != 0) return 1;
  if (testScalarConditionAndSwitchUseBlackboard() != 0) return 1;
  if (testInTreeIfThenElseConditionMatchesExecutor() != 0) return 1;
  if (testWhileDoConditionFormsMatchTerminalEffects() != 0) return 1;
  if (testReactiveFallbackRechecksPriorityAfterRunning() != 0) return 1;
  if (testEmptyReactiveFallbackMatchesFailure() != 0) return 1;
  if (testEmptySwitchResolvesSelectorThenFails() != 0) return 1;
  if (testEmptyParallelAndReactiveSequenceMatchExecutor() != 0) return 1;
  if (testRunOnceAndKeepRunningStateMatchExecutor() != 0) return 1;
  if (testSubTreeExpansionAndCycleRejection() != 0) return 1;
  if (testPipelineSequencePreservesInputMutationWithinStage() != 0) return 1;
  if (testPipelineSequenceResumesActiveStageAndRestoresInput() != 0) return 1;
  if (testPipelineSequenceRestoresInputBeforeNestedSwitchSelection() != 0) return 1;
  if (testBuiltInPipelineSequenceHooks() != 0) return 1;
  if (testConsumeQueueResumesCurrentItemAcrossRunning() != 0) return 1;
  if (testBuiltInConsumeQueue() != 0) return 1;
  if (testConsumeQueueRejectsMalformedQueue() != 0) return 1;
  if (testBuiltInEntryUpdatedHook() != 0) return 1;
  if (testCancelReportsHostHaltFailure() != 0) return 1;
  if (testFailedInitialHostTickRunsCleanupExactlyOnce() != 0) return 1;
  if (testHostExceptionsStayInsideCAbi() != 0) return 1;
  return 0;
}
