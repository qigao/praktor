#ifndef __FILE_OPERATION_EXECUTOR_HPP__
#define __FILE_OPERATION_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Praktor::Execution
{

class CreateDirectoryExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "create_directory"; }
};

class CopyFileExecutor : public TaskExecutor  
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "copy_file"; }
};

class MoveFileExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "move_file"; }
};

std::unique_ptr<TaskExecutor> createCreateDirectoryExecutor();
std::unique_ptr<TaskExecutor> createCopyFileExecutor();
std::unique_ptr<TaskExecutor> createMoveFileExecutor();

}  // namespace Praktor::Execution

#endif  // __FILE_OPERATION_EXECUTOR_HPP__
