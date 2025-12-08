#ifndef __WORKFLOW_EXECUTOR_HPP__
#define __WORKFLOW_EXECUTOR_HPP__

#include "dependency_graph.hpp"
#include "task_executor.hpp"
#include "trigger_executor.hpp"
#include "workflow_context.hpp"
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class WorkflowExecutor {
public:
    WorkflowExecutor(DependencyGraph<Task>& graph,
                     std::unordered_map<std::string, std::string> base_environment = {},
                     size_t num_threads = 1);

    void execute(WorkflowContext& context, std::optional<std::string> alias = std::nullopt);

private:
    bool executeTask(const Task& task, WorkflowContext& context, std::optional<std::string> alias = std::nullopt);
    bool evaluateWhen(const Task& task, WorkflowContext& context) const;
    void executeTriggers(const Task& task, bool success, WorkflowContext& context);
    std::unordered_map<std::string, std::string> buildTaskEnvironment(
        const Task& task,
        const std::unordered_map<std::string, std::string>& inherited_env,
        WorkflowContext& context) const;

    DependencyGraph<Task>& graph_;
    std::unordered_map<std::string, std::string> base_environment_;
    size_t max_concurrency_;
    Weave::Execution::TriggerExecutor trigger_executor_;
    
    // Executor registry: maps TaskAction to its executor implementation
    std::unordered_map<TaskAction, std::unique_ptr<TaskExecutor>> executors_;
};

#endif // __WORKFLOW_EXECUTOR_HPP__
