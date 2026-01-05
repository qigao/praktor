#ifndef __COMMAND_EXECUTOR_HPP__
#define __COMMAND_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Praktor::Execution
{

class CommandExecutor : public TaskExecutor
{
public:
  CommandExecutor() = default;
  ~CommandExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "command"; }

private:
  TaskResult executeProcess(const RunCommandParams& params,
                           const WorkflowContext& context);
  void applyOutputs(const Task& task, const TaskResult& result,
                   WorkflowContext& context);
};

std::unique_ptr<TaskExecutor> createCommandExecutor();

}  // namespace Praktor::Execution

#endif  // __COMMAND_EXECUTOR_HPP__
