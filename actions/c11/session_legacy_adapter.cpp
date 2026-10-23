#include "core/executor.hpp"
#include "tree_executor_session.hpp"
#include "session_compatibility.hpp"
#include "plan.h"
#include <stdexcept>
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
  const auto it = node.params.find(key);
  if (it == node.params.end()) {
    if (required) throw std::invalid_argument(std::string("Missing required integer parameter: ") + key);
    return default_value;
  }
  praktor_c11_leaf_parameter p{}; std::string resolved; size_t size = 0;
  if (std::holds_alternative<std::string>(it->second)) {
    resolved = std::get<std::string>(it->second);
    if (resolved.size() >= 2 && resolved.front() == '{' && resolved.back() == '}') {
      const auto name = resolved.substr(1,resolved.size()-2);
      if (!blackboard.has(name)) throw std::invalid_argument("Missing blackboard key: " + name);
      resolved = blackboard.get(name);
    }
    p.kind = PRAKTOR_C11_LEAF_STRING; p.text = resolved.c_str(); size = resolved.size();
  } else if (std::holds_alternative<double>(it->second)) {
    p.kind = PRAKTOR_C11_LEAF_NUMBER; p.number = std::get<double>(it->second);
  } else { p.kind = PRAKTOR_C11_LEAF_BOOLEAN; p.boolean = std::get<bool>(it->second); }
  int value = 0;
  if (praktor_c11_parameter_integer(&p,size,&value) != SALTS_OK)
    throw std::invalid_argument(std::string("Parameter must be a 32-bit integer: ") + key);
  return value;
}
uint64_t resolveDurationParameter(const actions::Node& node,
                                  const actions::Blackboard& blackboard,
                                  const char* key, int64_t default_ms) {
  const auto value = resolveIntegerParameter(node,blackboard,key,default_ms);
  if (value < 0 || static_cast<uint64_t>(value) > UINT64_MAX/UINT64_C(1000000))
    throw std::invalid_argument("Duration must be non-negative and fit nanoseconds");
  return static_cast<uint64_t>(value)*UINT64_C(1000000);
}
TreePlan compileTreePlan(const actions::Tree& tree,
                         const actions::Blackboard& blackboard,
                         const salts_bt_tree_config& limits) {
  (void)blackboard; // Substituted controls are always resolved on tick.
  return compileTreePlan(tree, limits);
}
} // namespace actions::c11
