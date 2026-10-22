#include "core/executor.hpp"
#include "tree_executor_session.hpp"
#include "session_compatibility.hpp"
#include "plan_value_reader.hpp"
#include <praktor/shell/shell_executor.hpp>

namespace actions::c11 {
namespace {
struct LegacyAdapter final : detail::SessionCompatibility {
  actions::Executor& executor;
  actions::Blackboard& blackboard;
  actions::ExecutionContext& context;
  LegacyAdapter(actions::Executor& e, actions::Blackboard& b,
                actions::ExecutionContext& c) : executor(e), blackboard(b), context(c) {}
  std::string get(const std::string& key) const override { return blackboard.get(key); }
  bool has(const std::string& key) const override { return blackboard.has(key); }
  void set(const std::string& key, const std::string& value) override { blackboard.set(key, value); }
  salts_bt_node_status tick(const actions::Node& node) override {
    switch (executor.execute(node, blackboard, context)) {
      case actions::NodeStatus::SUCCESS: return SALTS_BT_SUCCESS;
      case actions::NodeStatus::FAILURE: return SALTS_BT_FAILURE;
      case actions::NodeStatus::RUNNING: return SALTS_BT_RUNNING;
    }
    return SALTS_BT_FAILURE;
  }
  std::vector<std::string> environment() const override {
    std::vector<std::string> result;
    for (const auto& entry : executor.getEnvironment())
      result.push_back(entry.first + "=" + entry.second);
    return result;
  }
  void emit(const std::string& line) const override {
    Praktor::Shell::ShellExecutor::emitStreamLine(line);
  }
};
detail::PlanValueReader reader(const actions::Blackboard& blackboard) {
  return {[&blackboard](const std::string& key) { return blackboard.get(key); },
          [&blackboard](const std::string& key) { return blackboard.has(key); }};
}
}
TreeExecutorSession::TreeExecutorSession(
    const actions::Tree& tree, actions::Executor& executor,
    actions::Blackboard& blackboard,
    actions::ExecutionContext& execution_context,
    const salts_bt_tree_config& limits,
    const praktor_c11_bt_host* host_hooks,
    cflow_process_retainer* recovery_owner,
    uint32_t shell_close_budget_ms)
    : TreeExecutorSession(
          tree, executor, blackboard, execution_context, limits,
          [&executor](const std::string& name) { return executor.getTree(name); },
          host_hooks, recovery_owner, shell_close_budget_ms) {}

TreeExecutorSession::TreeExecutorSession(
    const actions::Tree& tree, actions::Executor& executor,
    actions::Blackboard& blackboard,
    actions::ExecutionContext& execution_context,
    const salts_bt_tree_config& limits,
    const SubTreeResolver& subtree_resolver,
    const praktor_c11_bt_host* host_hooks,
    cflow_process_retainer* recovery_owner,
    uint32_t shell_close_budget_ms)
    : TreeExecutorSession(tree, subtree_resolver, limits, host_hooks,
                          recovery_owner, shell_close_budget_ms,
                          std::make_shared<LegacyAdapter>(executor, blackboard,
                                                          execution_context)) {}


int64_t resolveIntegerParameter(const actions::Node& node,
                                const actions::Blackboard& blackboard,
                                const char* key, int64_t default_value, bool required) {
  return resolveIntegerParameter(node, reader(blackboard), key, default_value, required);
}
uint64_t resolveDurationParameter(const actions::Node& node,
                                  const actions::Blackboard& blackboard,
                                  const char* key, int64_t default_ms) {
  return resolveDurationParameter(node, reader(blackboard), key, default_ms);
}
TreePlan compileTreePlan(const actions::Tree& tree,
                         const actions::Blackboard& blackboard,
                         const salts_bt_tree_config& limits) {
  return compileTreePlan(tree, reader(blackboard), limits);
}
} // namespace actions::c11
