#ifndef __TASK_EXECUTOR_HPP__
#define __TASK_EXECUTOR_HPP__

#include <memory>
#include <string>
#include "dag/workflow_context.hpp"
#include "yml/task.hpp"

namespace Weave::Execution
{

struct TaskResult
{
  bool success = false;
  std::string error_message;
  
  // Command execution specific fields
  int64_t exit_code = 0;
  std::string stdout_data;
  std::string stderr_data;
  
  TaskResult(bool success_) : success(success_) {}
  TaskResult(bool success_, const std::string& error) 
      : success(success_), error_message(error) {}
};

class TaskExecutor
{
public:
  virtual ~TaskExecutor() = default;
  
  virtual TaskResult execute(const Task& task, WorkflowContext& context) = 0;
  virtual std::string getTaskType() const = 0;
};

}  // namespace Weave::Execution

#endif  // __TASK_EXECUTOR_HPP__