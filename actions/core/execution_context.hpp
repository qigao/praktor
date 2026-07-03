#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace actions {

// Forward declaration
enum class NodeStatus;

// State for a single node during execution
struct NodeState {
  std::size_t current_child = 0;  // For Sequence/Fallback: which child is currently executing
  int retry_count = 0;    // For Retry: how many attempts have been made
  bool has_run = false;   // For RunOnce: whether node has been executed
  NodeStatus last_status; // Last execution status (only valid if has_run == true)
  std::string last_value; // For EntryUpdated: last observed value
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

} // namespace actions
