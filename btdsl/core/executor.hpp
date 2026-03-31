#pragma once

#include "ast.hpp"
#include "btdsl/event_queue.hpp"
#include "btdsl/async_executor.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace btdsl {

enum class NodeStatus {
  SUCCESS,
  FAILURE,
  RUNNING
};

// Forward declarations
class ExecutionContext;

// Simple Blackboard for sharing data between nodes
class Blackboard {
public:
  void set(const std::string& key, const std::string& value);
  std::string get(const std::string& key) const;
  bool has(const std::string& key) const;

  // Event queue integration
  void triggerEvent(const std::string& event);
  bool pollEvent(const std::string& event);
  bool hasEvent(const std::string& event) const;
  EventQueue& getEventQueue() { return events_; }

private:
  std::map<std::string, std::string> data_;
  EventQueue events_;
};

// Task function signature: takes params and blackboard, returns status
using TaskFunction = std::function<NodeStatus(const std::map<std::string, Value>&, Blackboard&)>;

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

private:
  std::map<std::string, TaskFunction> tasks_;
  std::map<std::string, Tree> trees_;
  AsyncExecutor async_executor_;
  std::map<std::string, std::string> environment_;  // Environment variables for Shell nodes

  // Built-in control flow implementations
  NodeStatus executeSequence(const Node& node, Blackboard& bb);
  NodeStatus executeSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeFallback(const Node& node, Blackboard& bb);
  NodeStatus executeFallback(const Node& node, Blackboard& bb, ExecutionContext& ctx);
  NodeStatus executeParallel(const Node& node, Blackboard& bb);
  NodeStatus executeReactiveSequence(const Node& node, Blackboard& bb);
  NodeStatus executeReactiveFallback(const Node& node, Blackboard& bb);

  // Built-in decorator implementations
  NodeStatus executeRetry(const Node& node, Blackboard& bb);
  NodeStatus executeInverter(const Node& node, Blackboard& bb);
  NodeStatus executeForceSuccess(const Node& node, Blackboard& bb);
  NodeStatus executeForceFailure(const Node& node, Blackboard& bb);
  NodeStatus executeRepeat(const Node& node, Blackboard& bb);
  NodeStatus executeTimeout(const Node& node, Blackboard& bb);
  NodeStatus executeDelay(const Node& node, Blackboard& bb);

  // Built-in control flow - advanced
  NodeStatus executeSwitch(const Node& node, Blackboard& bb);
  NodeStatus executeWhileDo(const Node& node, Blackboard& bb);
  NodeStatus executeIfThenElse(const Node& node, Blackboard& bb);

  // SubTree support
  NodeStatus executeSubTree(const Node& node, Blackboard& bb);

  // Helper to resolve parameter values (handle blackboard variables)
  std::string resolveParam(const Value& value, Blackboard& bb);

  // Helper to get numeric parameter
  int getIntParam(const Node& node, const std::string& key, int default_value = 0);
};

} // namespace btdsl
