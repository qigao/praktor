#include "core/executor.hpp"
#include "core/execution_context.hpp"
#include "actions/shell_executor.hpp"
#include "actions/output_parser.hpp"
#include <json_parser.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace actions {

namespace {

struct JsonDeleter {
  void operator()(json_value_t* value) const noexcept {
        json_free(value);
  }
};

struct SerializedJsonDeleter {
    void operator()(char* text) const noexcept { json_serialize_free(text); }
};

using OwnedJson = std::unique_ptr<json_value_t, JsonDeleter>;
using OwnedSerializedJson = std::unique_ptr<char, SerializedJsonDeleter>;

std::string serializeJson(const json_value_t* value) {
  size_t size = 0;
    OwnedSerializedJson text(json_serialize(value, &size));
  if (!text) {
    throw std::runtime_error("Failed to serialize JSON value");
  }
  return std::string(text.get(), size);
}

bool consumeJsonArrayFront(std::string_view input, std::string& item, std::string& remaining,
                           bool& has_more) {
    json_value_t* parsed_document = json_parse(input.data(), input.size());
    if (!parsed_document) {
    return false;
  }
  OwnedJson document(reinterpret_cast<json_value_t*>(parsed_document));
    if (json_type(document.get()) != JSON_ARRAY || json_array_size(document.get()) == 0) {
    return false;
  }

    item = serializeJson(json_array_get(document.get(), 0));
    OwnedJson tail(json_create_array());
  if (!tail) {
    throw std::bad_alloc();
  }
    for (size_t index = 1; index < json_array_size(document.get()); ++index) {
        OwnedJson child(json_clone(json_array_get(document.get(), index)));
        if (!child || !json_array_add_checked(tail.get(), child.get())) {
      throw std::runtime_error("Failed to rebuild JSON queue");
    }
    child.release();
  }
  remaining = serializeJson(tail.get());
    has_more = json_array_size(tail.get()) != 0;
  return true;
}

} // namespace

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

static bool isTruthyValue(const std::string& value) {
  std::string normalized = value;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  return !(normalized.empty() ||
           normalized == "0" ||
           normalized == "false" ||
           normalized == "no" ||
           normalized == "off" ||
           normalized == "null");
}

static bool evaluateConditionValue(const Value& value, Blackboard& bb) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  if (std::holds_alternative<double>(value)) {
    return std::fpclassify(std::get<double>(value)) != FP_ZERO;
  }
  return isTruthyValue(resolveParamStatic(value, bb));
}

static std::string describeValueForError(const Value& value, Blackboard& bb) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  }
  return resolveParamStatic(value, bb);
}

static int parseStrictIntText(const std::string& raw,
                              const std::string& key,
                              const std::string& owner) {
  try {
    size_t pos = 0;
    long long parsed = std::stoll(raw, &pos, 10);
    if (pos != raw.size()) {
      throw std::invalid_argument("trailing characters");
    }
    if (parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max()) {
      throw std::out_of_range("out of int range");
    }
    return static_cast<int>(parsed);
  } catch (const std::exception&) {
  }

  try {
    size_t pos = 0;
    double parsed = std::stod(raw, &pos);
    double integral_part = 0.0;
    double fractional_part = std::modf(parsed, &integral_part);
    if (pos != raw.size() ||
        !std::isfinite(parsed) ||
        std::fpclassify(fractional_part) != FP_ZERO ||
        parsed < static_cast<double>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<double>(std::numeric_limits<int>::max())) {
      throw std::invalid_argument("not an integral numeric string");
    }
    return static_cast<int>(parsed);
  } catch (const std::exception&) {
    throw std::runtime_error(
      "Parameter '" + key + "' on node '" + owner + "' must be an integer, got '" + raw + "'");
  }
}

static int parseStrictIntValue(const Value& value,
                               Blackboard& bb,
                               const std::string& key,
                               const std::string& owner) {
  if (std::holds_alternative<double>(value)) {
    const double numeric = std::get<double>(value);
    double integral_part = 0.0;
    double fractional_part = std::modf(numeric, &integral_part);
    if (!std::isfinite(numeric) || std::fpclassify(fractional_part) != FP_ZERO ||
        numeric < static_cast<double>(std::numeric_limits<int>::min()) ||
        numeric > static_cast<double>(std::numeric_limits<int>::max())) {
      throw std::runtime_error(
        "Parameter '" + key + "' on node '" + owner + "' must be an integer, got '" +
        describeValueForError(value, bb) + "'");
    }
    return static_cast<int>(numeric);
  }

  if (std::holds_alternative<bool>(value)) {
    throw std::runtime_error(
      "Parameter '" + key + "' on node '" + owner + "' must be an integer, got '" +
      describeValueForError(value, bb) + "'");
  }

  return parseStrictIntText(resolveParamStatic(value, bb), key, owner);
}

static int getTaskIntParam(const std::map<std::string, Value>& params,
                           Blackboard& bb,
                           const std::string& owner,
                           const std::string& key,
                           int default_value) {
  auto it = params.find(key);
  if (it == params.end()) {
    return default_value;
  }
  return parseStrictIntValue(it->second, bb, key, owner);
}

static bool parseStrictBoolText(const std::string& raw,
                                const std::string& key,
                                const std::string& owner) {
  std::string normalized = raw;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (normalized == "true") {
    return true;
  }
  if (normalized == "false") {
    return false;
  }
  throw std::runtime_error(
    "Parameter '" + key + "' on node '" + owner + "' must be a boolean, got '" + raw + "'");
}

