#include "executors/declarative_tree_executor.hpp"
#include "actions/shell_executor.hpp"
#include "util/path_utils.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"
#include "util/env_parser.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <regex>
#include <set>
#include <sstream>

namespace Praktor::Execution
{

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

int parseDurationMs(const std::string& timeout) {
  if (timeout.empty()) {
    return 30000;
  }

  if (std::all_of(timeout.begin(), timeout.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
    return std::stoi(timeout);
  }

  if (timeout.size() < 2) {
    throw std::runtime_error("Invalid timeout format: " + timeout);
  }

  char unit = timeout.back();
  int value = std::stoi(timeout.substr(0, timeout.size() - 1));

  switch (unit) {
    case 's': return value * 1000;
    case 'm': return value * 60 * 1000;
    case 'h': return value * 60 * 60 * 1000;
    default: throw std::runtime_error("Invalid timeout unit in: " + timeout);
  }
}

bool looksLikeStructuredValue(const std::string& value) {
  if (value.empty()) {
    return false;
  }

  char first = value.front();
  return first == '{' || first == '[';
}

WorkflowValue parseWorkflowValue(const std::string& value) {
  if (looksLikeStructuredValue(value)) {
    try {
      return WorkflowValue::parse(value);
    } catch (const std::exception&) {
    }
  }
  return WorkflowValue(value);
}

struct TaskScopeGuard {
  WorkflowContext& context;
  bool active = true;

  TaskScopeGuard(WorkflowContext& ctx, const std::string& task_name)
      : context(ctx) {
    context.pushTaskScope(task_name);
  }

  ~TaskScopeGuard() {
    if (active) {
      context.popTaskScope();
    }
  }

  TaskScopeGuard(const TaskScopeGuard&) = delete;
  TaskScopeGuard& operator=(const TaskScopeGuard&) = delete;
};

void emitShellConsoleLine(const std::string& line) {
  if (Praktor::Logging::isVerboseEnabled()) {
    return;
  }

  std::string message = "__SHELL__:";
  message += line;
  Praktor::Logging::emitConsoleEvent(message);
}

std::string stringifyDeclaredParam(const actions::Value& value) {
  if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  }
  return std::get<bool>(value) ? "true" : "false";
}

void collectDeclaredOutputKeys(const actions::Node& node,
                               std::set<std::string>& output_keys,
                               std::set<std::string>& stderr_keys,
                               std::set<std::string>& exit_code_keys) {
  auto output_it = node.params.find("output_key");
  if (output_it != node.params.end()) {
    output_keys.insert(stringifyDeclaredParam(output_it->second));
  }

  if (node.id == "SetVariable") {
    auto key_it = node.params.find("key");
    if (key_it != node.params.end()) {
      output_keys.insert(stringifyDeclaredParam(key_it->second));
    }
  }

  auto stderr_it = node.params.find("stderr_key");
  if (stderr_it != node.params.end()) {
    stderr_keys.insert(stringifyDeclaredParam(stderr_it->second));
  }

  auto exit_it = node.params.find("exit_code_key");
  if (exit_it != node.params.end()) {
    exit_code_keys.insert(stringifyDeclaredParam(exit_it->second));
  }

  for (const auto& child : node.children) {
    collectDeclaredOutputKeys(child, output_keys, stderr_keys, exit_code_keys);
  }
}

void applyTaskExecutionDefaults(actions::Node& node, const Task& task) {
  if (node.id == "Shell") {
    if (node.params.find("stream_output") == node.params.end()) {
      node.params["stream_output"] = !task.silent;
    }

    if (task.working_dir && node.params.find("working_dir") == node.params.end()) {
      node.params["working_dir"] = Praktor::util::resolveRelativePath(task.source_path, *task.working_dir).string();
    }

    if (task.timeout && node.params.find("timeout") == node.params.end()) {
      node.params["timeout"] = static_cast<double>(parseDurationMs(*task.timeout));
    }
  }

  for (auto& child : node.children) {
    applyTaskExecutionDefaults(child, task);
  }
}

void setContextEnvironmentValue(WorkflowContext& context, const std::string& key, const std::string& value) {
  context.setValue("env." + key, value);
}

} // namespace

