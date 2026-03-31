#include "core/executor.hpp"
#include "core/execution_context.hpp"
#include "btdsl/shell_executor.hpp"
#include "btdsl/output_parser.hpp"
#include <jsoncons/json.hpp>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <thread>

namespace btdsl {

// Forward declaration - WorkflowContext is defined in Praktor
class WorkflowContext;

// Resolve a Value to string, expanding blackboard variables like "{key}"
// Note: Mustache template support ({{ variable }}) will be added when WorkflowContext is available
static std::string resolveParamStatic(const Value& value, Blackboard& bb) {
  if (std::holds_alternative<std::string>(value)) {
    const std::string& s = std::get<std::string>(value);
    if (s.size() >= 3 && s.front() == '{' && s.back() == '}') {
      std::string key = s.substr(1, s.size() - 2);
      return bb.get(key);
    }
    return s;
  } else if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  } else if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return "";
}

// ============ Blackboard ============

void Blackboard::set(const std::string& key, const std::string& value) {
  data_[key] = value;
}

std::string Blackboard::get(const std::string& key) const {
  auto it = data_.find(key);
  if (it == data_.end()) {
    throw std::runtime_error("Blackboard key not found: " + key);
  }
  return it->second;
}

bool Blackboard::has(const std::string& key) const {
  return data_.find(key) != data_.end();
}

void Blackboard::triggerEvent(const std::string& event) {
  events_.push(event);
}

bool Blackboard::pollEvent(const std::string& event) {
  return events_.poll(event);
}

bool Blackboard::hasEvent(const std::string& event) const {
  return events_.peek(event);
}

// ============ Executor ============

Executor::Executor() {
  registerBuiltins();
}

void Executor::registerTask(const std::string& name, TaskFunction func) {
  tasks_[name] = func;
}