static bool parseStrictBoolValue(const Value& value,
                                 Blackboard& bb,
                                 const std::string& key,
                                 const std::string& owner) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  if (std::holds_alternative<double>(value)) {
    throw std::runtime_error(
      "Parameter '" + key + "' on node '" + owner + "' must be a boolean, got '" +
      describeValueForError(value, bb) + "'");
  }
  return parseStrictBoolText(resolveParamStatic(value, bb), key, owner);
}

static bool getTaskBoolParam(const std::map<std::string, Value>& params,
                             Blackboard& bb,
                             const std::string& owner,
                             const std::string& key,
                             bool default_value) {
  auto it = params.find(key);
  if (it == params.end()) {
    return default_value;
  }
  return parseStrictBoolValue(it->second, bb, key, owner);
}

static std::string getTaskStringParam(const std::map<std::string, Value>& params,
                                      Blackboard& bb,
                                      const std::string& key,
                                      const std::string& default_value = "") {
  auto it = params.find(key);
  if (it == params.end()) {
    return default_value;
  }
  return resolveParamStatic(it->second, bb);
}

static int resolveSwitchIndex(const Value& value, Blackboard& bb) {
  if (std::holds_alternative<std::string>(value)) {
    const std::string& raw = std::get<std::string>(value);
    if (raw.size() < 3 || raw.front() != '{' || raw.back() != '}') {
      if (bb.has(raw)) {
        return parseStrictIntText(bb.get(raw), "variable", "Switch");
      }
    }
  }
  return parseStrictIntValue(value, bb, "variable", "Switch");
}

// ============ Blackboard ============

void Blackboard::set(const std::string& key, const std::string& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  data_[key] = value;
}

std::string Blackboard::get(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = data_.find(key);
  if (it == data_.end()) {
    throw std::runtime_error("Blackboard key not found: " + key);
  }
  return it->second;
}

