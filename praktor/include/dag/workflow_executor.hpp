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
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class WorkflowExecutor {
public:
    WorkflowExecutor(DependencyGraph<Task>& graph,
                     std::vector<Task> all_tasks,
                     std::unordered_map<std::string, std::string> base_environment = {},
                     size_t num_threads = 1,
                     bool schedule_trigger_tasks = false);

    WorkflowExecutor(DependencyGraph<Task>& graph,
                     std::unordered_map<std::string, std::string> base_environment = {},
                     size_t num_threads = 1);

    void execute(WorkflowContext& context, std::optional<std::string> alias = std::nullopt);

private:
    struct TaskExecutionOutcome {
        bool success = false;
        std::string status = "failed";
        TaskResult result;
        bool has_task_result = false;
    };

    bool executeTask(const Task& task, WorkflowContext& context, std::optional<std::string> alias = std::nullopt, bool ignore_when = false);
    TaskExecutionOutcome executeTaskInternal(const Task& task, WorkflowContext& context, std::optional<std::string> alias = std::nullopt, bool ignore_when = false);
    void setTaskExecutionStatus(const Task& task, WorkflowContext& context,
                                std::optional<std::string> alias, std::string_view status,
                                const std::string& error_message = {}) const;
    void clearTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                   std::optional<std::string> alias) const;
    void mergeTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                   std::optional<std::string> alias,
                                   const WorkflowValue& outputs) const;
    WorkflowValue getTaskOutputsSnapshot(const Task& task, const WorkflowContext& context) const;
    bool evaluateWhen(const Task& task, WorkflowContext& context) const;
    void executeTriggers(const Task& task, bool success, WorkflowContext& context);
    bool executeTriggeredTask(const Task& task, WorkflowContext& context);
    bool executeTriggeredTask(const Task& task, WorkflowContext& context,
                             std::unordered_set<std::string>& active_stack,
                             bool dependency_only);
    std::unordered_map<std::string, std::string> buildTaskEnvironment(
        const Task& task,
        const std::unordered_map<std::string, std::string>& inherited_env,
        WorkflowContext& context) const;
    const Task* findTaskByName(std::string_view task_name) const;

    DependencyGraph<Task>& graph_;
    std::unordered_map<std::string, std::string> base_environment_;
    size_t max_concurrency_;
    Praktor::Execution::TriggerExecutor trigger_executor_;

    // Executor registry: maps TaskAction to its executor implementation
    std::unordered_map<TaskAction, std::unique_ptr<TaskExecutor>> executors_;

    // Use shared thread pool instead of per-executor pool
    bool use_shared_pool_ = false;

    // Concurrency control for execute()
    std::mutex execution_mutex_;
    std::condition_variable execution_cv_;

    // Caching
    struct TaskCacheState {
        std::map<std::string, std::string> source_hashes;
        std::string action_hash;
    };
    std::unordered_map<std::string, TaskCacheState> cache_;
    std::string cache_file_;
    bool use_cache_ = true;

    void loadCache(const std::string& workflow_path);
    void saveCache();
    bool checkSkipTask(const Task& task, WorkflowContext& context);
    void updateTaskCache(const Task& task, WorkflowContext& context);

    // Execution state for DAG traversal
    struct ExecutionState {
        std::unordered_map<Task, int> in_degree;
        std::unordered_set<std::string> scheduled;
        size_t total_tasks = 0;
        size_t completed = 0;
        size_t active = 0;
        bool workflow_failed = false;
        bool should_stop = false;

        bool isFinished() const { return (should_stop && active == 0) || (completed == total_tasks); }
        bool hasPendingWork() const { return active > 0; }
    };

    bool isRegularlySchedulable(const Task& task) const;
    void initializeExecutionState(ExecutionState& state, const std::vector<Task>& nodes);
    std::vector<Task> getReadyTasks(ExecutionState& state);
    std::vector<Task> onTaskCompleted(ExecutionState& state, const Task& task, bool success);
    void scheduleTask(const Task& task, WorkflowContext& context,
                      ExecutionState& state, std::optional<std::string> alias);

    std::vector<Task> all_tasks_;
    std::unordered_map<std::string, Task> all_task_lookup_;
    std::unordered_set<std::string> trigger_only_tasks_;
    bool schedule_trigger_tasks_ = false;
};

#endif // __WORKFLOW_EXECUTOR_HPP__
