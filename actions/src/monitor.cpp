#include "actions/monitor.hpp"
#include "core/executor.hpp"
#include <algorithm>

namespace actions {

void ExecutionMonitor::recordExecution(const std::string& node_id, 
                                       NodeStatus status, 
                                       std::chrono::milliseconds duration) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto& metrics = metrics_[node_id];
  metrics.node_id = node_id;
  metrics.execution_count++;
  metrics.total_duration += duration;
  
  // Update min/max duration
  metrics.min_duration = std::min(metrics.min_duration, duration);
  metrics.max_duration = std::max(metrics.max_duration, duration);
  
  // Update average
  metrics.avg_duration = metrics.total_duration / metrics.execution_count;
  
  // Update status counts
  switch (status) {
    case NodeStatus::SUCCESS:
      metrics.success_count++;
      break;
    case NodeStatus::FAILURE:
      metrics.failure_count++;
      break;
    case NodeStatus::RUNNING:
      metrics.running_count++;
      break;
  }
}

NodeMetrics ExecutionMonitor::getMetrics(const std::string& node_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = metrics_.find(node_id);
  if (it != metrics_.end()) {
    return it->second;
  }
  
  // Return empty metrics if not found
  NodeMetrics empty;
  empty.node_id = node_id;
  return empty;
}

std::map<std::string, NodeMetrics> ExecutionMonitor::getAllMetrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_;
}

void ExecutionMonitor::reset(const std::string& node_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  metrics_.erase(node_id);
}

void ExecutionMonitor::resetAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  metrics_.clear();
}

size_t ExecutionMonitor::getNodeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_.size();
}

ExecutionMonitor::Summary ExecutionMonitor::getSummary() const {
  std::lock_guard<std::mutex> lock(mutex_);
  
  Summary summary;
  
  for (const auto& [node_id, metrics] : metrics_) {
    summary.total_executions += metrics.execution_count;
    summary.total_successes += metrics.success_count;
    summary.total_failures += metrics.failure_count;
    summary.total_duration += metrics.total_duration;
  }
  
  if (summary.total_executions > 0) {
    summary.avg_duration = summary.total_duration / summary.total_executions;
    summary.overall_success_rate = 
        static_cast<double>(summary.total_successes) / summary.total_executions * 100.0;
  }
  
  return summary;
}

} // namespace actions
