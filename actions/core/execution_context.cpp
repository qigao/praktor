#include "core/execution_context.hpp"
#include "core/executor.hpp"

namespace actions {

NodeState& ExecutionContext::getState(const std::string& node_id) {
  return node_states_[node_id];
}

bool ExecutionContext::hasState(const std::string& node_id) const {
  return node_states_.find(node_id) != node_states_.end();
}

void ExecutionContext::reset(const std::string& node_id) {
  node_states_.erase(node_id);
}

void ExecutionContext::clear() {
  node_states_.clear();
}

} // namespace actions
