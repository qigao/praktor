#ifndef __CONTROL_FLOW_EXECUTOR_HPP__
#define __CONTROL_FLOW_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"
#include <functional>

namespace Weave::Execution
{

// Function type for executing subtasks
using SubTaskExecutor = std::function<void(const Task&, const WorkflowContext&)>;
using TaskFinder = std::function<const Task&(const std::string&)>;

class ParallelExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "parallel"; }
  
  void setSubTaskExecutor(SubTaskExecutor executor) { sub_task_executor_ = executor; }
  void setTaskFinder(TaskFinder finder) { task_finder_ = finder; }

private:
  SubTaskExecutor sub_task_executor_;
  TaskFinder task_finder_;
};

class GroupExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "group"; }
  
  void setSubTaskExecutor(SubTaskExecutor executor) { sub_task_executor_ = executor; }
  void setTaskFinder(TaskFinder finder) { task_finder_ = finder; }

private:
  SubTaskExecutor sub_task_executor_;
  TaskFinder task_finder_;
};

class ChooseExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "choose"; }
  
  void setSubTaskExecutor(SubTaskExecutor executor) { sub_task_executor_ = executor; }
  void setTaskFinder(TaskFinder finder) { task_finder_ = finder; }

private:
  SubTaskExecutor sub_task_executor_;
  TaskFinder task_finder_;
};

class DynamicTasksExecutor : public TaskExecutor
{
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "dynamic_tasks"; }
  
  void setSubTaskExecutor(SubTaskExecutor executor) { sub_task_executor_ = executor; }

private:
  SubTaskExecutor sub_task_executor_;
};

std::unique_ptr<TaskExecutor> createParallelExecutor();
std::unique_ptr<TaskExecutor> createGroupExecutor();
std::unique_ptr<TaskExecutor> createChooseExecutor();
std::unique_ptr<TaskExecutor> createDynamicTasksExecutor();

}  // namespace Weave::Execution

#endif  // __CONTROL_FLOW_EXECUTOR_HPP__