// Whitelisted context roots for security
static const std::set<std::string> ALLOWED_CONTEXT_ROOTS = {
  "variables", "env", "tasks"
};

// Sensitive path patterns for redaction
static const std::vector<std::string> SENSITIVE_PATTERNS = {
  "ctx.secrets.", "ctx.env.PASSWORD", "ctx.env.TOKEN", "ctx.env.API_KEY",
  "ctx.env.SECRET", "ctx.env.KEY"
};

// Special shell characters that need escaping
static const char* SHELL_SPECIAL_CHARS = "$`\\\"';|&<>(){}[]*?~!#%^";

// Reserved output keys automatically populated
static const std::set<std::string> RESERVED_OUTPUT_KEYS = {
  "shell_exit_code", "shell_stdout", "shell_stderr", 
  "execution_time_ms", "node_status"
};

// Task 3.2: Context bridge - detect {ctx.*} syntax
bool DeclarativeTreeExecutor::hasContextRef(const std::string& param) const
{
  return param.find("{ctx.") != std::string::npos;
}

// Task 3.2: Context bridge - extract path from {ctx.path.to.value}
std::string DeclarativeTreeExecutor::extractContextPath(const std::string& param) const
{
  size_t start = param.find("{ctx.");
  if (start == std::string::npos) {
    return "";
  }
  
  start += 1;  // Skip the '{'
  size_t end = param.find('}', start);
  if (end == std::string::npos) {
    throw std::runtime_error("Unclosed context reference: " + param);
  }
  
  return param.substr(start, end - start);
}

// Task 3.3: Security - check if context root is whitelisted
bool DeclarativeTreeExecutor::isAllowedContextRoot(const std::string& root) const
{
  return ALLOWED_CONTEXT_ROOTS.count(root) > 0;
}

