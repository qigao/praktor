#ifndef __DYNAMIC_TASKS_EXECUTOR_HPP__
#define __DYNAMIC_TASKS_EXECUTOR_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

#include <functional>
#include <memory>

namespace Prakter::Execution
{

/**
 * @brief Callback type for executing generated subtasks
 *
 * The DynamicTasksExecutor generates Task objects but needs the parent
 * WorkflowExecutor to actually execute them (to reuse retry logic, triggers, etc.)
 */
using SubTaskCallback = std::function<bool(const Task&, WorkflowContext&)>;

/**
 * @class DynamicTasksExecutor
 * @brief Executor for dynamic_tasks - generates and executes tasks at runtime
 *
 * This executor:
 * 1. Retrieves a JSON array from the context (items_variable)
 * 2. For each item, generates a Task from the template
 * 3. Executes generated tasks sequentially via the callback
 *
 * Example YAML:
 *   - name: deploy_services
 *     dynamic_tasks:
 *       items_variable: "{{ tasks.discover.outputs.data }}"
 *       template:
 *         name: "deploy_{{ item.name }}"
 *         command: "./deploy.sh --service {{ item.name }}"
 */
class DynamicTasksExecutor : public TaskExecutor
{
public:
    DynamicTasksExecutor() = default;
    ~DynamicTasksExecutor() override = default;

    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "dynamic_tasks"; }

    /**
     * @brief Set the callback for executing generated subtasks
     *
     * Must be called before execute(). The callback receives each generated
     * Task and should return true on success, false on failure.
     */
    void setSubTaskCallback(SubTaskCallback callback) { subtask_callback_ = std::move(callback); }

private:
    /**
     * @brief Generate a single Task from the template and item
     */
    Task generateTask(const DynamicTaskTemplate& tmpl,
                      const jsoncons::json& item,
                      size_t index,
                      WorkflowContext& context);

    /**
     * @brief Substitute {{ item }} and {{ item.field }} placeholders in a string
     */
    std::string substituteItemPlaceholders(const std::string& input,
                                           const jsoncons::json& item,
                                           size_t index);

    SubTaskCallback subtask_callback_;
};

std::unique_ptr<TaskExecutor> createDynamicTasksExecutor();

}  // namespace Prakter::Execution

#endif  // __DYNAMIC_TASKS_EXECUTOR_HPP__
