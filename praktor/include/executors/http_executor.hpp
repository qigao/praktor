#ifndef __HTTP_EXECUTOR_HPP__
#define __HTTP_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Praktor::Execution
{

class HttpExecutor : public TaskExecutor
{
public:
  HttpExecutor() = default;
  ~HttpExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "http"; }

private:
  void applyOutputs(const Task& task, const TaskResult& result,
                   WorkflowContext& context);
};

std::unique_ptr<TaskExecutor> createHttpExecutor();

}  // namespace Praktor::Execution

#endif  // __HTTP_EXECUTOR_HPP__
