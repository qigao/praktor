#include "executors/declarative_tree_executor.hpp"
#include "btdsl/shell_executor.hpp"
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

std::filesystem::path resolveRelativePath(const std::string& source_path, const std::string& child) {
  std::filesystem::path relative(child);
  if (relative.is_absolute()) {
    return relative.lexically_normal();
  }

  std::filesystem::path base =
      source_path.empty() ? std::filesystem::current_path() : std::filesystem::path(source_path).parent_path();
  return (base / relative).lexically_normal();
}

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
      return jsoncons::json::parse(value);
    } catch (const std::exception&) {
    }
  }
  return WorkflowValue(value);
}

void emitShellConsoleLine(const std::string& line) {
  if (Praktor::Logging::isVerboseEnabled()) {
    return;
  }

  std::string message = "__SHELL__:";
  message += line;
  Praktor::Logging::emitConsoleEvent(message);
}

void collectDeclaredOutputKeys(const btdsl::Node& node,
                               std::set<std::string>& output_keys,
                               std::set<std::string>& stderr_keys,
                               std::set<std::string>& exit_code_keys) {
  auto output_it = node.params.find("output_key");
  if (output_it != node.params.end() && std::holds_alternative<std::string>(output_it->second)) {
    output_keys.insert(std::get<std::string>(output_it->second));
  }

  auto stderr_it = node.params.find("stderr_key");
  if (stderr_it != node.params.end() && std::holds_alternative<std::string>(stderr_it->second)) {
    stderr_keys.insert(std::get<std::string>(stderr_it->second));
  }

  auto exit_it = node.params.find("exit_code_key");
  if (exit_it != node.params.end() && std::holds_alternative<std::string>(exit_it->second)) {
    exit_code_keys.insert(std::get<std::string>(exit_it->second));
  }

  for (const auto& child : node.children) {
    collectDeclaredOutputKeys(child, output_keys, stderr_keys, exit_code_keys);
  }
}

