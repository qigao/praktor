#ifndef __TRIGGER_EXECUTOR_HPP__
#define __TRIGGER_EXECUTOR_HPP__

#include "workflow_context.hpp"
#include "yml/task.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Praktor {
namespace Execution {

using ExecutionCallback = std::function<bool(const Task &)>;

/**
 * @class TriggerExecutor
 * @brief Executes event-driven triggers (on_success, on_failure, on_complete)
 */
class TriggerExecutor {
public:
  /**
   * @brief Execute triggers for a task based on its completion status
   * @param task The task that completed
   * @param success Whether the task succeeded
   * @param context The workflow context
   * @param all_tasks All tasks in the workflow (for trigger task lookup)
   * @param callback Function to execute a task
   */
  void executeTriggers(const Task &task, bool success, WorkflowContext &context,
                       const std::vector<Task> &all_tasks, ExecutionCallback callback);

private:
  void executeTriggerAction(const TriggerAction &action, WorkflowContext &context,
                            const std::vector<Task> &all_tasks, ExecutionCallback callback);
};

} // namespace Execution
} // namespace Praktor

#endif // __TRIGGER_EXECUTOR_HPP__
