#ifndef __SCRIPT_EXECUTOR_HPP__
#define __SCRIPT_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Weave::Execution
{

class ScriptExecutor : public TaskExecutor
{
public:
  ScriptExecutor() = default;
  ~ScriptExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "script"; }

private:
  void applyOutputs(const Task& task,
                    const TaskResult& result,
                    WorkflowContext& context);
};

std::unique_ptr<TaskExecutor> createScriptExecutor();

}  // namespace Weave::Execution

#endif  // __SCRIPT_EXECUTOR_HPP__
