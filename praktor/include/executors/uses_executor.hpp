#ifndef __USES_EXECUTOR_HPP__
#define __USES_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

#include <unordered_map>
#include <string>

namespace Praktor::Execution
{

/**
 * @class UsesExecutor
 * @brief Executor for "uses" tasks that invoke reusable workflows
 *
 * This executor handles nested workflow execution with proper variable scoping
 * and environment inheritance.
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
    std::unordered_map<std::string, std::string> base_environment_;
    size_t num_threads_;
};

std::unique_ptr<TaskExecutor> createUsesExecutor(
    std::unordered_map<std::string, std::string> base_environment = {},
    size_t num_threads = 1);

}  // namespace Praktor::Execution

#endif  // __USES_EXECUTOR_HPP__
