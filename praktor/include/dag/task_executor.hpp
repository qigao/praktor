#ifndef __TASK_EXECUTOR_HPP__
#define __TASK_EXECUTOR_HPP__

#include "workflow_context.hpp"
#include "yml/task.hpp"

#include <optional>
#include <string>

/**
 * @struct TaskResult
 * @brief Result of task execution
 */
struct TaskResult {
  bool success;
  std::string error_message;
  int64_t exit_code = 0;
  std::string stdout_data;
  std::string stderr_data;
  bool output_streamed_live = false;
  std::optional<TaskFailureContext> nested_failure_context;

  TaskResult(bool s = true, std::string msg = "")
      : success(s), error_message(std::move(msg)), exit_code(s ? 0 : -1) {}
};

/**
 * @class TaskExecutor
 * @brief Abstract base class for all task executors
 *
 * Linus principle: "Good taste eliminates switch/case through polymorphism"
 *
 * Before: WorkflowExecutor had a switch/case for each task type
 * After: Each task type has its own executor implementing this interface
 *
 * Adding a new task type? Create a new executor class. Zero changes to WorkflowExecutor.
 */
class TaskExecutor {
public:
  /**
   * @brief Execute a task
   * @param task The task to execute
   * @param context The workflow context for variable access
   * @return TaskResult indicating success/failure
   */
  virtual TaskResult execute(const Task &task, WorkflowContext &context) = 0;

  /**
   * @brief Get the task type this executor handles
   * @return Task type name (e.g., "command", "script", "uses")
   */
  virtual std::string getTaskType() const = 0;

  virtual ~TaskExecutor() = default;

protected:
  TaskExecutor() = default;
  TaskExecutor(const TaskExecutor &) = default;
  TaskExecutor &operator=(const TaskExecutor &) = default;
  TaskExecutor(TaskExecutor &&) noexcept = default;
  TaskExecutor &operator=(TaskExecutor &&) noexcept = default;
};

#endif // __TASK_EXECUTOR_HPP__
