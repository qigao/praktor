#pragma once

#include "dag/task_executor.hpp"
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <memory>
#include <unordered_map>
#include <string>

namespace Praktor::Execution
{

/**
 * @class UsesExecutor
 * @brief Executor for "uses" tasks that invoke reusable workflows
 *
 * Executes nested workflows in complete isolation:
 * - Independent WorkflowContext (not shared with parent)
 * - Own variables, env, embedded modules
 * - Outputs exported back to parent after completion
 */
class UsesExecutor : public TaskExecutor
{
public:
    UsesExecutor(std::unordered_map<std::string, std::string> base_environment = {},
                 size_t num_threads = 1);
    ~UsesExecutor() override = default;

    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "uses"; }

private:
    TaskFailureContext buildNestedFailureContext(const Workflow& nested,
                                                 const WorkflowContext& nested_context) const;

    std::unordered_map<std::string, std::string> base_environment_;
    size_t num_threads_;

    // Create isolated context with task.vars as initial variables
    std::unique_ptr<WorkflowContext> createIsolatedContext(
        const Task& task,
        const Workflow& nested,
        const WorkflowContext& parent_context);

    // Build environment from nested workflow's env and dotEnv
    std::unordered_map<std::string, std::string> buildNestedEnvironment(
        const Workflow& nested,
        const WorkflowContext& nested_context);

    // Export nested workflow outputs to parent context
    void exportOutputsToParent(
        const std::string& task_name,
        const WorkflowContext& nested_context,
        WorkflowContext& parent_context);
};

std::unique_ptr<TaskExecutor> createUsesExecutor(
    std::unordered_map<std::string, std::string> base_environment = {},
    size_t num_threads = 1);

}  // namespace Praktor::Execution

