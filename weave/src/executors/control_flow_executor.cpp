#include "executors/control_flow_executor.hpp"
#include "util/logger.hpp"
#include "util/expression_evaluator.hpp"
#include "util/template_engine.hpp"

#include <future>
#include <algorithm>
#include <variant>

namespace Weave::Execution
{

TaskResult ParallelExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<ParallelParams>(task.specifics);

    LOG_INFO("Executing parallel block: " + task.name);

    if (!sub_task_executor_ || !task_finder_) {
      return TaskResult(false, "ParallelExecutor requires sub_task_executor and task_finder");
    }

    // Execute all tasks in the parallel block concurrently
    std::vector<std::future<void>> futures;
    std::vector<std::exception_ptr> exceptions;
    std::mutex exception_mutex;

    for (const auto& sub_task : params.tasks) {
      auto future = std::async(std::launch::async, [&, sub_task]() {
        try {
          // The sub_task is already a full Task object, no need for task_finder_
          sub_task_executor_(sub_task, context);
        } catch (...) {
          std::lock_guard<std::mutex> lock(exception_mutex);
          exceptions.push_back(std::current_exception());
        }
      });
      futures.push_back(std::move(future));
    }

    // Wait for all parallel tasks to complete
    for (auto& future : futures) {
      future.get();
    }

    // Check if any tasks failed
    if (!exceptions.empty()) {
      try {
        std::rethrow_exception(exceptions[0]);
      } catch (const std::exception& e) {
        return TaskResult(false, "Parallel task failed: " + std::string(e.what()));
      }
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, "Task does not contain ParallelParams: " + std::string(e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, "Parallel execution failed: " + std::string(e.what()));
  }
}

TaskResult GroupExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<GroupParams>(task.specifics);

    LOG_INFO("Executing group: " + task.name);

    if (!sub_task_executor_ || !task_finder_) {
      return TaskResult(false, "GroupExecutor requires sub_task_executor and task_finder");
    }

    // Execute all tasks in the group sequentially
    for (const auto& sub_task : params.tasks) {
      // The sub_task is already a full Task object, no need for task_finder_
      sub_task_executor_(sub_task, context);
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, "Task does not contain GroupParams: " + std::string(e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, "Group execution failed: " + std::string(e.what()));
  }
}

TaskResult ChooseExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<ChooseParams>(task.specifics);

    LOG_INFO("Executing choose block: " + task.name);

    if (!sub_task_executor_ || !task_finder_) {
      return TaskResult(false, "ChooseExecutor requires sub_task_executor and task_finder");
    }

    Weave::Util::ExpressionEvaluator evaluator;
    bool branch_executed = false;

    // Check each branch condition
    for (const auto& branch : params.branches) {
      if (branch.when.empty() || evaluator.evaluate(branch.when, context)) {
        LOG_INFO("Choose branch condition met. Executing branch.");

        // Execute all tasks in this branch
        for (const auto& sub_task : branch.tasks) {
          // The sub_task is already a full Task object, no need for task_finder_
          sub_task_executor_(sub_task, context);
        }

        branch_executed = true;
        break;  // Execute only the first matching branch
      }
    }

    // Execute default tasks if no branch matched
    if (!branch_executed && !params.default_task_names.empty()) {
      LOG_INFO("No choose branch condition met. Executing default tasks.");

      for (const auto& task_name : params.default_task_names) {
        const Task& target_task = task_finder_(task_name);
        sub_task_executor_(target_task, context);
      }
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, "Task does not contain ChooseParams: " + std::string(e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, "Choose execution failed: " + std::string(e.what()));
  }
}

TaskResult DynamicTasksExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<DynamicTasksParams>(task.specifics);

    LOG_INFO("Executing dynamic_tasks: " + task.name);

    if (!sub_task_executor_) {
      return TaskResult(false, "DynamicTasksExecutor requires sub_task_executor");
    }

    // Get the items JSON array from context
    std::string items_var = params.items_variable;

    // Remove {{ }} if present
    if (items_var.find("{{") == 0 && items_var.find("}}") == items_var.length() - 2) {
      items_var = items_var.substr(2, items_var.length() - 4);
    }

    WorkflowValue items_json;
    try {
      items_json = context.getValue<WorkflowValue>(items_var);
    } catch (const std::exception& e) {
      return TaskResult(false, "Dynamic tasks items variable '" + items_var
                        + "' not found in context: " + std::string(e.what()));
    }

    // Generate tasks from template
    std::vector<Task> generated_tasks =
        Weave::Util::TemplateEngine::generateTasks(params.task_template, items_json, context);

    LOG_INFO("Generated " + std::to_string(generated_tasks.size()) + " tasks from template");

    // Execute generated tasks sequentially
    for (const auto& generated_task : generated_tasks) {
      LOG_INFO("Executing generated task: " + generated_task.name);
      sub_task_executor_(generated_task, context);
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, "Task does not contain DynamicTasksParams: " + std::string(e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, "DynamicTasks execution failed: " + std::string(e.what()));
  }
}

// Factory functions
std::unique_ptr<TaskExecutor> createParallelExecutor()
{
  return std::make_unique<ParallelExecutor>();
}

std::unique_ptr<TaskExecutor> createGroupExecutor()
{
  return std::make_unique<GroupExecutor>();
}

std::unique_ptr<TaskExecutor> createChooseExecutor()
{
  return std::make_unique<ChooseExecutor>();
}

std::unique_ptr<TaskExecutor> createDynamicTasksExecutor()
{
  return std::make_unique<DynamicTasksExecutor>();
}

}  // namespace Weave::Execution
