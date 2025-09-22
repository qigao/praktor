#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <variant>

#ifdef _WIN32
  #define _WINSOCKAPI_
  #include <WinSock2.h>
  #include <windows.h>
#endif
#include <uv.h>

#include "dag/executor_pool_monitor.hpp"
#include "dag/executors/control_flow_executor.hpp"
#include "dag/executors/file_operation_executor.hpp"
#include "dag/executors/run_command_executor.hpp"
#include "dag/workflow_executor.hpp"
#include "dag/task_execution_exception.hpp"
#include "util/expression_evaluator.hpp"
#include "util/template_engine.hpp"
#include "util/variable_substitution.hpp"

using Weave::Execution::TaskExecutorPool;
using Weave::Util::ExpressionEvaluator;
using Weave::Util::TemplateEngine;

// Duration parser helper
std::chrono::milliseconds parseDuration(const std::string& duration_str)
{
  if (duration_str.empty() || duration_str == "0") {
    return std::chrono::milliseconds(0);
  }

  long long value = 0;
  std::string unit;

  size_t first_char = duration_str.find_first_not_of("0123456789");
  std::string num_part = duration_str.substr(0, first_char);
  unit = duration_str.substr(first_char);

  value = std::stoll(num_part);

  if (unit == "s") {
    return std::chrono::seconds(value);
  } else if (unit == "m") {
    return std::chrono::minutes(value);
  } else if (unit == "h") {
    return std::chrono::hours(value);
  } else if (unit == "ms") {
    return std::chrono::milliseconds(value);
  }

  // Default to seconds if no unit or unknown unit
  return std::chrono::seconds(std::stoll(duration_str));
}

// --- Constructor ---
WorkflowExecutor::WorkflowExecutor(EnhancedGraph<Task>& graph,
                                   size_t num_threads)
    : graph_(graph)
    , pool_(num_threads)
{
  initializeExecutors();
}

void WorkflowExecutor::initializeExecutors()
{
  auto& pool = TaskExecutorPool::getInstance();

  // Register all executor types with the pool
  pool.registerExecutor("run_command",
                        Weave::Execution::createRunCommandExecutor);
  pool.registerExecutor("create_directory",
                        Weave::Execution::createCreateDirectoryExecutor);
  pool.registerExecutor("copy_file", Weave::Execution::createCopyFileExecutor);
  pool.registerExecutor("move_file", Weave::Execution::createMoveFileExecutor);
  pool.registerExecutor("parallel", Weave::Execution::createParallelExecutor);
  pool.registerExecutor("group", Weave::Execution::createGroupExecutor);
  pool.registerExecutor("choose", Weave::Execution::createChooseExecutor);
  pool.registerExecutor("dynamic_tasks",
                        Weave::Execution::createDynamicTasksExecutor);
}

// --- Main Execution Logic ---
void WorkflowExecutor::execute(WorkflowContext& context)
{
  LOG_INFO("Starting concurrent workflow execution...");
  tasks_in_flight_ = 0;
  stop_on_failure_ = false;

  // Initialize in-degrees
  auto nodes = graph_.getNodes();
  for (const auto& node : nodes) {
    in_degree_[node] = graph_.getInDegree(node);
  }

  // Schedule initial tasks (in-degree 0)
  for (const auto& node : nodes) {
    if (in_degree_[node] == 0) {
      LOG_DEBUG("Scheduling initial task: " + node.name);
      scheduleTask(node, context);
    }
  }

  // Wait for all tasks to complete
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return tasks_in_flight_ == 0; });
  }

  if (stop_on_failure_) {
    LOG_ERROR("Workflow execution failed.");
    context.setValue("workflow_status", "failed");
  } else {
    LOG_INFO("Workflow execution completed successfully.");
    context.setValue("workflow_status", "success");

    // Log executor pool performance stats
    Weave::Execution::ExecutorPoolMonitor::logPoolStats();
  }
}

void WorkflowExecutor::scheduleTask(const Task& task,
                                    const WorkflowContext& context)
{
  LOG_DEBUG("Attempting to schedule task: " + task.name);
  if (stop_on_failure_) {
    return;
  }

  tasks_in_flight_++;
  pool_.enqueue(
      [this, task, context]
      {
        LOG_DEBUG("Task '" + task.name + "' entered thread pool.");
        try {
          if (!evaluateWhenCondition(task.when, context)) {
            LOG_INFO("Skipping task '" + task.name
                     + "' due to 'when' condition.");
          } else {
            LOG_INFO("Starting task: " + task.name);
            executeTaskBody(task, context);
            LOG_INFO("Completed task: " + task.name);
          }

          onTaskFinished(task, context, true);
        } catch (const TaskExecutionException& e) {
          LOG_ERROR("Task '" + task.name + "' failed: " + e.what());
          stop_on_failure_ = true;

          // Use the detailed failure context from the exception
          onTaskFinished(task, context, false, e.getFailureContext());
          return;
        } catch (const std::exception& e) {
          LOG_ERROR("Task '" + task.name + "' failed: " + e.what());
          stop_on_failure_ = true;

          // Create basic failure context for generic exceptions
          TaskFailureContext failure_context;
          failure_context.task_name = task.name;
          failure_context.task_type = getTaskTypeName(task);
          failure_context.error_message = e.what();
          failure_context.exit_code = -1;  // Default for generic failures

          onTaskFinished(task, context, false, failure_context);
          return;
        }
      });
}

