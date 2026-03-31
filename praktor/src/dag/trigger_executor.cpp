#include "dag/trigger_executor.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"

#include <sstream>

namespace Praktor {
namespace Execution {

void TriggerExecutor::executeTriggers(const Task &task, bool success, WorkflowContext &context,
                                      const std::vector<Task> &all_tasks,
                                      ExecutionCallback callback) {
  if (!task.triggers || task.triggers->empty()) {
    return;
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

  // Execute all collected actions
  for (const auto &action : actions_to_execute) {
    try {
      executeTriggerAction(action, context, all_tasks, callback);
    } catch (const std::exception &e) {
      logw("Trigger execution failed: {}", e.what());
    }
  }
}

void TriggerExecutor::executeTriggerAction(const TriggerAction &action, WorkflowContext &context,
                                           const std::vector<Task> &all_tasks,
                                           ExecutionCallback callback) {

  // TriggerAction is now just a task name string
  std::string task_name = substituteVariables(action, context);

  logd("Executing trigger action: {}", task_name);

  // Find the task by name
  const Task *trigger_task = nullptr;
  for (const auto &t : all_tasks) {
    if (t.name == task_name) {
      trigger_task = &t;
      break;
    }
  }

  if (!trigger_task) {
    loge("Trigger task not found: {}", task_name);
    throw std::runtime_error("Trigger task not found: " + task_name);
  }

  logd("Executing trigger task '{}'", task_name);
  bool success = callback(*trigger_task);
  if (!success) {
    logw("Trigger task '{}' failed", task_name);
  }
}

} // namespace Execution
} // namespace Praktor
