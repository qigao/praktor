#include "dag/trigger_executor.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"

#include <sstream>

namespace Praktor {
namespace Execution {

bool TriggerExecutor::executeTriggers(const Task &task, bool success, WorkflowContext &context,
                                      const std::vector<Task> &all_tasks,
                                      ExecutionCallback callback, size_t depth) {
  if (!task.triggers || task.triggers->empty()) {
    return true;
  }

  const auto &triggers = *task.triggers;
  std::vector<TriggerAction> actions_to_execute;

  // Collect actions based on task status
  if (success) {
    actions_to_execute.insert(actions_to_execute.end(), triggers.on_success.begin(),
                              triggers.on_success.end());
  } else {
    actions_to_execute.insert(actions_to_execute.end(), triggers.on_failure.begin(),
                              triggers.on_failure.end());
  }

  // on_complete always executes
  actions_to_execute.insert(actions_to_execute.end(), triggers.on_complete.begin(),
                            triggers.on_complete.end());

  // Track visited triggers to detect cycles
  std::unordered_set<std::string> visited_triggers;
  visited_triggers.insert(task.name);  // Start with the current task

  // Execute all collected actions
  bool any_trigger_failed = false;
  for (const auto &action : actions_to_execute) {
    try {
      if (!executeTriggerAction(action, context, all_tasks, callback, visited_triggers, depth)) {
        any_trigger_failed = true;
      }
    } catch (const std::exception &e) {
      logwf("Trigger execution failed: {}", e.what());
      any_trigger_failed = true;
      // Continue executing remaining triggers but record the failure
    }
  }
  
  return !any_trigger_failed;
}

bool TriggerExecutor::executeTriggerAction(const TriggerAction &action, WorkflowContext &context,
                                           const std::vector<Task> &all_tasks,
                                           ExecutionCallback callback,
                                           std::unordered_set<std::string> &visited_triggers,
                                           size_t depth) {

  // TriggerAction is now just a task name string
  std::string task_name = substituteVariables(action, context);

  // Detect trigger cycles
  if (visited_triggers.count(task_name) > 0) {
    logef("Circular trigger dependency detected: task '{}' is already in the trigger call chain", task_name);
    throw std::runtime_error("Circular trigger dependency detected for task: " + task_name);
  }

  if (depth >= max_trigger_depth_) {
    throw std::runtime_error(
        "Trigger chain exceeded maximum depth (" + std::to_string(max_trigger_depth_) +
        "); possible circular triggers involving task '" + task_name + "'");
  }

  visited_triggers.insert(task_name);
  logdf("Executing trigger action: {}", task_name);

  // Find the task by name
  const Task *trigger_task = nullptr;
  for (const auto &t : all_tasks) {
    if (t.name == task_name) {
      trigger_task = &t;
      break;
    }
  }

  if (!trigger_task) {
    logef("Trigger task not found: {}", task_name);
    visited_triggers.erase(task_name);  // Clean up before throwing
    throw std::runtime_error("Trigger task not found: " + task_name);
  }

  logdf("Executing trigger task '{}'", task_name);
  bool success = callback(*trigger_task, depth + 1);
  if (!success) {
    logwf("Trigger task '{}' failed", task_name);
  }
  
  // Clean up visited set after execution
  visited_triggers.erase(task_name);
  return success;
}

} // namespace Execution
} // namespace Praktor
