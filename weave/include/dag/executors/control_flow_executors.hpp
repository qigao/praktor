#ifndef __CONTROL_FLOW_EXECUTORS_HPP__
#define __CONTROL_FLOW_EXECUTORS_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"
#include "util/thread_pool.hpp"

namespace Weave::Execution {

// Forward declaration
class WorkflowExecutor;

/**
 * @brief Executor for parallel tasks
 */
class ParallelExecutor : public TaskExecutor {
public:
    explicit ParallelExecutor(WorkflowExecutor& workflow_executor, ThreadPool& thread_pool);
    
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "parallel"; }

private:
    WorkflowExecutor& workflow_executor_;
    ThreadPool& thread_pool_;
};

/**
 * @brief Executor for group tasks (sequential execution)
 */
class GroupExecutor : public TaskExecutor {
public:
    explicit GroupExecutor(WorkflowExecutor& workflow_executor);
    
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "group"; }

private:
    WorkflowExecutor& workflow_executor_;
};

/**
 * @brief Executor for choose tasks (conditional branching)
 */
class ChooseExecutor : public TaskExecutor {
public:
    explicit ChooseExecutor(WorkflowExecutor& workflow_executor);
    
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "choose"; }

private:
    WorkflowExecutor& workflow_executor_;
    
    /**
     * @brief Evaluate a when condition
     */
    bool evaluateWhenCondition(const std::string& condition, const WorkflowContext& context);
};

/**
 * @brief Executor for dynamic_tasks (template-based task generation)
 */
class DynamicTasksExecutor : public TaskExecutor {
public:
    explicit DynamicTasksExecutor(WorkflowExecutor& workflow_executor);
    
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "dynamic_tasks"; }

private:
    WorkflowExecutor& workflow_executor_;
};

// Factory functions that need WorkflowExecutor reference
std::unique_ptr<TaskExecutor> createParallelExecutor(WorkflowExecutor& workflow_executor, ThreadPool& thread_pool);
std::unique_ptr<TaskExecutor> createGroupExecutor(WorkflowExecutor& workflow_executor);
std::unique_ptr<TaskExecutor> createChooseExecutor(WorkflowExecutor& workflow_executor);
std::unique_ptr<TaskExecutor> createDynamicTasksExecutor(WorkflowExecutor& workflow_executor);

} // namespace Weave::Execution

#endif // __CONTROL_FLOW_EXECUTORS_HPP__