void applyTaskExecutionDefaults(btdsl::Node& node, const Task& task) {
  if (node.id == "Shell") {
    if (node.params.find("stream_output") == node.params.end()) {
      node.params["stream_output"] = !task.silent;
    }

    if (task.working_dir && node.params.find("working_dir") == node.params.end()) {
      node.params["working_dir"] = resolveRelativePath(task.source_path, *task.working_dir).string();
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
  if (isSensitivePath(path)) {
    TLOG_DEBUG("Context access: {} = [REDACTED]", path);
  } else {
    TLOG_DEBUG("Context access: {} = {}", path, value);
  }
}

// Task 3.5: Command injection prevention - sanitize shell values
std::string DeclarativeTreeExecutor::sanitizeForShell(const std::string& value) const
{
  std::string sanitized;
  sanitized.reserve(value.size() * 2);  // Reserve space for escapes
  
  for (char c : value) {
    if (strchr(SHELL_SPECIAL_CHARS, c)) {
      sanitized += '\\';
    }
    sanitized += c;
  }
  
  return sanitized;
}

// Task 3.2: Context bridge - resolve {ctx.*} references
std::string DeclarativeTreeExecutor::resolveContextRef(const std::string& param, const WorkflowContext& ctx) const
{
  if (!hasContextRef(param)) {
    return param;
  }
  
  std::string contextPath = extractContextPath(param);
  
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
  
  // Task 3.3: Security - validate context root
  std::string root = components[1];
  if (!isAllowedContextRoot(root)) {
    throw std::runtime_error("Unauthorized context access: '" + root + "' is not whitelisted. "
                             "Allowed roots: variables, env, tasks");
  }
  
  // Resolve based on category
  std::string resolved;
  
  if (root == "variables") {
    // ctx.variables.VAR_NAME
    if (components.size() < 3) {
      throw std::runtime_error("Invalid context reference: missing variable name");
    }
    std::string varName = components[2];
    resolved = ctx.getValueOrDefault<std::string>(varName, "");
    if (resolved.empty()) {
      throw std::runtime_error("Variable not found: " + varName);
    }
  }
  else if (root == "env") {
    // ctx.env.ENV_VAR
    if (components.size() < 3) {
      throw std::runtime_error("Invalid context reference: missing environment variable name");
    }
    std::string envName = components[2];
    WorkflowValue envValue = ctx.getValueByPath("env." + envName);
    if (envValue.is_null()) {
      throw std::runtime_error("Environment variable not found: " + envName);
    }
    resolved = envValue.is_string() ? envValue.as<std::string>() : envValue.to_string();
  }
  else if (root == "tasks") {
    // ctx.tasks.TASK_NAME.outputs.KEY or ctx.tasks.TASK_NAME.outputs.KEY.nested.path
    if (components.size() < 5) {
      throw std::runtime_error("Invalid context reference: expected ctx.tasks.TASK_NAME.outputs.KEY");
    }
    
    std::string taskName = components[2];
    std::string outputsKey = components[3];
    
    if (outputsKey != "outputs") {
      throw std::runtime_error("Invalid context reference: expected 'outputs' after task name");
    }
    
    // Build the full path: tasks.TASK_NAME.outputs.KEY.nested.path
    std::string fullPath = "tasks." + taskName + ".outputs";
    for (size_t i = 4; i < components.size(); ++i) {
      fullPath += "." + components[i];
    }
    
    // Use WorkflowContext's getValueByPath for nested access
    WorkflowValue value = ctx.getValueByPath(fullPath);
    if (value.is_null()) {
      throw std::runtime_error("Context path not found: " + contextPath + 
                               "\nTask '" + taskName + "' may not have been executed or output key does not exist");
    }
    
    // Convert to string
    if (value.is_string()) {
      resolved = value.as<std::string>();
    } else {
      resolved = value.to_string();
    }
  }
  
  // Task 3.3: Log context access for audit
  logContextAccess(contextPath, resolved);
  
  // Replace the {ctx.*} reference with resolved value
  std::string result = param;
  size_t start = result.find("{" + contextPath + "}");
  if (start != std::string::npos) {
    result.replace(start, contextPath.length() + 2, resolved);
  }
  
  return result;
}

// Preprocess node parameters: expand Mustache templates {{ variable }}
void DeclarativeTreeExecutor::preprocessNodeParams(btdsl::Node& node, const WorkflowContext& context)
{
  // Process all parameters in this node
  for (auto& [key, value] : node.params) {
    if (std::holds_alternative<std::string>(value)) {
      std::string& str = std::get<std::string>(value);

      // Expand Mustache templates: {{ variable }}
      if (str.find("{{") != std::string::npos) {
        str = substituteVariables(str, context);
        TLOG_DEBUG("Preprocessed parameter '{}': expanded Mustache template", key);
      }
    }
  }

  // Recursively process all children
  for (auto& child : node.children) {
    preprocessNodeParams(child, context);
  }
}

// Task 3.4: Parse parameter value to appropriate type
btdsl::Value DeclarativeTreeExecutor::parseValue(const std::string& str) const
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

// Task 3.4: Convert Praktor BtdslNode to btdsl::Node with context bridge support
btdsl::Node DeclarativeTreeExecutor::convertNode(const BtdslNode& praktor_node, 
                                                   const WorkflowContext& context,
                                                   const std::string& taskName)
{
  btdsl::Node node;
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
      
      substituted = resolveContextRef(substituted, context);
      
      // Task 3.5: Sanitize if this is a shell command parameter
      if (praktor_node.type == "Shell" && key == "cmd") {
        TLOG_WARN("Using context value in shell command. Value has been sanitized to prevent injection.");
        substituted = sanitizeForShell(substituted);
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

// Task 3.6: Bridge Praktor context to BTDSL blackboard
void DeclarativeTreeExecutor::contextToBlackboard(const WorkflowContext& context, btdsl::Blackboard& bb)
{
  TLOG_DEBUG("Bridging WorkflowContext to BTDSL Blackboard");
  
  // Copy all workflow variables to blackboard
  auto allVars = context.getAllVariables();
  for (const auto& [key, value] : allVars) {
    bb.set(key, value);
    TLOG_DEBUG("  Variable: {} = {}", key, value);
  }
  
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
void DeclarativeTreeExecutor::blackboardToContext(const btdsl::Blackboard& bb, 
                                                    WorkflowContext& context, 
                                                    const std::string& taskName)
{
  TLOG_DEBUG("Mapping BTDSL Blackboard outputs to WorkflowContext for task: {}", taskName);
  
  // Bridge BT Shell node outputs to workflow context.
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
  TLOG_DEBUG("Executing behavior_tree task: {}", task.name);

  auto execution_context = context.fork();
  auto& local_context = *execution_context;
  auto params = std::get<BtdslParams>(task.specifics);
  btdsl::Executor executor;
  std::set<std::string> output_keys;
  std::set<std::string> stderr_keys;
  std::set<std::string> exit_code_keys;

  EnvMap task_environment_overrides;
  for (const auto& env_file : task.dot_env) {
    try {
      auto parsed = Praktor::util::parseDotEnvFile(resolveRelativePath(task.source_path, env_file));
      for (const auto& [key, value] : parsed) {
        task_environment_overrides[key] = substituteVariables(value, local_context);
      }
      TLOG_DEBUG("Loaded dotEnv file for BTDSL: {}", env_file);
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

  btdsl::Tree tree;
  tree.name = task.name;
  tree.root = convertNode(params.root, local_context, task.name);
  TLOG_DEBUG("BTDSL tree built from YAML: {} (root: {})", tree.name, tree.root.id);
  collectDeclaredOutputKeys(tree.root, output_keys, stderr_keys, exit_code_keys);

  applyTaskExecutionDefaults(tree.root, task);

  btdsl::Blackboard blackboard;
  btdsl::ShellExecutor::setStreamCallback(emitShellConsoleLine);

  if (!shell_environment.empty()) {
    TLOG_DEBUG("Set {} environment variables for BTDSL execution", shell_environment.size());
  }

  contextToBlackboard(local_context, blackboard);
  preprocessNodeParams(tree.root, local_context);

  auto start_time = std::chrono::high_resolution_clock::now();
  btdsl::NodeStatus status = executor.execute(tree, blackboard);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  blackboard.set("execution_time_ms", std::to_string(duration.count()));

  std::string status_str = (status == btdsl::NodeStatus::SUCCESS) ? "SUCCESS" :
                           (status == btdsl::NodeStatus::FAILURE) ? "FAILURE" : "RUNNING";
  blackboard.set("node_status", status_str);

  blackboardToContext(blackboard, local_context, task.name);

  for (const auto& key : output_keys) {
    if (!blackboard.has(key)) {
      continue;
    }
    local_context.setCurrentTaskOutput(key, parseWorkflowValue(blackboard.get(key)));
  }

  for (const auto& key : stderr_keys) {
    if (blackboard.has(key)) {
      local_context.setCurrentTaskOutput(key, blackboard.get(key));
    }
  }

  for (const auto& key : exit_code_keys) {
    if (blackboard.has(key)) {
      local_context.setCurrentTaskOutput(key, blackboard.get(key));
    }
  }

  bool success = (status == btdsl::NodeStatus::SUCCESS);
  std::string error_msg = success ? "" : "BTDSL execution failed";

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
    result.error_message = "BTDSL execution failed with status " + status_str;
  } else if (!success) {
    result.error_message = "Command failed with exit code " +
      std::to_string(result.exit_code) + ". Stderr: " + result.stderr_data;
  }

  TLOG_DEBUG("Behavior tree task {} completed with status: {} ({}ms)",
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