void Executor::registerBuiltins() {
  // Built-in control nodes are handled specially in execute()
  // We register them here just to mark them as valid node types
  tasks_["Sequence"] = nullptr;
  tasks_["Fallback"] = nullptr;
  tasks_["Parallel"] = nullptr;
  tasks_["ReactiveSequence"] = nullptr;
  tasks_["ReactiveFallback"] = nullptr;
  tasks_["Switch"] = nullptr;
  tasks_["WhileDo"] = nullptr;
  tasks_["IfThenElse"] = nullptr;

  // Decorators
  tasks_["Retry"] = nullptr;
  tasks_["Inverter"] = nullptr;
  tasks_["ForceSuccess"] = nullptr;
  tasks_["ForceFailure"] = nullptr;
  tasks_["Repeat"] = nullptr;
  tasks_["Timeout"] = nullptr;
  tasks_["Delay"] = nullptr;

  // SubTree
  tasks_["SubTree"] = nullptr;

  // Shell execution task
  registerTask("Shell", [this](const auto& params, Blackboard& bb) {
    std::string cmd;
    std::string input;
    std::string working_dir;
    std::string output_key = "shell_output";
    std::string stderr_key = "shell_stderr";
    std::string exit_code_key = "shell_exit_code";
    int timeout = 30000;
    bool stream_output = true;
    std::map<std::string, std::string> env_vars;

    // Start with executor's environment variables
    env_vars = environment_;

    // Extract parameters
    if (params.count("cmd")) {
      cmd = resolveParamStatic(params.at("cmd"), bb);
    }
    if (params.count("input")) {
      input = resolveParamStatic(params.at("input"), bb);
    }
    if (params.count("working_dir")) {
      working_dir = resolveParamStatic(params.at("working_dir"), bb);
    }
    if (params.count("output_key")) {
      output_key = std::string(std::get<std::string>(params.at("output_key")));
    }
    if (params.count("stderr_key")) {
      stderr_key = std::string(std::get<std::string>(params.at("stderr_key")));
    }
    if (params.count("exit_code_key")) {
      exit_code_key = std::string(std::get<std::string>(params.at("exit_code_key")));
    }
    if (params.count("timeout")) {
      timeout = static_cast<int>(std::get<double>(params.at("timeout")));
    }
    if (params.count("stream_output")) {
      if (std::holds_alternative<bool>(params.at("stream_output"))) {
        stream_output = std::get<bool>(params.at("stream_output"));
      } else {
        stream_output = resolveParamStatic(params.at("stream_output"), bb) != "false";
      }
    }

    // Extract node-level environment variables (if provided)
    // TODO: Parse env map from params when YAML parser supports it
    if (params.count("env")) {
      // Node-level env would override executor env
      // env_vars extraction will be implemented when needed
    }

    if (stream_output && !cmd.empty()) {
      ShellExecutor::emitStreamLine("$ " + cmd);
    }

    // Execute command with environment variables
    auto result = ShellExecutor::execute(cmd, input, working_dir, timeout, env_vars, stream_output);

    // Store results in blackboard
    bb.set(output_key, result.stdout_output);
    bb.set(stderr_key, result.stderr_output);
    bb.set(exit_code_key, std::to_string(result.exit_code));

    if (!result.success()) {
      bb.set("shell_error", result.stderr_output);
      return NodeStatus::FAILURE;
    }

    return NodeStatus::SUCCESS;
  });

  // Parse regex task
  registerTask("ParseRegex", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_output";
    std::string pattern;
    int capture_group = 1;
    std::string output_key = "parsed_output";

    if (params.count("input_key")) {
      input_key = std::string(std::get<std::string>(params.at("input_key")));
    }
    if (params.count("pattern")) {
      pattern = std::string(std::get<std::string>(params.at("pattern")));
    }
    if (params.count("capture_group")) {
      capture_group = static_cast<int>(std::get<double>(params.at("capture_group")));
    }
    if (params.count("output_key")) {
      output_key = std::string(std::get<std::string>(params.at("output_key")));
    }

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    std::string input = bb.get(input_key);
    std::vector<std::string> captures;

    if (OutputParser::parseRegex(input, pattern, captures)) {
      if (capture_group > 0 && capture_group <= static_cast<int>(captures.size())) {
        bb.set(output_key, captures[capture_group - 1]);
        return NodeStatus::SUCCESS;
      }
    }

    return NodeStatus::FAILURE;
  });

  // Parse JSON task
  registerTask("ParseJson", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_output";
    std::string path;
    std::string output_key = "parsed_output";

    if (params.count("input_key")) {
      input_key = std::string(std::get<std::string>(params.at("input_key")));
    }
    if (params.count("path")) {
      path = std::string(std::get<std::string>(params.at("path")));
    }
    if (params.count("output_key")) {
      output_key = std::string(std::get<std::string>(params.at("output_key")));
    }

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    std::string input = bb.get(input_key);
    std::string output;

    if (OutputParser::parseJson(input, path, output)) {
      bb.set(output_key, output);
      return NodeStatus::SUCCESS;
    }

    return NodeStatus::FAILURE;
  });

  // Parse lines task
  registerTask("ParseLines", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_output";
    std::string filter;
    std::string output_key = "parsed_lines";

    if (params.count("input_key")) {
      input_key = std::string(std::get<std::string>(params.at("input_key")));
    }
    if (params.count("filter")) {
      filter = std::string(std::get<std::string>(params.at("filter")));
    }
    if (params.count("output_key")) {
      output_key = std::string(std::get<std::string>(params.at("output_key")));
    }

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    std::string input = bb.get(input_key);
    auto lines = OutputParser::parseLines(input, filter);

    // Store line count
    bb.set(output_key + "_count", std::to_string(lines.size()));

    // Store lines (concatenated with newlines)
    std::string result;
    for (const auto& line : lines) {
      if (!result.empty()) result += "\n";
      result += line;
    }
    bb.set(output_key, result);

    return NodeStatus::SUCCESS;
  });

  // Parse key-value task
  registerTask("ParseKeyValue", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_output";
    std::string delimiter = "=";
    std::string line_separator = "\n";
    std::string output_key = "parsed_output";

    if (params.count("input_key")) {
      input_key = std::string(std::get<std::string>(params.at("input_key")));
    }
    if (params.count("delimiter")) {
      delimiter = std::string(std::get<std::string>(params.at("delimiter")));
    }
    if (params.count("line_separator")) {
      line_separator = std::string(std::get<std::string>(params.at("line_separator")));
    }
    if (params.count("output_key")) {
      output_key = std::string(std::get<std::string>(params.at("output_key")));
    }

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    std::string input = bb.get(input_key);
    auto parsed = OutputParser::parseKeyValue(input, delimiter, line_separator);

    jsoncons::json result = jsoncons::json::object();
    for (const auto& [key, value] : parsed) {
      result[key] = value;
    }

    bb.set(output_key, result.to_string());
    return NodeStatus::SUCCESS;
  });

  // Check exit code task
  registerTask("CheckExitCode", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_exit_code";
    int expected = 0;

    if (params.count("input_key")) {
      input_key = std::string(std::get<std::string>(params.at("input_key")));
    }
    if (params.count("expected")) {
      expected = static_cast<int>(std::get<double>(params.at("expected")));
    }

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    int exit_code = std::stoi(bb.get(input_key));
    return exit_code == expected ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
  });

  // Wait for event task
  registerTask("WaitEvent", [](const auto& params, Blackboard& bb) {
    std::string event = std::string(std::get<std::string>(params.at("event")));

    int timeout_ms = 30000;
    if (params.count("timeout")) {
      timeout_ms = static_cast<int>(std::get<double>(params.at("timeout")));
    }

    auto start = std::chrono::steady_clock::now();

    while (true) {
      if (bb.pollEvent(event)) {
        return NodeStatus::SUCCESS;
      }

      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
      ).count();

      if (elapsed > timeout_ms) {
        return NodeStatus::FAILURE;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // Sleep task - explicit delay without requiring a child node
  registerTask("Sleep", [](const auto& params, Blackboard& bb) {
    int duration_ms = 0;

    if (params.count("duration")) {
      const auto& val = params.at("duration");
      if (std::holds_alternative<double>(val)) {
        duration_ms = static_cast<int>(std::get<double>(val));
      } else if (std::holds_alternative<std::string>(val)) {
        // Parse duration strings: "5s", "500ms", "2m", "1h"
        std::string s = std::get<std::string>(val);
        size_t pos;
        double n = std::stod(s, &pos);
        std::string unit = s.substr(pos);
        if (unit == "ms")       duration_ms = static_cast<int>(n);
        else if (unit == "s")   duration_ms = static_cast<int>(n * 1000);
        else if (unit == "m")   duration_ms = static_cast<int>(n * 60000);
        else if (unit == "h")   duration_ms = static_cast<int>(n * 3600000);
        else                    duration_ms = static_cast<int>(n);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    return NodeStatus::SUCCESS;
  });

  // FileExists task - check if a file or directory exists
  registerTask("FileExists", [](const auto& params, Blackboard& bb) {
    if (!params.count("path")) {
      return NodeStatus::FAILURE;
    }

    std::string path = resolveParamStatic(params.at("path"), bb);
    std::string output_key;
    if (params.count("output_key")) {
      output_key = std::get<std::string>(params.at("output_key"));
    }

    bool exists = std::filesystem::exists(path);

    if (!output_key.empty()) {
      bb.set(output_key, exists ? "true" : "false");
    }

    bool fail_if_missing = true;
    if (params.count("fail_if_missing")) {
      const auto& v = params.at("fail_if_missing");
      if (std::holds_alternative<bool>(v)) {
        fail_if_missing = std::get<bool>(v);
      }
    }

    if (!exists && !fail_if_missing) {
      return NodeStatus::SUCCESS;
    }

    return exists ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
  });

  // SetVariable task - write a value to the blackboard
  registerTask("SetVariable", [](const auto& params, Blackboard& bb) {
    if (!params.count("key")) {
      return NodeStatus::FAILURE;
    }

    std::string key = std::get<std::string>(params.at("key"));

    if (params.count("value")) {
      bb.set(key, resolveParamStatic(params.at("value"), bb));
    } else if (params.count("from_context")) {
      // Copy value from another blackboard key
      std::string src_key = resolveParamStatic(params.at("from_context"), bb);
      if (!bb.has(src_key)) {
        return NodeStatus::FAILURE;
      }
      bb.set(key, bb.get(src_key));
    } else {
      return NodeStatus::FAILURE;
    }

    return NodeStatus::SUCCESS;
  });
}

void Executor::registerTree(const Tree& tree) {
  trees_[tree.name] = tree;
}

const Tree* Executor::getTree(const std::string& name) const {
  auto it = trees_.find(name);
  if (it == trees_.end()) {
    return nullptr;
  }
  return &it->second;
}

NodeStatus Executor::execute(const Tree& tree, Blackboard& bb) {
  return execute(tree.root, bb);
}

NodeStatus Executor::execute(const Tree& tree, Blackboard& bb, ExecutionContext& ctx) {
  return execute(tree.root, bb, ctx);
}

NodeStatus Executor::execute(const Node& node, Blackboard& bb) {
  // Handle built-in control flow nodes
  if (node.id == "Sequence") {
    return executeSequence(node, bb);
  } else if (node.id == "Fallback") {
    return executeFallback(node, bb);
  } else if (node.id == "Parallel") {
    return executeParallel(node, bb);
  } else if (node.id == "ReactiveSequence") {
    return executeReactiveSequence(node, bb);
  } else if (node.id == "ReactiveFallback") {
    return executeReactiveFallback(node, bb);
  }

  // Handle decorators
  if (node.id == "Retry") {
    return executeRetry(node, bb);
  } else if (node.id == "Inverter") {
    return executeInverter(node, bb);
  } else if (node.id == "ForceSuccess") {
    return executeForceSuccess(node, bb);
  } else if (node.id == "ForceFailure") {
    return executeForceFailure(node, bb);
  } else if (node.id == "Repeat") {
    return executeRepeat(node, bb);
  } else if (node.id == "Timeout") {
    return executeTimeout(node, bb);
  } else if (node.id == "Delay") {
    return executeDelay(node, bb);
  }

  // Handle advanced control flow
  if (node.id == "Switch") {
    return executeSwitch(node, bb);
  } else if (node.id == "WhileDo") {
    return executeWhileDo(node, bb);
  } else if (node.id == "IfThenElse") {
    return executeIfThenElse(node, bb);
  }

  // Handle SubTree
  if (node.id == "SubTree") {
    return executeSubTree(node, bb);
  }

  // Look up task
  auto it = tasks_.find(node.id);
  if (it == tasks_.end()) {
    throw std::runtime_error("Task not registered: " + node.id);
  }

  if (it->second == nullptr) {
    throw std::runtime_error("Built-in node used incorrectly: " + node.id);
  }

  // Execute task
  return it->second(node.params, bb);
}

NodeStatus Executor::execute(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  // Generate unique node ID for state tracking
  std::string node_id = node.id + "_" + std::to_string(reinterpret_cast<uintptr_t>(&node));

  // Handle built-in control flow nodes with state
  if (node.id == "Sequence") {
    return executeSequence(node, bb, ctx);
  } else if (node.id == "Fallback") {
    return executeFallback(node, bb, ctx);
  }

  // For other nodes, use stateless execution
  return execute(node, bb);
}

NodeStatus Executor::executeSequence(const Node& node, Blackboard& bb) {
  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb);
    if (status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  // Generate unique node ID for state tracking
  std::string node_id = "Sequence_" + std::to_string(reinterpret_cast<uintptr_t>(&node));

  NodeState& state = ctx.getState(node_id);

  // Start from where we left off
  for (size_t i = state.current_child; i < node.children.size(); i++) {
    NodeStatus status = execute(node.children[i], bb, ctx);

    if (status == NodeStatus::FAILURE) {
      ctx.reset(node_id);  // Reset state on failure
      return NodeStatus::FAILURE;
    }

    if (status == NodeStatus::RUNNING) {
      state.current_child = i;  // Save progress
      return NodeStatus::RUNNING;
    }

    // SUCCESS: continue to next child
  }

  // All children succeeded
  ctx.reset(node_id);  // Reset state on completion
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeFallback(const Node& node, Blackboard& bb) {
  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb);
    if (status == NodeStatus::SUCCESS) {
      return NodeStatus::SUCCESS;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeFallback(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  // Generate unique node ID for state tracking
  std::string node_id = "Fallback_" + std::to_string(reinterpret_cast<uintptr_t>(&node));

  NodeState& state = ctx.getState(node_id);

  // Start from where we left off
  for (size_t i = state.current_child; i < node.children.size(); i++) {
    NodeStatus status = execute(node.children[i], bb, ctx);

    if (status == NodeStatus::SUCCESS) {
      ctx.reset(node_id);  // Reset state on success
      return NodeStatus::SUCCESS;
    }

    if (status == NodeStatus::RUNNING) {
      state.current_child = i;  // Save progress
      return NodeStatus::RUNNING;
    }

    // FAILURE: continue to next child
  }

  // All children failed
  ctx.reset(node_id);  // Reset state on completion
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeParallel(const Node& node, Blackboard& bb) {
  int success_count = 0;
  int failure_count = 0;
  int running_count = 0;

  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb);
    if (status == NodeStatus::SUCCESS) {
      success_count++;
    } else if (status == NodeStatus::FAILURE) {
      failure_count++;
    } else if (status == NodeStatus::RUNNING) {
      running_count++;
    }
  }

  // Simple parallel logic: succeed if all succeed, fail if any fail
  if (running_count > 0) {
    return NodeStatus::RUNNING;
  }
  if (failure_count > 0) {
    return NodeStatus::FAILURE;
  }
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeReactiveSequence(const Node& node, Blackboard& bb) {
  // Reactive sequence re-evaluates all previous children
  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb);
    if (status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeReactiveFallback(const Node& node, Blackboard& bb) {
  // Reactive fallback re-evaluates all previous children
  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb);
    if (status == NodeStatus::SUCCESS) {
      return NodeStatus::SUCCESS;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }
  return NodeStatus::FAILURE;
}

// ============ Decorators ============

NodeStatus Executor::executeRetry(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Retry must have exactly 1 child");
  }

  int num_attempts = getIntParam(node, "num_attempts", 3);

  for (int i = 0; i < num_attempts; ++i) {
    NodeStatus status = execute(node.children[0], bb);
    if (status == NodeStatus::SUCCESS) {
      return NodeStatus::SUCCESS;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
    // On failure, retry
  }

  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeInverter(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Inverter must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb);
  if (status == NodeStatus::SUCCESS) {
    return NodeStatus::FAILURE;
  } else if (status == NodeStatus::FAILURE) {
    return NodeStatus::SUCCESS;
  }
  return NodeStatus::RUNNING;
}

NodeStatus Executor::executeForceSuccess(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ForceSuccess must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb);
  if (status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeForceFailure(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ForceFailure must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb);
  if (status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeRepeat(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Repeat must have exactly 1 child");
  }

  int num_cycles = getIntParam(node, "num_cycles", -1);

  if (num_cycles < 0) {
    // Infinite repeat
    while (true) {
      NodeStatus status = execute(node.children[0], bb);
      if (status == NodeStatus::RUNNING) {
        return NodeStatus::RUNNING;
      }
      // Continue repeating regardless of success/failure
    }
  } else {
    // Finite repeat
    for (int i = 0; i < num_cycles; ++i) {
      NodeStatus status = execute(node.children[0], bb);
      if (status == NodeStatus::RUNNING) {
        return NodeStatus::RUNNING;
      }
    }
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeTimeout(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Timeout must have exactly 1 child");
  }

  int timeout_ms = getIntParam(node, "timeout_ms", 1000);

  // Note: This is a simplified implementation
  // In a real system, you'd use async execution with timers
  auto start = std::chrono::steady_clock::now();

  NodeStatus status = execute(node.children[0], bb);

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start).count();

  if (elapsed > timeout_ms && status == NodeStatus::RUNNING) {
    return NodeStatus::FAILURE;
  }

  return status;
}

NodeStatus Executor::executeDelay(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Delay must have exactly 1 child");
  }

  int delay_ms = getIntParam(node, "delay_ms", 100);

  // Simple delay implementation
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  return execute(node.children[0], bb);
}

// ============ Advanced Control Flow ============

NodeStatus Executor::executeSwitch(const Node& node, Blackboard& bb) {
  // Switch(variable="{key}") { case1 case2 case3 }
  // Executes the child at index matching the variable value

  auto it = node.params.find("variable");
  if (it == node.params.end()) {
    throw std::runtime_error("Switch requires 'variable' parameter");
  }

  std::string var_value = resolveParam(it->second, bb);
  int index = 0;

  try {
    index = std::stoi(var_value);
  } catch (const std::exception& e) {
    throw std::runtime_error("Switch variable must be numeric: " + var_value + " (" + e.what() + ")");
  }

  if (index < 0 || index >= static_cast<int>(node.children.size())) {
    // Out of range, return failure
    return NodeStatus::FAILURE;
  }

  return execute(node.children[index], bb);
}

NodeStatus Executor::executeWhileDo(const Node& node, Blackboard& bb) {
  // WhileDo { condition action }
  if (node.children.size() != 2) {
    throw std::runtime_error("WhileDo must have exactly 2 children (condition, action)");
  }

  int max_iterations = getIntParam(node, "max_iterations", 100);
  int iteration = 0;

  while (iteration < max_iterations) {
    NodeStatus condition_status = execute(node.children[0], bb);

    if (condition_status == NodeStatus::FAILURE) {
      return NodeStatus::SUCCESS; // Condition false, exit loop successfully
    }

    if (condition_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    // Condition is SUCCESS, execute action
    NodeStatus action_status = execute(node.children[1], bb);

    if (action_status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }

    if (action_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    iteration++;
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeIfThenElse(const Node& node, Blackboard& bb) {
  // IfThenElse { condition then_branch else_branch }
  if (node.children.size() < 2 || node.children.size() > 3) {
    throw std::runtime_error("IfThenElse must have 2 or 3 children (condition, then, [else])");
  }

  NodeStatus condition_status = execute(node.children[0], bb);

  if (condition_status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }

  if (condition_status == NodeStatus::SUCCESS) {
    // Execute then branch
    return execute(node.children[1], bb);
  } else {
    // Execute else branch if it exists
    if (node.children.size() == 3) {
      return execute(node.children[2], bb);
    }
    return NodeStatus::FAILURE;
  }
}

// ============ SubTree ============

NodeStatus Executor::executeSubTree(const Node& node, Blackboard& bb) {
  // Get tree ID from parameters
  auto it = node.params.find("ID");
  if (it == node.params.end()) {
    throw std::runtime_error("SubTree requires 'ID' parameter");
  }

  std::string tree_id = std::get<std::string>(it->second);
  const Tree* subtree = getTree(tree_id);
  if (subtree == nullptr) {
    throw std::runtime_error("SubTree not found: " + tree_id);
  }

  // Execute the subtree
  return execute(subtree->root, bb);
}

// ============ Helpers ============

int Executor::getIntParam(const Node& node, const std::string& key, int default_value) {
  auto it = node.params.find(key);
  if (it == node.params.end()) {
    return default_value;
  }

  if (std::holds_alternative<double>(it->second)) {
    return static_cast<int>(std::get<double>(it->second));
  }

  return default_value;
}

std::string Executor::resolveParam(const Value& value, Blackboard& bb) {
  return resolveParamStatic(value, bb);
}

} // namespace btdsl