void WorkflowExecutor::onTaskFinished(const Task& task,
                                      const WorkflowContext& context,
                                      bool task_succeeded,
                                      const std::optional<TaskFailureContext>& failure_context)
{
  std::vector<Task> successors_to_schedule;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!stop_on_failure_) {  // Only schedule new tasks if we haven't failed
      for (const auto& successor : graph_.getEdges(task)) {
        in_degree_[successor]--;
        if (in_degree_[successor] == 0) {
          successors_to_schedule.push_back(successor);
        }
      }
    }
  }

  // Schedule on_success/on_failure tasks with proper context
  if (task_succeeded) {
    for (const auto& success_task_name : task.on_success) {
      try {
        const Task& success_task = getTaskByName(success_task_name);
        scheduleTask(success_task, context);
      } catch (const std::runtime_error& e) {
        LOG_ERROR("Error scheduling on_success task '" + success_task_name
                  + "': " + e.what());
        stop_on_failure_ = true;
      }
    }
  } else {  // Task failed
    // Set up failure context for on_failure tasks
    auto contextWithFailure = const_cast<WorkflowContext&>(context);
    if (failure_context.has_value()) {
      contextWithFailure.setFailureContext(failure_context.value());
    }

    for (const auto& failure_task_name : task.on_failure) {
      try {
        const Task& failure_task = getTaskByName(failure_task_name);
        LOG_INFO("Executing on_failure task '" + failure_task_name
                + "' with failure context from '" + task.name + "'");
        scheduleTask(failure_task, contextWithFailure);
      } catch (const std::runtime_error& e) {
        LOG_ERROR("Error scheduling on_failure task '" + failure_task_name
                  + "': " + e.what());
        stop_on_failure_ = true;
      }
    }

    // Clean up failure context after scheduling all failure tasks
    if (failure_context.has_value()) {
      contextWithFailure.clearFailureContext();
    }
  }

  for (const auto& successor : successors_to_schedule) {
    scheduleTask(successor, context);
  }

  tasks_in_flight_--;
  cv_.notify_one();
}

const Task& WorkflowExecutor::getTaskByName(const std::string& taskName) const
{
  for (const auto& task : graph_.getNodes()) {
    if (task.name == taskName) {
      return task;
    }
  }
  throw std::runtime_error("Task not found in graph: " + taskName);
}

// --- Task Body Execution ---

void WorkflowExecutor::executeTaskBody(const Task& task,
                                       const WorkflowContext& context)
{
  if (!task.each.items.empty()) {
    LOG_INFO("Executing task '" + task.name + "' with each loop over "
             + std::to_string(task.each.items.size()) + " items");

    // Execute the task for each item in the list
    for (size_t i = 0; i < task.each.items.size(); ++i) {
      const std::string& item = task.each.items[i];

      // Create a modified context with the current item and task variables
      WorkflowContext itemContext = context;  // Copy the context
      itemContext.setValue(task.each.as, item);

      // Add task-level variables to the context
      for (const auto& [key, value] : task.vars) {
        itemContext.setValue(key, value);
      }

      LOG_INFO("Executing iteration " + std::to_string(i + 1) + "/"
               + std::to_string(task.each.items.size()) + " with "
               + task.each.as + "=" + item);

      // Execute the task with the item-specific context
      executeSingleTaskAttempt(task,
                               const_cast<const WorkflowContext&>(itemContext));
    }
  } else {
    // Create context with task variables merged
    WorkflowContext taskContext = context;  // Copy the context
    for (const auto& [key, value] : task.vars) {
      taskContext.setValue(key, value);
    }

    executeSingleTaskAttempt(task, taskContext);
  }
}

