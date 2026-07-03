#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace actions {

// Forward declaration
enum class NodeStatus;

/**
 * @brief Metrics for a single node
 */
struct NodeMetrics {
  std::string node_id;
  int execution_count = 0;
  int success_count = 0;
  int failure_count = 0;
  int running_count = 0;
  std::chrono::milliseconds total_duration{0};
  std::chrono::milliseconds min_duration{std::chrono::milliseconds::max()};
  std::chrono::milliseconds max_duration{0};
  std::chrono::milliseconds avg_duration{0};
  
  double success_rate() const {
    if (execution_count == 0) return 0.0;
    return static_cast<double>(success_count) / execution_count * 100.0;
  }
  
  double failure_rate() const {
    if (execution_count == 0) return 0.0;
    return static_cast<double>(failure_count) / execution_count * 100.0;
  }
};

/**
 * @brief Execution monitor for action orchestration
 * 
 * Tracks execution statistics for each node in the tree.
 * Thread-safe for concurrent access.
 */
class ExecutionMonitor {
public:
  /**
   * @brief Record a node execution
   * @param node_id Unique identifier for the node
   * @param status Execution result
   * @param duration Time taken to execute
   */
  void recordExecution(const std::string& node_id, 
                       NodeStatus status, 
                       std::chrono::milliseconds duration);
  
  /**
   * @brief Get metrics for a specific node
   * @param node_id Node identifier
   * @return Metrics for the node, or empty metrics if not found
   */
  NodeMetrics getMetrics(const std::string& node_id) const;
  
  /**
   * @brief Get metrics for all monitored nodes
   * @return Map of node_id to metrics
   */
  std::map<std::string, NodeMetrics> getAllMetrics() const;
  
  /**
   * @brief Reset metrics for a specific node
   * @param node_id Node identifier
   */
  void reset(const std::string& node_id);
  
  /**
   * @brief Reset all metrics
   */
  void resetAll();
  
  /**
   * @brief Get total number of monitored nodes
   */
  size_t getNodeCount() const;
  
  /**
   * @brief Get summary statistics across all nodes
   */
  struct Summary {
    int total_executions = 0;
    int total_successes = 0;
    int total_failures = 0;
    std::chrono::milliseconds total_duration{0};
    std::chrono::milliseconds avg_duration{0};
    double overall_success_rate = 0.0;
  };
  
  Summary getSummary() const;

private:
  mutable std::mutex mutex_;
  std::map<std::string, NodeMetrics> metrics_;
};

} // namespace actions
