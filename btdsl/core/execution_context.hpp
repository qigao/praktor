#pragma once

#include <map>
#include <string>

namespace btdsl {

enum class NodeStatus;

// State for a single node during execution
struct NodeState {
  int current_child = 0;  // For Sequence/Fallback: which child is currently executing
  int retry_count = 0;    // For Retry: how many attempts have been made
  NodeStatus last_status; // Last execution status
};

// Execution context that maintains state across multiple ticks
class ExecutionContext {
public:
  // Get state for a node (creates if doesn't exist)
  NodeState& getState(const std::string& node_id);

  // Check if state exists for a node
  bool hasState(const std::string& node_id) const;

  // Reset state for a specific node
  void reset(const std::string& node_id);

  // Clear all states
  void clear();

private:
  std::map<std::string, NodeState> node_states_;
};

} // namespace btdsl
