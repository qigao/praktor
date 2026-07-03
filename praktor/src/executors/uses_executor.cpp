#include "executors/uses_executor.hpp"

#include "dag/dependency_graph.hpp"
#include "dag/workflow_executor.hpp"
#include "util/env_parser.hpp"
#include "util/logging.hpp"
#include "util/path_utils.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_parser.hpp"

#include <filesystem>
#include <jsoncons/json.hpp>
#include <algorithm>
#include <sstream>

namespace Praktor::Execution {

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

std::string taskTypeName(const Task& task) {
  if (!task.declared_runner.empty()) {
    return task.declared_runner;
  }

  if (task.script && task.action == TaskAction::None) {
    return "script";
  }

  switch (task.action) {
    case TaskAction::Uses:
      return "uses";
    case TaskAction::DynamicTasks:
      return "dynamic_tasks";
    case TaskAction::Orch:
      return "actions";
    case TaskAction::Command:
      return "command";
    case TaskAction::Program:
      return "program";
    case TaskAction::None:
      break;
  }

  return "unknown";
}

void setContextEnvironment(WorkflowContext& context, const EnvMap& env) {
  for (const auto& [key, value] : env) {
    context.setValue("env." + key, value);
  }
}

} // anonymous namespace

UsesExecutor::UsesExecutor(std::unordered_map<std::string, std::string> base_environment,
                           size_t num_threads)
    : base_environment_(std::move(base_environment)), num_threads_(num_threads) {}

TaskResult UsesExecutor::execute(const Task &task, WorkflowContext &context) {
  TLOG_DEBUG("Executing uses: {}", task.name);

  try {
    const auto &params = std::get<UsesParams>(task.specifics);
    std::filesystem::path resolved = Praktor::util::resolveRelativePath(task.source_path, params.path);

    // Parse nested workflow
    std::filesystem::path base_dir = resolved.parent_path();
    Workflow nested = TaskParser::parseFileWithImports(resolved.string(), base_dir.string());
    nested.source_path = resolved.string();

    for (const auto& nested_task : nested.tasks) {
      if (nested_task.name == task.name) {
        return TaskResult(
            false, "Uses task '" + task.name +
                       "' collides with nested workflow task name '" + nested_task.name + "'");
      }
    }

    // Build isolated context for nested workflow
    auto nested_context = createIsolatedContext(task, nested, context);

    // Build environment
    EnvMap nested_env = buildNestedEnvironment(nested, *nested_context);
    setContextEnvironment(*nested_context, nested_env);

    // Execute nested workflow in isolation
    DependencyGraph<Task> graph = TaskParser::buildGraph(nested);
    WorkflowExecutor nested_executor(graph, nested.tasks, nested_env, num_threads_, false);
    nested_executor.execute(*nested_context, task.name);

    // Export outputs back to parent context
    exportOutputsToParent(task.name, *nested_context, context);

    const std::string nested_status =
        nested_context->getValueOrDefault<std::string>("workflow_status", "unknown");
    if (nested_status == "failed") {
      TaskResult result(false, "Nested workflow '" + resolved.string() + "' failed");
      result.nested_failure_context = buildNestedFailureContext(nested, *nested_context);
      if (result.nested_failure_context.has_value() &&
          !result.nested_failure_context->task_name.empty()) {
        result.error_message += " (first failed task: '" + result.nested_failure_context->task_name + "')";
      }
      return result;
    }

    return TaskResult(true);
  } catch (const std::bad_variant_access &e) {
    return TaskResult(false, "Task does not contain UsesParams: " + std::string(e.what()));
  } catch (const std::exception &e) {
    return TaskResult(false, "Uses execution failed: " + std::string(e.what()));
  }
}

TaskFailureContext UsesExecutor::buildNestedFailureContext(const Workflow& nested,
                                                           const WorkflowContext& nested_context) const {
  TaskFailureContext failure;
  const auto failed_tasks = nested_context.getFailedTasks();

  const Task* failed_task = nullptr;
  for (const auto& task : nested.tasks) {
    if (nested_context.getTaskStatus(task.name) == "failed") {
      failed_task = &task;
      break;
    }
  }

  if (!failed_task && !failed_tasks.empty()) {
    const auto candidate = std::find_if(nested.tasks.begin(), nested.tasks.end(),
                                        [&](const Task& task) {
                                          return task.name == failed_tasks.begin()->first;
                                        });
    if (candidate != nested.tasks.end()) {
      failed_task = &*candidate;
    }
  }

  if (!failed_task) {
    return failure;
  }

  failure.task_name = failed_task->name;
  failure.task_type = taskTypeName(*failed_task);
  auto error_it = failed_tasks.find(failed_task->name);
  if (error_it != failed_tasks.end()) {
    failure.error_message = error_it->second;
  }

  auto outputs = nested_context.getValueByPath("tasks." + failed_task->name + ".outputs");
  if (!outputs.is_object()) {
    if (failure.error_message.empty()) {
      failure.error_message = "Nested task failed";
    }
    return failure;
  }

  for (const auto& item : outputs.object_range()) {
    failure.captured_outputs[item.key()] = item.value();
  }

  if (outputs.contains("stdout") && outputs["stdout"].is_string()) {
    failure.stdout_data = outputs["stdout"].as<std::string>();
  }
  if (outputs.contains("stderr") && outputs["stderr"].is_string()) {
    failure.stderr_data = outputs["stderr"].as<std::string>();
  }
  if (outputs.contains("exit_code")) {
    try {
      failure.exit_code = outputs["exit_code"].as<int64_t>();
    } catch (const std::exception&) {
    }
  }
  if (failure.error_message.empty() && !failure.stderr_data.empty()) {
    failure.error_message = failure.stderr_data;
  }
  if (failure.error_message.empty()) {
    failure.error_message = "Nested task failed";
  }

  return failure;
}

