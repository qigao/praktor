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
    CHECK(blackboard.get("shell_error").find("unsuccessfully") != std::string::npos);
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
  CHECK(session.tick(&status) == SALTS_EIO);
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
  if (testCancelCallsHaltForRegisteredRunningLeaf() != 0) return 1;
  if (testScalarConditionAndSwitchUseBlackboard() != 0) return 1;
  if (testSubTreeExpansionAndCycleRejection() != 0) return 1;
  if (testBuiltInPipelineSequenceHooks() != 0) return 1;
  if (testBuiltInConsumeQueue() != 0) return 1;
  if (testConsumeQueueRejectsMalformedQueue() != 0) return 1;
  if (testBuiltInEntryUpdatedHook() != 0) return 1;
  if (testHostExceptionsStayInsideCAbi() != 0) return 1;
  return 0;
}
