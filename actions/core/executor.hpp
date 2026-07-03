#pragma once

#include "ast.hpp"
#include "actions/event_queue.hpp"
#include "actions/async_executor.hpp"
#include "actions/monitor.hpp"
#include "execution_context.hpp"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace actions {

enum class NodeStatus {
  SUCCESS,
  FAILURE,
  RUNNING
};

// Simple Blackboard for sharing data between nodes
class Blackboard {
public:
  Blackboard() = default;

  // Copy constructor — produces an independent snapshot of the blackboard state.
  // Used by Parallel to give each branch an isolated starting state.
  Blackboard(const Blackboard& other) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    data_ = other.data_;
    // events_ is not copied: each branch starts with an empty event queue.
  }

  Blackboard& operator=(const Blackboard& other) {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex_, other.mutex_);
    data_ = other.data_;
    return *this;
  }

  void set(const std::string& key, const std::string& value);
  std::string get(const std::string& key) const;
  bool has(const std::string& key) const;

  // Merge all entries from this blackboard into `target` (last-writer-wins per key).
  // Called sequentially after all parallel branches finish, so no locking race.
  void mergeInto(Blackboard& target) const {
    std::lock_guard<std::mutex> src_lock(mutex_);
    std::lock_guard<std::mutex> dst_lock(target.mutex_);
    for (const auto& [key, value] : data_) {
      target.data_[key] = value;
    }
  }

  // Event queue integration
  void triggerEvent(const std::string& event);
  bool pollEvent(const std::string& event);
  bool hasEvent(const std::string& event) const;
  EventQueue& getEventQueue() { return events_; }

private:
  std::map<std::string, std::string> data_;
  mutable std::mutex mutex_;
  EventQueue events_;
};

// Task function signature: takes params and blackboard, returns status
using TaskFunction = std::function<NodeStatus(const std::map<std::string, Value>&, Blackboard&)>;
using NodeHandler = std::function<NodeStatus(const Node&, Blackboard&)>;
using StatefulNodeHandler = std::function<NodeStatus(const Node&, Blackboard&, ExecutionContext&)>;

class Executor {
public:
  Executor();

  // Register a task handler
  void registerTask(const std::string& name, TaskFunction func);

  // Register built-in control flow nodes
  void registerBuiltins();

  // Execute a tree (without state management)
  NodeStatus execute(const Tree& tree, Blackboard& bb);

  // Execute a tree (with state management)
  NodeStatus execute(const Tree& tree, Blackboard& bb, ExecutionContext& ctx);

  // Execute a node (without state management)
  NodeStatus execute(const Node& node, Blackboard& bb);

  // Execute a node (with state management)
  NodeStatus execute(const Node& node, Blackboard& bb, ExecutionContext& ctx);

  // Get a tree by name (for SubTree support)
  void registerTree(const Tree& tree);
  const Tree* getTree(const std::string& name) const;

  // Get async executor
  AsyncExecutor& getAsyncExecutor() { return async_executor_; }

  // Set environment variables for Shell nodes
  void setEnvironment(const std::map<std::string, std::string>& env) { environment_ = env; }
  const std::map<std::string, std::string>& getEnvironment() const { return environment_; }
  
  // Monitoring
  void enableMonitoring(bool enable = true) { monitoring_enabled_ = enable; }
  bool isMonitoringEnabled() const { return monitoring_enabled_; }
  ExecutionMonitor& getMonitor() { return monitor_; }
  const ExecutionMonitor& getMonitor() const { return monitor_; }

private:
  std::map<std::string, TaskFunction> tasks_;
  std::map<std::string, Tree> trees_;
  std::unordered_map<std::string, NodeHandler> dispatch_;
  std::unordered_map<std::string, StatefulNodeHandler> stateful_dispatch_;
  AsyncExecutor async_executor_;
  ExecutionContext default_context_;
  std::map<std::string, std::string> environment_;  // Environment variables for Shell nodes
  ExecutionMonitor monitor_;  // Execution monitoring
  bool monitoring_enabled_ = false;  // Monitoring toggle

  // Built-in control flow implementations
  NodeStatus executeSequence(const Node& node, Blackboard& bb);
  NodeStatus executeSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeFallback(const Node& node, Blackboard& bb);
  NodeStatus executeFallback(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeParallel(const Node& node, Blackboard& bb);
  NodeStatus executeParallel(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeReactiveSequence(const Node& node, Blackboard& bb);
  NodeStatus executeReactiveSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx);

  // Built-in decorator implementations
  NodeStatus executeInverter(const Node& node, Blackboard& bb);
  NodeStatus executeInverter(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeForceSuccess(const Node& node, Blackboard& bb);
  NodeStatus executeForceSuccess(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeForceFailure(const Node& node, Blackboard& bb);
  NodeStatus executeForceFailure(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeRepeat(const Node& node, Blackboard& bb);
  NodeStatus executeRepeat(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeTimeout(const Node& node, Blackboard& bb);
  NodeStatus executeTimeout(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeDelay(const Node& node, Blackboard& bb);
  NodeStatus executeDelay(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  
  // Advanced decorators (BT.CPP compatible)
  NodeStatus executeKeepRunningUntilFailure(const Node& node, Blackboard& bb);
  NodeStatus executeKeepRunningUntilFailure(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeRunOnce(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeConsumeQueue(const Node& node, Blackboard& bb);
  NodeStatus executeConsumeQueue(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  
  // Extended decorators
  NodeStatus executePrecondition(const Node& node, Blackboard& bb);
  NodeStatus executePrecondition(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeEntryUpdated(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  
  // Extended control flow
  NodeStatus executePipelineSequence(const Node& node, Blackboard& bb);
  NodeStatus executePipelineSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx);

  // Built-in control flow - advanced
  NodeStatus executeSwitch(const Node& node, Blackboard& bb);
  NodeStatus executeSwitch(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeWhileDo(const Node& node, Blackboard& bb);
  NodeStatus executeWhileDo(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeIfThenElse(const Node& node, Blackboard& bb);
  NodeStatus executeIfThenElse(const Node& node, Blackboard& bb, ExecutionContext& ctx);

  // SubTree support
  NodeStatus executeSubTree(const Node& node, Blackboard& bb);
  NodeStatus executeSubTree(const Node& node, Blackboard& bb, ExecutionContext& ctx);

  // Helper to resolve parameter values (handle blackboard variables)
  std::string resolveParam(const Value& value, Blackboard& bb);

  // Helper to get numeric parameter
  int getIntParam(const Node& node, const std::string& key, Blackboard& bb, int default_value = 0);
};

} // namespace actions