// Task 3.3: Security - check if path is sensitive
bool DeclarativeTreeExecutor::isSensitivePath(const std::string& path) const
{
  for (const auto& pattern : SENSITIVE_PATTERNS) {
    if (path.find(pattern) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Task 3.3: Security - log context access with redaction
void DeclarativeTreeExecutor::logContextAccess(const std::string& path, const std::string& value) const
{
  (void)value;
  if (isSensitivePath(path)) {
    TLOG_DEBUG("Context access: {} [REDACTED]", path);
  } else {
    TLOG_DEBUG("Context access: {}", path);
  }
}

// Task 3.5: Command injection prevention - sanitize shell values
// Windows cmd.exe compatible: wraps value in double quotes and escapes embedded quotes
std::string DeclarativeTreeExecutor::sanitizeForShell(const std::string& value) const
{
  // For cmd.exe: wrap in double quotes, escape embedded double quotes with ""
  std::string sanitized = "\"";
  for (char c : value) {
    if (c == '"') {
      sanitized += "\"\"";  // cmd.exe escapes " as ""
    } else {
      sanitized += c;
    }
  }
  sanitized += "\"";
  return sanitized;
}

// Task 3.2: Context bridge - resolve {ctx.*} references
std::string DeclarativeTreeExecutor::resolveContextRef(
    const std::string& param,
    const WorkflowContext& ctx,
    const std::function<std::string(const std::string&)>& transform_value) const
{
  if (!hasContextRef(param)) {
    return param;
  }

  std::string result = param;
  size_t cursor = 0;

  while (true) {
    size_t open = result.find("{ctx.", cursor);
    if (open == std::string::npos) {
      break;
    }

    size_t close = result.find('}', open + 1);
    if (close == std::string::npos) {
      throw std::runtime_error("Unclosed context reference: " + param);
    }

    std::string contextPath = result.substr(open + 1, close - open - 1);

    // Split path into components: ctx.category.path.to.value
    std::vector<std::string> components;
    std::stringstream ss(contextPath);
    std::string component;
    while (std::getline(ss, component, '.')) {
      components.push_back(component);
    }

    if (components.empty() || components[0] != "ctx") {
      throw std::runtime_error("Invalid context reference: must start with 'ctx.'");
    }

    if (components.size() < 2) {
      throw std::runtime_error("Invalid context reference: missing category after 'ctx.'");
    }

    std::string root = components[1];
    if (!isAllowedContextRoot(root)) {
      throw std::runtime_error("Unauthorized context access: '" + root + "' is not whitelisted. "
                               "Allowed roots: variables, env, tasks");
    }

    std::string resolved;

    if (root == "variables") {
      if (components.size() < 3) {
        throw std::runtime_error("Invalid context reference: missing variable name");
      }
      std::string varName = components[2];
      resolved = ctx.getValue<std::string>(varName);
    } else if (root == "env") {
      if (components.size() < 3) {
        throw std::runtime_error("Invalid context reference: missing environment variable name");
      }
      std::string envName = components[2];
      WorkflowValue envValue = ctx.getValueByPath("env." + envName);
      if (envValue.is_null()) {
        throw std::runtime_error("Environment variable not found: " + envName);
      }
      resolved = envValue.is_string() ? envValue.as<std::string>() : envValue.to_string();
    } else if (root == "tasks") {
      if (components.size() < 5) {
        throw std::runtime_error("Invalid context reference: expected ctx.tasks.TASK_NAME.outputs.KEY");
      }

      std::string taskName = components[2];
      std::string outputsKey = components[3];

      if (outputsKey != "outputs") {
        throw std::runtime_error("Invalid context reference: expected 'outputs' after task name");
      }

      std::string fullPath = "tasks." + taskName + ".outputs";
      for (size_t i = 4; i < components.size(); ++i) {
        fullPath += "." + components[i];
      }

      WorkflowValue value = ctx.getValueByPath(fullPath);
      if (value.is_null()) {
        throw std::runtime_error("Context path not found: " + contextPath +
                                 "\nTask '" + taskName + "' may not have been executed or output key does not exist");
      }

      resolved = value.is_string() ? value.as<std::string>() : value.to_string();
    }

    logContextAccess(contextPath, resolved);
    if (transform_value) {
      resolved = transform_value(resolved);
    }
    result.replace(open, close - open + 1, resolved);
    cursor = open + resolved.size();
  }

  return result;
}

// Task 3.4: Parse parameter value to appropriate type
actions::Value DeclarativeTreeExecutor::parseValue(const std::string& str) const
{
  // Try to parse as number
  try {
    size_t pos;
    double num = std::stod(str, &pos);
    if (pos == str.length()) {
      return num;
    }
  } catch (const std::exception&) {}
  
  // Try to parse as boolean
  if (str == "true") {
    return true;
  } else if (str == "false") {
    return false;
  }
  
  // Default to string
  return str;
}

// Task 3.4: Convert Praktor OrchNode to actions::Node with context bridge support
actions::Node DeclarativeTreeExecutor::convertNode(const OrchNode& praktor_node, 
                                                   const WorkflowContext& context,
                                                   const std::string& taskName)
{
  actions::Node node;
  node.id = praktor_node.type;
  
  // Task 3.4: Convert parameters with variable substitution and context bridge
  for (const auto& [key, value] : praktor_node.params) {
    // Step 1: Apply Praktor variable substitution
    std::string substituted = substituteVariables(value, context);
    
    // Step 2: Apply context bridge resolution
    if (hasContextRef(substituted)) {
      // Task 3.3: Prevent circular dependencies
      if (substituted.find("ctx.tasks." + taskName + ".outputs") != std::string::npos) {
        throw std::runtime_error("Circular dependency detected: task '" + taskName + 
                                 "' cannot access its own outputs");
      }
      
      // Shell commands keep their command text literal; only context-sourced values are quoted.
      if (praktor_node.type == "Shell" && key == "cmd") {
        substituted = resolveContextRef(substituted, context, [this, &key](const std::string& resolved) {
          TLOG_WARN("Using context value in shell parameter '{}'. Value has been sanitized to prevent injection.", key);
          return sanitizeForShell(resolved);
        });
      } else {
        substituted = resolveContextRef(substituted, context);
      }
    }
    
    // Step 3: Parse to appropriate type
    node.params[key] = parseValue(substituted);
  }
  
  // Task 3.4: Recursively convert children
  for (const auto& child : praktor_node.children) {
    node.children.push_back(convertNode(child, context, taskName));
  }
  
  return node;
}

// Task 3.6: Bridge Praktor context to actions blackboard
void DeclarativeTreeExecutor::contextToBlackboard(const WorkflowContext& context, actions::Blackboard& bb)
{
  TLOG_DEBUG("Bridging WorkflowContext to actions Blackboard");
  
  // Copy all workflow variables to blackboard
  auto allVars = context.getAllVariables();
  for (const auto& [key, value] : allVars) {
    bb.set(key, value);
  }
  TLOG_DEBUG("Copied {} string variables to actions Blackboard", allVars.size());
  
  // Copy all task outputs to blackboard
  auto allValues = context.getAllVisibleValues();
  for (const auto& [key, value] : allValues) {
    if (key.find("tasks.") == 0) {
      // Extract task outputs
      if (value.is_object()) {
        bb.set(key, value.to_string());
      } else if (value.is_string()) {
        bb.set(key, value.as<std::string>());
      }
    }
  }
  
  // Environment variables stay in WorkflowContext under env.* and are resolved on demand.
  
  TLOG_DEBUG("Context bridging complete");
}

DeclarativeTreeExecutor::DeclarativeTreeExecutor(EnvMap base_environment)
  : base_environment_(std::move(base_environment)) {}

// Task 3.7: Map blackboard outputs back to context
void DeclarativeTreeExecutor::blackboardToContext(const actions::Blackboard& bb, 
                                                    WorkflowContext& context, 
                                                    const std::string& taskName)
{
  TLOG_DEBUG("Mapping actions Blackboard outputs to WorkflowContext for task: {}", taskName);
  
  // Bridge action Shell node outputs to workflow context.
  // The Shell node stores:
  //   shell_exit_code (or custom exit_code_key)
  //   shell_output (or custom output_key) — for stdout
  //   shell_stderr (or custom stderr_key)
  // We map these to the same output format CommandExecutor used:
  //   exit_code, stdout, stderr
  
  // Exit code
  if (bb.has("shell_exit_code")) {
    std::string value = bb.get("shell_exit_code");
    context.setCurrentTaskOutput("exit_code", value);
    TLOG_DEBUG("  Output: exit_code = {}", value);
  }
  
  // Stdout — Shell node default key is "shell_output", desugared commands use "stdout"
  if (bb.has("stdout")) {
    std::string value = bb.get("stdout");
    context.setCurrentTaskOutput("stdout", value);
    TLOG_DEBUG("  Output: stdout = {} bytes", value.size());
  } else if (bb.has("shell_output")) {
    std::string value = bb.get("shell_output");
    context.setCurrentTaskOutput("stdout", value);
    TLOG_DEBUG("  Output: stdout (from shell_output) = {} bytes", value.size());
  }
  
  // Stderr
  if (bb.has("shell_stderr")) {
    std::string value = bb.get("shell_stderr");
    context.setCurrentTaskOutput("stderr", value);
    TLOG_DEBUG("  Output: stderr = {} bytes", value.size());
  }
  
  // Execution timing
  if (bb.has("execution_time_ms")) {
    std::string value = bb.get("execution_time_ms");
    context.setCurrentTaskOutput("execution_time_ms", value);
    TLOG_DEBUG("  Output: execution_time_ms = {}", value);
  }
  
  // Node status
  if (bb.has("node_status")) {
    std::string value = bb.get("node_status");
    context.setCurrentTaskOutput("node_status", value);
    TLOG_DEBUG("  Output: node_status = {}", value);
  }

  TLOG_DEBUG("Output mapping complete");
}

TaskResult DeclarativeTreeExecutor::execute(const Task& task, WorkflowContext& context)
{
  TLOG_DEBUG("Executing action_orchestration task: {}", task.name);

  auto execution_context = context.fork();
  auto& local_context = *execution_context;
  TaskScopeGuard task_scope(local_context, task.name);
  auto params = std::get<OrchParams>(task.specifics);
  actions::Executor executor;
  std::set<std::string> output_keys;
  std::set<std::string> stderr_keys;
  std::set<std::string> exit_code_keys;

  EnvMap task_environment_overrides;
  for (const auto& env_file : task.dot_env) {
    try {
      auto parsed = Praktor::util::parseDotEnvFile(
          Praktor::util::resolveRelativePath(task.source_path, env_file));
      for (const auto& [key, value] : parsed) {
        task_environment_overrides[key] = substituteVariables(value, local_context);
      }
      TLOG_DEBUG("Loaded dotEnv file for orchestration: {}", env_file);
    } catch (const std::exception& e) {
      TLOG_WARN("Failed to load dotEnv file '{}': {}", env_file, e.what());
    }
  }

  for (const auto& [key, value] : task.env) {
    task_environment_overrides[key] = substituteVariables(value, local_context);
  }

  for (const auto& [key, value] : task_environment_overrides) {
    setContextEnvironmentValue(local_context, key, value);
  }

  std::map<std::string, std::string> shell_environment(base_environment_.begin(), base_environment_.end());
  for (const auto& [key, value] : task_environment_overrides) {
    shell_environment[key] = value;
  }
  executor.setEnvironment(shell_environment);

  actions::Tree tree;
  tree.name = task.name;
  tree.root = convertNode(params.root, local_context, task.name);
  TLOG_DEBUG("Actions tree built from YAML: {} (root: {})", tree.name, tree.root.id);
  collectDeclaredOutputKeys(tree.root, output_keys, stderr_keys, exit_code_keys);

  applyTaskExecutionDefaults(tree.root, task);

  actions::Blackboard blackboard;
  actions::ShellExecutor::setStreamCallback(emitShellConsoleLine);

  if (!shell_environment.empty()) {
    TLOG_DEBUG("Set {} environment variables for action execution", shell_environment.size());
  }

  contextToBlackboard(local_context, blackboard);
  // preprocessNodeParams removed: convertNode already performs Mustache substitution.
  // Calling it again would cause double expansion if variable values contain {{ }}.

  auto start_time = std::chrono::high_resolution_clock::now();
  actions::NodeStatus status = executor.execute(tree, blackboard);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  blackboard.set("execution_time_ms", std::to_string(duration.count()));

  std::string status_str = (status == actions::NodeStatus::SUCCESS) ? "SUCCESS" :
                           (status == actions::NodeStatus::FAILURE) ? "FAILURE" : "RUNNING";
  blackboard.set("node_status", status_str);

  blackboardToContext(blackboard, local_context, task.name);

  for (const auto& key : output_keys) {
    if (!blackboard.has(key)) {
      continue;
    }

    const std::string raw_value = blackboard.get(key);
    WorkflowValue parsed_value = parseWorkflowValue(raw_value);
    local_context.setCurrentTaskOutput(key, parsed_value);
  }

  for (const auto& key : stderr_keys) {
    if (blackboard.has(key)) {
      const std::string raw_value = blackboard.get(key);
      local_context.setCurrentTaskOutput(key, raw_value);
    }
  }

  for (const auto& key : exit_code_keys) {
    if (blackboard.has(key)) {
      const std::string raw_value = blackboard.get(key);
      local_context.setCurrentTaskOutput(key, raw_value);
    }
  }

  bool success = (status == actions::NodeStatus::SUCCESS);
  std::string error_msg = success ? "" : "Action execution failed";

  TaskResult result(success, error_msg);

  if (blackboard.has("shell_exit_code")) {
    try { result.exit_code = std::stoll(blackboard.get("shell_exit_code")); } catch (const std::exception&) {}
  }
  if (blackboard.has("stdout")) {
    result.stdout_data = blackboard.get("stdout");
  } else if (blackboard.has("shell_output")) {
    result.stdout_data = blackboard.get("shell_output");
  }
  if (blackboard.has("shell_stderr")) {
    result.stderr_data = blackboard.get("shell_stderr");
  }
  result.output_streamed_live = blackboard.has("shell_exit_code");

  if (!success && result.stderr_data.empty()) {
    result.error_message = "Action execution failed with status " + status_str;
  } else if (!success) {
    result.error_message = "Command failed with exit code " +
      std::to_string(result.exit_code) + ". Stderr: " + result.stderr_data;
  }

  context.mergeLocalValuesFrom(local_context);

  TLOG_DEBUG("Action orchestration task {} completed with status: {} ({}ms)",
            task.name,
            status_str,
            duration.count());

  return result;
}

std::unique_ptr<TaskExecutor> createDeclarativeTreeExecutor(EnvMap base_environment)
{
  return std::make_unique<DeclarativeTreeExecutor>(std::move(base_environment));
}

}  // namespace Praktor::Execution