bool Blackboard::has(const std::string& key) const {
  std::lock_guard<std::mutex> lock(mutex_);
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
  dispatch_["Sequence"] = [this](const Node& node, Blackboard& bb) {
    return executeSequence(node, bb);
  };
  dispatch_["Fallback"] = [this](const Node& node, Blackboard& bb) {
    return executeFallback(node, bb);
  };
  dispatch_["Selector"] = dispatch_["Fallback"];
  dispatch_["Parallel"] = [this](const Node& node, Blackboard& bb) {
    return executeParallel(node, bb);
  };
  dispatch_["ReactiveSequence"] = [this](const Node& node, Blackboard& bb) {
    return executeReactiveSequence(node, bb);
  };
  dispatch_["PipelineSequence"] = [this](const Node& node, Blackboard& bb) {
    return executePipelineSequence(node, bb);
  };
  dispatch_["Inverter"] = [this](const Node& node, Blackboard& bb) {
    return executeInverter(node, bb);
  };
  dispatch_["ForceSuccess"] = [this](const Node& node, Blackboard& bb) {
    return executeForceSuccess(node, bb);
  };
  dispatch_["ForceFailure"] = [this](const Node& node, Blackboard& bb) {
    return executeForceFailure(node, bb);
  };
  dispatch_["Repeat"] = [this](const Node& node, Blackboard& bb) {
    return executeRepeat(node, bb);
  };
  dispatch_["Timeout"] = [this](const Node& node, Blackboard& bb) {
    return executeTimeout(node, bb);
  };
  dispatch_["Delay"] = [this](const Node& node, Blackboard& bb) {
    return executeDelay(node, bb);
  };
  dispatch_["KeepRunningUntilFailure"] = [this](const Node& node, Blackboard& bb) {
    return executeKeepRunningUntilFailure(node, bb);
  };
  dispatch_["ConsumeQueue"] = [this](const Node& node, Blackboard& bb) {
    return executeConsumeQueue(node, bb);
  };
  dispatch_["Precondition"] = [this](const Node& node, Blackboard& bb) {
    return executePrecondition(node, bb);
  };
  dispatch_["Switch"] = [this](const Node& node, Blackboard& bb) {
    return executeSwitch(node, bb);
  };
  dispatch_["WhileDo"] = [this](const Node& node, Blackboard& bb) {
    return executeWhileDo(node, bb);
  };
  dispatch_["IfThenElse"] = [this](const Node& node, Blackboard& bb) {
    return executeIfThenElse(node, bb);
  };
  dispatch_["SubTree"] = [this](const Node& node, Blackboard& bb) {
    return executeSubTree(node, bb);
  };

  stateful_dispatch_["Sequence"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeSequence(node, bb, ctx);
  };
  stateful_dispatch_["Fallback"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeFallback(node, bb, ctx);
  };
  stateful_dispatch_["Selector"] = stateful_dispatch_["Fallback"];
  stateful_dispatch_["Parallel"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeParallel(node, bb, ctx);
  };
  stateful_dispatch_["ReactiveSequence"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeReactiveSequence(node, bb, ctx);
  };
  stateful_dispatch_["PipelineSequence"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executePipelineSequence(node, bb, ctx);
  };
  stateful_dispatch_["Inverter"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeInverter(node, bb, ctx);
  };
  stateful_dispatch_["ForceSuccess"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeForceSuccess(node, bb, ctx);
  };
  stateful_dispatch_["ForceFailure"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeForceFailure(node, bb, ctx);
  };
  stateful_dispatch_["Repeat"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeRepeat(node, bb, ctx);
  };
  stateful_dispatch_["Timeout"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeTimeout(node, bb, ctx);
  };
  stateful_dispatch_["Delay"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeDelay(node, bb, ctx);
  };
  stateful_dispatch_["KeepRunningUntilFailure"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeKeepRunningUntilFailure(node, bb, ctx);
  };
  stateful_dispatch_["RunOnce"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeRunOnce(node, bb, ctx);
  };
  stateful_dispatch_["ConsumeQueue"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeConsumeQueue(node, bb, ctx);
  };
  stateful_dispatch_["Precondition"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executePrecondition(node, bb, ctx);
  };
  stateful_dispatch_["EntryUpdated"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeEntryUpdated(node, bb, ctx);
  };
  stateful_dispatch_["Switch"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeSwitch(node, bb, ctx);
  };
  stateful_dispatch_["WhileDo"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeWhileDo(node, bb, ctx);
  };
  stateful_dispatch_["IfThenElse"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeIfThenElse(node, bb, ctx);
  };
  stateful_dispatch_["SubTree"] = [this](const Node& node, Blackboard& bb, ExecutionContext& ctx) {
    return executeSubTree(node, bb, ctx);
  };

  // Built-in node ids stay in tasks_ with null handlers so misuse still fails loudly.
  tasks_["Sequence"] = nullptr;
  tasks_["Fallback"] = nullptr;
  tasks_["Selector"] = nullptr;
  tasks_["Parallel"] = nullptr;
  tasks_["ReactiveSequence"] = nullptr;
  tasks_["Switch"] = nullptr;
  tasks_["WhileDo"] = nullptr;
  tasks_["IfThenElse"] = nullptr;

  // Decorators
  tasks_["Inverter"] = nullptr;
  tasks_["ForceSuccess"] = nullptr;
  tasks_["ForceFailure"] = nullptr;
  tasks_["Repeat"] = nullptr;
  tasks_["Timeout"] = nullptr;
  tasks_["Delay"] = nullptr;
  
  // Advanced decorators (BT.CPP compatible)
  tasks_["KeepRunningUntilFailure"] = nullptr;
  tasks_["RunOnce"] = nullptr;
  tasks_["ConsumeQueue"] = nullptr;
  
  // Extended decorators
  tasks_["Precondition"] = nullptr;
  tasks_["EntryUpdated"] = nullptr;
  
  // Extended control flow
  tasks_["PipelineSequence"] = nullptr;

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
    cmd = getTaskStringParam(params, bb, "cmd");
    input = getTaskStringParam(params, bb, "input");
    working_dir = getTaskStringParam(params, bb, "working_dir");
    output_key = getTaskStringParam(params, bb, "output_key", output_key);
    stderr_key = getTaskStringParam(params, bb, "stderr_key", stderr_key);
    exit_code_key = getTaskStringParam(params, bb, "exit_code_key", exit_code_key);
    timeout = getTaskIntParam(params, bb, "Shell", "timeout", timeout);
    stream_output = getTaskBoolParam(params, bb, "Shell", "stream_output", stream_output);

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

    input_key = getTaskStringParam(params, bb, "input_key", input_key);
    pattern = getTaskStringParam(params, bb, "pattern");
    capture_group = getTaskIntParam(params, bb, "ParseRegex", "capture_group", capture_group);
    output_key = getTaskStringParam(params, bb, "output_key", output_key);

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

    input_key = getTaskStringParam(params, bb, "input_key", input_key);
    path = getTaskStringParam(params, bb, "path");
    output_key = getTaskStringParam(params, bb, "output_key", output_key);

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

    input_key = getTaskStringParam(params, bb, "input_key", input_key);
    filter = getTaskStringParam(params, bb, "filter");
    output_key = getTaskStringParam(params, bb, "output_key", output_key);

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

    input_key = getTaskStringParam(params, bb, "input_key", input_key);
    delimiter = getTaskStringParam(params, bb, "delimiter", delimiter);
    line_separator = getTaskStringParam(params, bb, "line_separator", line_separator);
    output_key = getTaskStringParam(params, bb, "output_key", output_key);

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    std::string input = bb.get(input_key);
    auto parsed = OutputParser::parseKeyValue(input, delimiter, line_separator);

    json_value_t* result = json_create_object();
    if (!result) {
      return NodeStatus::FAILURE;
    }
    for (const auto& [key, value] : parsed) {
      json_object_set_string(result, key.c_str(), value.c_str());
    }

    bb.set(output_key, serializeJson(result));
    json_free(result);
    return NodeStatus::SUCCESS;
  });

  // Check exit code task
  registerTask("CheckExitCode", [](const auto& params, Blackboard& bb) {
    std::string input_key = "shell_exit_code";
    int expected = 0;

    input_key = getTaskStringParam(params, bb, "input_key", input_key);
    expected = getTaskIntParam(params, bb, "CheckExitCode", "expected", expected);

    if (!bb.has(input_key)) {
      return NodeStatus::FAILURE;
    }

    int exit_code = parseStrictIntText(bb.get(input_key), input_key, "CheckExitCode");
    return exit_code == expected ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
  });

  // Wait for event task
  registerTask("WaitEvent", [](const auto& params, Blackboard& bb) {
    std::string event = getTaskStringParam(params, bb, "event");

    int timeout_ms = getTaskIntParam(params, bb, "WaitEvent", "timeout", 30000);

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
      } else {
        // Parse duration strings: "5s", "500ms", "2m", "1h"
        std::string s = resolveParamStatic(val, bb);
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
    output_key = getTaskStringParam(params, bb, "output_key");

    bool exists = std::filesystem::exists(path);

    if (!output_key.empty()) {
      bb.set(output_key, exists ? "true" : "false");
    }

    bool fail_if_missing = getTaskBoolParam(
      params, bb, "FileExists", "fail_if_missing", true);

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

    std::string key = getTaskStringParam(params, bb, "key");

    if (params.count("value")) {
      bb.set(key, resolveParamStatic(params.at("value"), bb));
    } else if (params.count("from")) {
      // Copy value from another blackboard key
      std::string src_key = resolveParamStatic(params.at("from"), bb);
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
  return execute(tree.root, bb, default_context_);
}

NodeStatus Executor::execute(const Tree& tree, Blackboard& bb, ExecutionContext& ctx) {
  return execute(tree.root, bb, ctx);
}

NodeStatus Executor::execute(const Node& node, Blackboard& bb) {
  return execute(node, bb, default_context_);
}

NodeStatus Executor::execute(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  // Start monitoring if enabled
  auto start_time = monitoring_enabled_ ? std::chrono::high_resolution_clock::now() 
                                        : std::chrono::high_resolution_clock::time_point{};
  
  NodeStatus status;

  auto dispatch_it = stateful_dispatch_.find(node.id);
  if (dispatch_it != stateful_dispatch_.end()) {
    status = dispatch_it->second(node, bb, ctx);
  } else {
    auto stateless_dispatch_it = dispatch_.find(node.id);
    if (stateless_dispatch_it != dispatch_.end()) {
      status = stateless_dispatch_it->second(node, bb);
    } else {
      auto it = tasks_.find(node.id);
      if (it == tasks_.end()) {
        throw std::runtime_error("Task not registered: " + node.id);
      }

      if (it->second == nullptr) {
        throw std::runtime_error("Built-in node used incorrectly: " + node.id);
      }

      status = it->second(node.params, bb);
    }
  }
  
  // Record metrics if monitoring enabled
  if (monitoring_enabled_) {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    monitor_.recordExecution(node.id, status, duration);
  }
  
  return status;
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
  std::string node_id = "Fallback_" + std::to_string(reinterpret_cast<uintptr_t>(&node));
  NodeState& state = ctx.getState(node_id);

  for (size_t i = state.current_child; i < node.children.size(); i++) {
    NodeStatus status = execute(node.children[i], bb, ctx);

    if (status == NodeStatus::SUCCESS) {
      ctx.reset(node_id);
      return NodeStatus::SUCCESS;
    }

    if (status == NodeStatus::RUNNING) {
      state.current_child = i;
      return NodeStatus::RUNNING;
    }
  }

  ctx.reset(node_id);
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeParallel(const Node& node, Blackboard& bb) {
  // Each branch gets its own blackboard copy to prevent concurrent write collisions.
  // Results (output keys) are merged back into the shared bb after all branches finish.
  const size_t n = node.children.size();
  std::vector<Blackboard> branch_bbs(n);
  std::vector<std::shared_ptr<AsyncTask>> tasks;
  tasks.reserve(n);

  // Snapshot shared state into each branch before launching
  for (size_t i = 0; i < n; ++i) {
    branch_bbs[i] = bb;  // Blackboard copy ctor copies data_ map
  }

  for (size_t i = 0; i < n; ++i) {
    tasks.push_back(async_executor_.submit([this, &branch_bbs, i, child_copy = node.children[i]]() mutable {
      return execute(child_copy, branch_bbs[i]);
    }));
  }

  bool saw_running = false;
  bool saw_failure = false;

  for (size_t i = 0; i < tasks.size(); ++i) {
    NodeStatus status = tasks[i]->getResult();
    if (status == NodeStatus::RUNNING) {
      saw_running = true;
    } else if (status == NodeStatus::FAILURE) {
      saw_failure = true;
    }
    // Merge branch writes back to the shared blackboard (last-writer-wins per key)
    // This runs sequentially after getResult(), so no lock needed here.
    branch_bbs[i].mergeInto(bb);
  }

  async_executor_.cleanup();

  if (saw_running) {
    return NodeStatus::RUNNING;
  }
  if (saw_failure) {
    return NodeStatus::FAILURE;
  }
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeParallel(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  (void)ctx;

  const size_t n = node.children.size();
  std::vector<Blackboard> branch_bbs(n);
  std::vector<std::shared_ptr<AsyncTask>> tasks;
  tasks.reserve(n);

  for (size_t i = 0; i < n; ++i) {
    branch_bbs[i] = bb;
  }

  for (size_t i = 0; i < n; ++i) {
    tasks.push_back(async_executor_.submit([this, &branch_bbs, i, child_copy = node.children[i]]() mutable {
      ExecutionContext child_ctx;
      return execute(child_copy, branch_bbs[i], child_ctx);
    }));
  }

  bool saw_running = false;
  bool saw_failure = false;

  for (size_t i = 0; i < tasks.size(); ++i) {
    NodeStatus status = tasks[i]->getResult();
    if (status == NodeStatus::RUNNING) {
      saw_running = true;
    } else if (status == NodeStatus::FAILURE) {
      saw_failure = true;
    }
    branch_bbs[i].mergeInto(bb);
  }

  async_executor_.cleanup();

  if (saw_running) {
    return NodeStatus::RUNNING;
  }
  if (saw_failure) {
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

NodeStatus Executor::executeReactiveSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  for (const auto& child : node.children) {
    NodeStatus status = execute(child, bb, ctx);
    if (status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }
  return NodeStatus::SUCCESS;
}

// ============ Decorators ============

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

NodeStatus Executor::executeInverter(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Inverter must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb, ctx);
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

NodeStatus Executor::executeForceSuccess(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ForceSuccess must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb, ctx);
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

NodeStatus Executor::executeForceFailure(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ForceFailure must have exactly 1 child");
  }

  NodeStatus status = execute(node.children[0], bb, ctx);
  if (status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeRepeat(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Repeat must have exactly 1 child");
  }

  int num_cycles = getIntParam(node, "num_cycles", bb, -1);
  if (num_cycles < 0) {
    throw std::runtime_error(
      "Repeat requires a finite 'num_cycles' in the synchronous executor");
  }

  // Finite repeat
  for (int i = 0; i < num_cycles; ++i) {
    NodeStatus status = execute(node.children[0], bb);
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeRepeat(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Repeat must have exactly 1 child");
  }

  int num_cycles = getIntParam(node, "num_cycles", bb, -1);
  if (num_cycles < 0) {
    throw std::runtime_error(
      "Repeat requires a finite 'num_cycles' in the synchronous executor");
  }

  for (int i = 0; i < num_cycles; ++i) {
    NodeStatus status = execute(node.children[0], bb, ctx);
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeTimeout(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Timeout must have exactly 1 child");
  }

  int timeout_ms = getIntParam(node, "timeout_ms", bb, 1000);
  auto start = std::chrono::steady_clock::now();
  Node timed_child = node.children[0];

  // If the immediate child is a Shell node, push the timeout down so the
  // process itself can be interrupted instead of only failing after it returns.
  if (timed_child.id == "Shell") {
    auto it = timed_child.params.find("timeout");
    if (it == timed_child.params.end()) {
      timed_child.params["timeout"] = static_cast<double>(timeout_ms);
    } else if (std::holds_alternative<double>(it->second)) {
      double existing_timeout = std::get<double>(it->second);
      if (existing_timeout <= 0.0 || existing_timeout > static_cast<double>(timeout_ms)) {
        timed_child.params["timeout"] = static_cast<double>(timeout_ms);
      }
    }
  }

  NodeStatus status = execute(timed_child, bb);

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start).count();

  if (elapsed > timeout_ms) {
    return NodeStatus::FAILURE;
  }

  return status;
}

NodeStatus Executor::executeTimeout(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Timeout must have exactly 1 child");
  }

  int timeout_ms = getIntParam(node, "timeout_ms", bb, 1000);
  auto start = std::chrono::steady_clock::now();
  NodeStatus status;

  if (node.children[0].id == "Shell") {
    Node timed_child = node.children[0];
    auto it = timed_child.params.find("timeout");
    if (it == timed_child.params.end()) {
      timed_child.params["timeout"] = static_cast<double>(timeout_ms);
    } else if (std::holds_alternative<double>(it->second)) {
      double existing_timeout = std::get<double>(it->second);
      if (existing_timeout <= 0.0 || existing_timeout > static_cast<double>(timeout_ms)) {
        timed_child.params["timeout"] = static_cast<double>(timeout_ms);
      }
    }
    status = execute(timed_child, bb, ctx);
  } else {
    status = execute(node.children[0], bb, ctx);
  }

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - start).count();

  if (elapsed > timeout_ms) {
    return NodeStatus::FAILURE;
  }

  return status;
}

NodeStatus Executor::executeDelay(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Delay must have exactly 1 child");
  }

  int delay_ms = getIntParam(node, "delay_ms", bb, 100);

  // Simple delay implementation
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  return execute(node.children[0], bb);
}

NodeStatus Executor::executeDelay(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Delay must have exactly 1 child");
  }

  int delay_ms = getIntParam(node, "delay_ms", bb, 100);
  std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

  return execute(node.children[0], bb, ctx);
}

// ============ Advanced Control Flow ============

NodeStatus Executor::executeSwitch(const Node& node, Blackboard& bb) {
  // Switch(variable=...) { case1 case2 case3 }
  // Executes the child at index matching the resolved zero-based case index.

  auto it = node.params.find("variable");
  if (it == node.params.end()) {
    throw std::runtime_error("Switch requires 'variable' parameter");
  }

  int index = resolveSwitchIndex(it->second, bb);

  if (index < 0 || index >= static_cast<int>(node.children.size())) {
    // Out of range, return failure
    return NodeStatus::FAILURE;
  }

  return execute(node.children[index], bb);
}

NodeStatus Executor::executeSwitch(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  auto it = node.params.find("variable");
  if (it == node.params.end()) {
    throw std::runtime_error("Switch requires 'variable' parameter");
  }

  int index = resolveSwitchIndex(it->second, bb);

  if (index < 0 || index >= static_cast<int>(node.children.size())) {
    return NodeStatus::FAILURE;
  }

  return execute(node.children[index], bb, ctx);
}

NodeStatus Executor::executeWhileDo(const Node& node, Blackboard& bb) {
  // WhileDo { condition action }
  const auto condition_param = node.params.find("condition");
  const size_t expected_children = condition_param != node.params.end() ? 1 : 2;
  if (node.children.size() != expected_children) {
    throw std::runtime_error("WhileDo must have exactly 2 children (condition, action)");
  }

  int max_iterations = getIntParam(node, "max_iterations", bb, 100);
  int iteration = 0;

  while (iteration < max_iterations) {
    NodeStatus condition_status = NodeStatus::FAILURE;
    size_t action_index = 1;

    if (condition_param != node.params.end()) {
      condition_status = evaluateConditionValue(condition_param->second, bb)
                         ? NodeStatus::SUCCESS
                         : NodeStatus::FAILURE;
      action_index = 0;
    } else {
      condition_status = execute(node.children[0], bb);
    }

    if (condition_status == NodeStatus::FAILURE) {
      return NodeStatus::SUCCESS; // Condition false, exit loop successfully
    }

    if (condition_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    // Condition is SUCCESS, execute action
    NodeStatus action_status = execute(node.children[action_index], bb);

    if (action_status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }

    if (action_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    iteration++;
  }

  // Warn if loop terminated due to max_iterations limit
  if (iteration >= max_iterations) {
    std::cerr << "[WARN] WhileDo node reached max_iterations limit (" << max_iterations 
              << "). Loop terminated without condition becoming false.\n";
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeWhileDo(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  const auto condition_param = node.params.find("condition");
  const size_t expected_children = condition_param != node.params.end() ? 1 : 2;
  if (node.children.size() != expected_children) {
    throw std::runtime_error("WhileDo must have exactly 2 children (condition, action)");
  }

  int max_iterations = getIntParam(node, "max_iterations", bb, 100);
  int iteration = 0;

  while (iteration < max_iterations) {
    NodeStatus condition_status = NodeStatus::FAILURE;
    size_t action_index = 1;

    if (condition_param != node.params.end()) {
      condition_status = evaluateConditionValue(condition_param->second, bb)
                         ? NodeStatus::SUCCESS
                         : NodeStatus::FAILURE;
      action_index = 0;
    } else {
      condition_status = execute(node.children[0], bb, ctx);
    }

    if (condition_status == NodeStatus::FAILURE) {
      return NodeStatus::SUCCESS;
    }

    if (condition_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    NodeStatus action_status = execute(node.children[action_index], bb, ctx);

    if (action_status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }

    if (action_status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    iteration++;
  }

  // Warn if loop terminated due to max_iterations limit
  if (iteration >= max_iterations) {
    std::cerr << "[WARN] WhileDo node reached max_iterations limit (" << max_iterations 
              << "). Loop terminated without condition becoming false.\n";
  }

  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executeIfThenElse(const Node& node, Blackboard& bb) {
  // IfThenElse { condition then_branch else_branch }
  const auto condition_param = node.params.find("condition");
  const size_t min_children = condition_param != node.params.end() ? 1 : 2;
  const size_t max_children = condition_param != node.params.end() ? 2 : 3;
  if (node.children.size() < min_children || node.children.size() > max_children) {
    throw std::runtime_error("IfThenElse must have 2 or 3 children (condition, then, [else])");
  }

  NodeStatus condition_status = NodeStatus::FAILURE;
  size_t then_index = 1;
  size_t else_index = 2;

  if (condition_param != node.params.end()) {
    condition_status = evaluateConditionValue(condition_param->second, bb)
                       ? NodeStatus::SUCCESS
                       : NodeStatus::FAILURE;
    then_index = 0;
    else_index = 1;
  } else {
    condition_status = execute(node.children[0], bb);
  }

  if (condition_status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }

  if (condition_status == NodeStatus::SUCCESS) {
    // Execute then branch
    return execute(node.children[then_index], bb);
  } else {
    // Execute else branch if it exists
    if (node.children.size() > else_index) {
      return execute(node.children[else_index], bb);
    }
    return NodeStatus::FAILURE;
  }
}

NodeStatus Executor::executeIfThenElse(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  const auto condition_param = node.params.find("condition");
  const size_t min_children = condition_param != node.params.end() ? 1 : 2;
  const size_t max_children = condition_param != node.params.end() ? 2 : 3;
  if (node.children.size() < min_children || node.children.size() > max_children) {
    throw std::runtime_error("IfThenElse must have 2 or 3 children (condition, then, [else])");
  }

  NodeStatus condition_status = NodeStatus::FAILURE;
  size_t then_index = 1;
  size_t else_index = 2;

  if (condition_param != node.params.end()) {
    condition_status = evaluateConditionValue(condition_param->second, bb)
                       ? NodeStatus::SUCCESS
                       : NodeStatus::FAILURE;
    then_index = 0;
    else_index = 1;
  } else {
    condition_status = execute(node.children[0], bb, ctx);
  }

  if (condition_status == NodeStatus::RUNNING) {
    return NodeStatus::RUNNING;
  }

  if (condition_status == NodeStatus::SUCCESS) {
    return execute(node.children[then_index], bb, ctx);
  }

  if (node.children.size() > else_index) {
    return execute(node.children[else_index], bb, ctx);
  }
  return NodeStatus::FAILURE;
}

// ============ SubTree ============

NodeStatus Executor::executeSubTree(const Node& node, Blackboard& bb) {
  // Get subtree name from parameters
  auto it = node.params.find("tree");
  if (it == node.params.end()) {
    throw std::runtime_error("SubTree requires 'tree' parameter");
  }

  std::string tree_id = resolveParamStatic(it->second, bb);
  const Tree* subtree = getTree(tree_id);
  if (subtree == nullptr) {
    throw std::runtime_error("SubTree not found: " + tree_id);
  }

  // Execute the subtree
  return execute(subtree->root, bb);
}

NodeStatus Executor::executeSubTree(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  auto it = node.params.find("tree");
  if (it == node.params.end()) {
    throw std::runtime_error("SubTree requires 'tree' parameter");
  }

  std::string tree_id = resolveParamStatic(it->second, bb);
  const Tree* subtree = getTree(tree_id);
  if (subtree == nullptr) {
    throw std::runtime_error("SubTree not found: " + tree_id);
  }

  return execute(subtree->root, bb, ctx);
}

// ============ Advanced Decorators (BT.CPP Compatible) ============

NodeStatus Executor::executeKeepRunningUntilFailure(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("KeepRunningUntilFailure must have exactly 1 child");
  }

  int max_iterations = getIntParam(node, "max_iterations", bb, 1000);
  
  for (int i = 0; i < max_iterations; ++i) {
    NodeStatus status = execute(node.children[0], bb);
    
    if (status == NodeStatus::FAILURE) {
      return NodeStatus::SUCCESS;  // Failure is success for this decorator
    }
    
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
    
    // SUCCESS: continue looping
  }
  
  // Exceeded max iterations without failure
  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeKeepRunningUntilFailure(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("KeepRunningUntilFailure must have exactly 1 child");
  }

  int max_iterations = getIntParam(node, "max_iterations", bb, 1000);

  for (int i = 0; i < max_iterations; ++i) {
    NodeStatus status = execute(node.children[0], bb, ctx);

    if (status == NodeStatus::FAILURE) {
      return NodeStatus::SUCCESS;
    }

    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
  }

  return NodeStatus::FAILURE;
}

NodeStatus Executor::executeRunOnce(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("RunOnce must have exactly 1 child");
  }

  std::string node_id = "RunOnce_" + std::to_string(reinterpret_cast<uintptr_t>(&node));
  
  // Check if already executed
  if (ctx.hasState(node_id)) {
    NodeState& state = ctx.getState(node_id);
    if (state.has_run) {
      // Return cached result
      return state.last_status;
    }
  }
  
  // First execution
  NodeStatus status = execute(node.children[0], bb, ctx);
  
  if (status != NodeStatus::RUNNING) {
    // Cache the result
    NodeState& state = ctx.getState(node_id);
    state.has_run = true;
    state.last_status = status;
  }
  
  return status;
}

NodeStatus Executor::executeConsumeQueue(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ConsumeQueue must have exactly 1 child");
  }

  // Get queue key from parameters
  std::string queue_key = getTaskStringParam(node.params, bb, "queue_key", "task_queue");
  
  if (!bb.has(queue_key)) {
    return NodeStatus::FAILURE;
  }
  
  // Get queue content (JSON array format expected)
  std::string queue_data = bb.get(queue_key);
  
  try {
    std::string item;
    std::string remaining;
    bool has_more = false;
    if (!consumeJsonArrayFront(queue_data, item, remaining, has_more)) {
      return NodeStatus::FAILURE;
    }

    // Update queue in blackboard
    bb.set(queue_key, remaining);
    
    // Set current item in blackboard for child to consume
    std::string item_key = getTaskStringParam(node.params, bb, "item_key", "current_item");
    bb.set(item_key, item);
    
    // Execute child with the item
    NodeStatus status = execute(node.children[0], bb);
    
    if (status == NodeStatus::SUCCESS && has_more) {
      // More items to process, return RUNNING to continue
      return NodeStatus::RUNNING;
    }
    
    return status;
    
  } catch (const std::exception&) {
    // Invalid queue format
    return NodeStatus::FAILURE;
  }
}

NodeStatus Executor::executeConsumeQueue(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("ConsumeQueue must have exactly 1 child");
  }

  std::string queue_key = getTaskStringParam(node.params, bb, "queue_key", "task_queue");

  if (!bb.has(queue_key)) {
    return NodeStatus::FAILURE;
  }

  std::string queue_data = bb.get(queue_key);

  try {
    std::string item;
    std::string remaining;
    bool has_more = false;
    if (!consumeJsonArrayFront(queue_data, item, remaining, has_more)) {
      return NodeStatus::FAILURE;
    }

    bb.set(queue_key, remaining);

    std::string item_key = getTaskStringParam(node.params, bb, "item_key", "current_item");
    bb.set(item_key, item);

    NodeStatus status = execute(node.children[0], bb, ctx);

    if (status == NodeStatus::SUCCESS && has_more) {
      return NodeStatus::RUNNING;
    }

    return status;

  } catch (const std::exception&) {
    return NodeStatus::FAILURE;
  }
}

// ============ Extended Decorators ============

NodeStatus Executor::executePrecondition(const Node& node, Blackboard& bb) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Precondition must have exactly 1 child");
  }

  // Get condition parameter
  auto condition_it = node.params.find("condition");
  if (condition_it == node.params.end()) {
    throw std::runtime_error("Precondition requires 'condition' parameter");
  }

  // Evaluate condition
  bool condition_met = evaluateConditionValue(condition_it->second, bb);
  
  if (!condition_met) {
    // Condition not met, return FAILURE without executing child
    return NodeStatus::FAILURE;
  }
  
  // Condition met, execute child
  return execute(node.children[0], bb);
}

NodeStatus Executor::executePrecondition(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("Precondition must have exactly 1 child");
  }

  auto condition_it = node.params.find("condition");
  if (condition_it == node.params.end()) {
    throw std::runtime_error("Precondition requires 'condition' parameter");
  }

  bool condition_met = evaluateConditionValue(condition_it->second, bb);

  if (!condition_met) {
    return NodeStatus::FAILURE;
  }

  return execute(node.children[0], bb, ctx);
}

NodeStatus Executor::executeEntryUpdated(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.size() != 1) {
    throw std::runtime_error("EntryUpdated must have exactly 1 child");
  }

  // Get watched key from parameters
  std::string watch_key = getTaskStringParam(node.params, bb, "watch_key");
  if (watch_key.empty()) {
    throw std::runtime_error("EntryUpdated requires 'watch_key' parameter");
  }
  
  std::string node_id = "EntryUpdated_" + std::to_string(reinterpret_cast<uintptr_t>(&node));
  NodeState& state = ctx.getState(node_id);
  
  // Get current value
  std::string current_value;
  if (bb.has(watch_key)) {
    current_value = bb.get(watch_key);
  }
  
  // Check if value changed
  bool value_changed = false;
  if (!state.has_run) {
    // First execution
    value_changed = true;
    state.has_run = true;
  } else {
    // Compare with stored value
    std::string stored_key = node_id + "_stored_value";
    if (bb.has(stored_key)) {
      std::string stored_value = bb.get(stored_key);
      value_changed = (current_value != stored_value);
    } else {
      value_changed = true;
    }
  }
  
  if (value_changed) {
    // Store new value
    bb.set(node_id + "_stored_value", current_value);
    
    // Reset child state to force re-execution
    ctx.reset(node_id + "_child");
    
    NodeStatus status = execute(node.children[0], bb, ctx);
    state.last_status = status;
    return status;
  } else {
    // Value unchanged, return cached result
    return state.last_status;
  }
}

// ============ Extended Control Flow ============

NodeStatus Executor::executePipelineSequence(const Node& node, Blackboard& bb) {
  // PipelineSequence executes children in sequence, passing output of each to the next
  // Each child can read from "pipeline_input" and write to "pipeline_output"
  
  if (node.children.empty()) {
    return NodeStatus::SUCCESS;
  }
  
  // Get initial input (optional)
  std::string pipeline_data;
  std::string input_key = getTaskStringParam(node.params, bb, "input_key", "pipeline_input");
  if (bb.has(input_key)) {
    pipeline_data = bb.get(input_key);
  }
  
  // Execute each child in sequence
  for (size_t i = 0; i < node.children.size(); ++i) {
    // Set input for this stage
    bb.set("pipeline_input", pipeline_data);
    
    // Execute child
    NodeStatus status = execute(node.children[i], bb);
    
    if (status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }
    
    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }
    
    // Get output from this stage for next stage
    if (bb.has("pipeline_output")) {
      pipeline_data = bb.get("pipeline_output");
    }
  }
  
  // Store final output (optional)
  std::string output_key = getTaskStringParam(node.params, bb, "output_key", "pipeline_result");
  bb.set(output_key, pipeline_data);
  
  return NodeStatus::SUCCESS;
}

NodeStatus Executor::executePipelineSequence(const Node& node, Blackboard& bb, ExecutionContext& ctx) {
  if (node.children.empty()) {
    return NodeStatus::SUCCESS;
  }

  std::string pipeline_data;
  std::string input_key = getTaskStringParam(node.params, bb, "input_key", "pipeline_input");
  if (bb.has(input_key)) {
    pipeline_data = bb.get(input_key);
  }

  for (size_t i = 0; i < node.children.size(); ++i) {
    bb.set("pipeline_input", pipeline_data);

    NodeStatus status = execute(node.children[i], bb, ctx);

    if (status == NodeStatus::FAILURE) {
      return NodeStatus::FAILURE;
    }

    if (status == NodeStatus::RUNNING) {
      return NodeStatus::RUNNING;
    }

    if (bb.has("pipeline_output")) {
      pipeline_data = bb.get("pipeline_output");
    }
  }

  std::string output_key = getTaskStringParam(node.params, bb, "output_key", "pipeline_result");
  bb.set(output_key, pipeline_data);

  return NodeStatus::SUCCESS;
}

// ============ Helpers ============

int Executor::getIntParam(const Node& node,
                          const std::string& key,
                          Blackboard& bb,
                          int default_value) {
  auto it = node.params.find(key);
  if (it == node.params.end()) {
    return default_value;
  }
  return parseStrictIntValue(it->second, bb, key, node.id);
}

std::string Executor::resolveParam(const Value& value, Blackboard& bb) {
  return resolveParamStatic(value, bb);
}

} // namespace actions