void WorkflowExecutor::executeSingleTaskAttempt(const Task& task,
                                                const WorkflowContext& context)
{
  for (int i = 0; i <= task.retries.count; ++i) {
    try {
      // Get task type name and get executor from pool
      std::string taskType = getTaskTypeName(task);
      auto& pool = TaskExecutorPool::getInstance();
      auto executor = pool.getExecutor(taskType);  // Cached, no new/delete!

      if (!executor) {
        throw std::runtime_error("No executor found for task type: "
                                 + taskType);
      }

      // Set up callbacks for control flow executors
      if (taskType == "parallel" || taskType == "group" || taskType == "choose")
      {
        auto controlFlowExecutor =
            std::dynamic_pointer_cast<Weave::Execution::ParallelExecutor>(
                executor);
        if (controlFlowExecutor) {
          controlFlowExecutor->setSubTaskExecutor(
              [this](const Task& subtask, const WorkflowContext& ctx)
              { this->executeSingleTaskAttempt(subtask, ctx); });
          controlFlowExecutor->setTaskFinder(
              [this](const std::string& taskName) -> const Task&
              { return this->getTaskByName(taskName); });
        }

        auto groupExecutor =
            std::dynamic_pointer_cast<Weave::Execution::GroupExecutor>(
                executor);
        if (groupExecutor) {
          groupExecutor->setSubTaskExecutor(
              [this](const Task& subtask, const WorkflowContext& ctx)
              { this->executeSingleTaskAttempt(subtask, ctx); });
          groupExecutor->setTaskFinder(
              [this](const std::string& taskName) -> const Task&
              { return this->getTaskByName(taskName); });
        }

        auto chooseExecutor =
            std::dynamic_pointer_cast<Weave::Execution::ChooseExecutor>(
                executor);
        if (chooseExecutor) {
          chooseExecutor->setSubTaskExecutor(
              [this](const Task& subtask, const WorkflowContext& ctx)
              { this->executeSingleTaskAttempt(subtask, ctx); });
          chooseExecutor->setTaskFinder(
              [this](const std::string& taskName) -> const Task&
              { return this->getTaskByName(taskName); });
        }
      } else if (taskType == "dynamic_tasks") {
        auto dynamicExecutor =
            std::dynamic_pointer_cast<Weave::Execution::DynamicTasksExecutor>(
                executor);
        if (dynamicExecutor) {
          dynamicExecutor->setSubTaskExecutor(
              [this](const Task& subtask, const WorkflowContext& ctx)
              { this->executeSingleTaskAttempt(subtask, ctx); });
        }
      }

      LOG_DEBUG("Calling execute on executor for task: " + task.name + " (type: " + taskType + ")");
      // Execute the task (executor is reused, no allocation overhead!)
      auto result =
          executor->execute(task, const_cast<WorkflowContext&>(context));

      if (!result.success) {
        // Create detailed failure context
        TaskFailureContext failure_context;
        failure_context.task_name = task.name;
        failure_context.task_type = taskType;
        failure_context.exit_code = result.exit_code;
        failure_context.stdout_data = result.stdout_data;
        failure_context.stderr_data = result.stderr_data;
        failure_context.error_message = result.error_message;

        // TODO: Capture any outputs that were set before failure
        // This would require checking what variables were added to context

        throw TaskExecutionException(result.error_message, failure_context);
      }

      return;  // Success

    } catch (const std::exception& e) {
      if (i < task.retries.count) {
        LOG_WARNING("Task '" + task.name + "' failed. Retrying ("
                    + std::to_string(i + 1) + "/"
                    + std::to_string(task.retries.count)
                    + ")... Error: " + std::string(e.what()));
        std::this_thread::sleep_for(parseDuration(task.retries.delay));
      } else {
        throw;  // Final attempt failed
      }
    }
  }
}

std::string WorkflowExecutor::getTaskTypeName(const Task& task)
{
  return std::visit(
      [](const auto& params) -> std::string
      {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, RunCommandParams>) {
          return "run_command";
        } else if constexpr (std::is_same_v<T, CreateDirectoryParams>) {
          return "create_directory";
        } else if constexpr (std::is_same_v<T, CopyFileParams>) {
          return "copy_file";
        } else if constexpr (std::is_same_v<T, MoveFileParams>) {
          return "move_file";
        } else if constexpr (std::is_same_v<T, ParallelParams>) {
          return "parallel";
        } else if constexpr (std::is_same_v<T, GroupParams>) {
          return "group";
        } else if constexpr (std::is_same_v<T, ChooseParams>) {
          return "choose";
        } else if constexpr (std::is_same_v<T, DynamicTasksParams>) {
          return "dynamic_tasks";
        } else {
          return "unknown";
        }
      },
      task.specifics);
}

bool WorkflowExecutor::evaluateWhenCondition(const std::string& condition,
                                             const WorkflowContext& context)
{
  if (condition.empty()) {
    return true;
  }
  ExpressionEvaluator evaluator;
  return evaluator.evaluate(condition, context);
}