std::unique_ptr<WorkflowContext> UsesExecutor::createIsolatedContext(
    const Task &task,
    const Workflow &nested,
    const WorkflowContext &parent_context) {

  // Start with task.vars passed from parent (these are the "inputs" to nested workflow)
  std::unordered_map<std::string, std::string> initial_vars;
  for (const auto &[key, value] : task.vars) {
    initial_vars[key] = substituteVariables(value, parent_context);
  }

  // Create fresh context with initial vars
  auto ctx = std::make_unique<WorkflowContext>(initial_vars);
  setContextEnvironment(*ctx, base_environment_);

  // Add nested workflow's own variables (can reference task.vars via substitution)
  for (const auto &[key, value] : nested.variables) {
    if (initial_vars.find(key) == initial_vars.end()) {
      ctx->setValue(key, substituteVariables(value, *ctx));
    }
  }

  // Load nested workflow's embedded modules
  if (!nested.embedded.empty()) {
    ctx->setEmbeddedModules(nested.embedded);
  }

  // Load nested workflow's native modules
  if (!nested.native_modules.empty()) {
    ctx->setNativeModules(nested.native_modules);
  }

  // Set source path for relative path resolution
  ctx->setSourcePath(nested.source_path);

  return ctx;
}

std::unordered_map<std::string, std::string> UsesExecutor::buildNestedEnvironment(
    const Workflow &nested,
    const WorkflowContext &nested_context) {

  EnvMap env = base_environment_;

  // Add nested workflow's env
  for (const auto &[key, value] : nested.env) {
    env[key] = substituteVariables(value, nested_context);
  }

  // Load nested workflow's dotEnv files
  for (const auto &env_file : nested.dot_env) {
    std::filesystem::path env_path = Praktor::util::resolveRelativePath(nested.source_path, env_file);
    try {
      auto parsed = Praktor::util::parseDotEnvFile(env_path);
      for (const auto &[key, value] : parsed) {
        env[key] = substituteVariables(value, nested_context);
      }
    } catch (const std::exception &e) {
      TLOG_WARN("Failed to load nested dotEnv file '{}': {}", env_path.string(), e.what());
    }
  }

  return env;
}

void UsesExecutor::exportOutputsToParent(
    const std::string &task_name,
    const WorkflowContext &nested_context,
    WorkflowContext &parent_context) {

  jsoncons::json aggregated_outputs = jsoncons::json::object();
  auto alias_outputs = nested_context.getValueByPath("tasks." + task_name + ".outputs");
  if (alias_outputs.is_object()) {
    for (const auto& item : alias_outputs.object_range()) {
      aggregated_outputs[item.key()] = item.value();
    }
  }

  auto nested_tasks = nested_context.getValueByPath("tasks");
  if (nested_tasks.is_object()) {
    jsoncons::json nested_task_summary = jsoncons::json::object();
    for (const auto& item : nested_tasks.object_range()) {
      if (item.key() == task_name) {
        continue;
      }
      nested_task_summary[item.key()] = item.value();
    }
    aggregated_outputs["nested_tasks"] = std::move(nested_task_summary);
  }

  // Also check for workflow_status
  auto status = nested_context.getValueByPath("workflow_status");
  if (!status.is_null()) {
    aggregated_outputs["workflow_status"] = status;
  }

  // Write aggregated outputs to parent context under the uses task name
  for (const auto &item : aggregated_outputs.object_range()) {
    parent_context.setTaskOutput(task_name, item.key(), item.value());
  }
}

std::unique_ptr<TaskExecutor>
createUsesExecutor(std::unordered_map<std::string, std::string> base_environment,
                   size_t num_threads) {
  return std::make_unique<UsesExecutor>(std::move(base_environment), num_threads);
}

} // namespace Praktor::Execution
