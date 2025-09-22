#ifndef __RUN_COMMAND_EXECUTOR_HPP__
#define __RUN_COMMAND_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Weave::Execution
{

class RunCommandExecutor : public TaskExecutor
{
public:
  RunCommandExecutor() = default;
  ~RunCommandExecutor() override = default;
  
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "run_command"; }

private:
  TaskResult executeProcess(const RunCommandParams& params, 
                           const WorkflowContext& context);
  void applyOutputs(const Task& task, const TaskResult& result, 
                   WorkflowContext& context);
};

std::unique_ptr<TaskExecutor> createRunCommandExecutor();

}  // namespace Weave::Execution

#endif  // __RUN_COMMAND_EXECUTOR_HPP__