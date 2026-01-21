#include "executors/uses_executor.hpp"

#include "dag/dependency_graph.hpp"
#include "dag/scoped_variables.hpp"
#include "dag/workflow_executor.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_parser.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Praktor::Execution {

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

std::string trim(const std::string &value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

EnvMap parseDotEnvFile(const std::filesystem::path &file_path) {
  EnvMap result;
  std::ifstream input(file_path);
  if (!input.is_open()) {
    throw std::runtime_error("Failed to open .env file: " + file_path.string());
  }

  std::string line;
  while (std::getline(input, line)) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    if (trimmed.rfind("export ", 0) == 0) {
      trimmed = trim(trimmed.substr(7));
    }

    size_t equals_pos = trimmed.find('=');
    if (equals_pos == std::string::npos) {
      continue;
    }

    std::string key = trim(trimmed.substr(0, equals_pos));
    std::string value = trim(trimmed.substr(equals_pos + 1));
    if (!value.empty() && value.size() >= 2) {
      bool quoted = (value.front() == '"' && value.back() == '"') ||
                    (value.front() == '\'' && value.back() == '\'');
      if (quoted) {
        value = value.substr(1, value.size() - 2);
      }
    }

    if (!key.empty()) {
      result[key] = value;
    }
  }

  return result;
}

std::filesystem::path resolveRelativePath(const std::string &base, const std::string &child) {
  std::filesystem::path base_path =
      base.empty() ? std::filesystem::current_path() : std::filesystem::path(base).parent_path();
  std::filesystem::path relative(child);
  if (relative.is_absolute()) {
    return relative.lexically_normal();
  }
  return (base_path / relative).lexically_normal();
}

} // anonymous namespace

UsesExecutor::UsesExecutor(std::unordered_map<std::string, std::string> base_environment,
                           size_t num_threads)
    : base_environment_(std::move(base_environment)), num_threads_(num_threads) {}

TaskResult UsesExecutor::execute(const Task &task, WorkflowContext &context) {
  TLOG_INFO("Executing uses: {}", task.name);

  try {
    const auto &params = std::get<UsesParams>(task.specifics);
    std::filesystem::path base_dir = task.source_path.empty()
                                         ? std::filesystem::current_path()
                                         : std::filesystem::path(task.source_path).parent_path();

    std::filesystem::path resolved = resolveRelativePath(task.source_path, params.path);
    Workflow nested = TaskParser::parseFileWithImports(resolved.string(), base_dir.string());
    nested.source_path = resolved.string();

    DependencyGraph<Task> graph = TaskParser::buildGraph(nested);

    // Build environment for nested workflow
    EnvMap nested_env = base_environment_;
    Vars env_vars_to_scope;

    for (const auto &[key, value] : nested.env) {
      std::string evaluated = substituteVariables(value, context);
      nested_env[key] = evaluated;
      env_vars_to_scope[key] = evaluated;
    }

    for (const auto &env_file : nested.dot_env) {
      std::filesystem::path env_path = resolveRelativePath(nested.source_path, env_file);
      try {
        auto parsed = parseDotEnvFile(env_path);
        for (const auto &[key, value] : parsed) {
          std::string evaluated = substituteVariables(value, context);
          nested_env[key] = evaluated;
          env_vars_to_scope[key] = evaluated;
        }
      } catch (const std::exception &e) {
        TLOG_WARN("Failed to load nested dotEnv file '{}': {}", env_path.string(), e.what());
      }
    }

    // RAII: All variables automatically restored when scope exits
    ScopedVariables env_scope(context, env_vars_to_scope);
    ScopedVariables var_scope(context, nested.variables);

    WorkflowExecutor nested_executor(graph, nested_env, num_threads_);
    nested_executor.execute(context, task.name);

    // Note: Nested task outputs are already set to the uses task via alias
    // in setCurrentTaskOutput. No additional collection needed since outputs
    // go directly to TaskRegistry which is not affected by ScopedVariables.

    return TaskResult(true);
  } catch (const std::bad_variant_access &e) {
    return TaskResult(false, "Task does not contain UsesParams: " + std::string(e.what()));
  } catch (const std::exception &e) {
    return TaskResult(false, "Uses execution failed: " + std::string(e.what()));
  }
}

std::unique_ptr<TaskExecutor>
createUsesExecutor(std::unordered_map<std::string, std::string> base_environment,
                   size_t num_threads) {
  return std::make_unique<UsesExecutor>(std::move(base_environment), num_threads);
}

} // namespace Praktor::Execution
