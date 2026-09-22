#pragma once

#include "dependency_graph.hpp"
#include "task_executor.hpp"
#include "trigger_executor.hpp"
#include "workflow_context.hpp"
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <memory>
#include <future>
#include <thread>
#include <map>
#include <mutex>
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
                     bool schedule_trigger_tasks = false,
                     size_t max_trigger_depth = Praktor::Execution::kMaxTriggerChainDepth);

    WorkflowExecutor(DependencyGraph<Task>& graph,
                     std::unordered_map<std::string, std::string> base_environment = {},
                     size_t num_threads = 1,
                     size_t max_trigger_depth = Praktor::Execution::kMaxTriggerChainDepth);

    void execute(WorkflowContext& context, std::optional<std::string> alias = std::nullopt);

private:
    struct TaskExecutionOutcome {
        bool success = false;
        std::string status = "failed";
        TaskResult result;
        bool has_task_result = false;
    };

    bool executeScheduledTaskOnce(const Task& task, WorkflowContext& context,
                                  std::optional<std::string> alias, bool ignore_when = false,
                                  size_t trigger_depth = 0);
    bool executeTask(const Task& task, WorkflowContext& context, std::optional<std::string> alias = std::nullopt, bool ignore_when = false, size_t trigger_depth = 0);
    TaskExecutionOutcome executeTaskInternal(const Task& task, WorkflowContext& context, std::optional<std::string> alias = std::nullopt, bool ignore_when = false, bool report_terminal_status = true);
    void setTaskExecutionStatus(const Task& task, WorkflowContext& context,
                                std::optional<std::string> alias, std::string_view status,
                                const std::string& error_message = {},
                                const TaskFailureContext* failure_snapshot = nullptr) const;
    void clearTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                   std::optional<std::string> alias) const;
    void mergeTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                   std::optional<std::string> alias,
                                   const WorkflowValue& outputs) const;
    WorkflowValue getTaskOutputsSnapshot(const Task& task, const WorkflowContext& context) const;
    bool evaluateWhen(const Task& task, WorkflowContext& context) const;
    bool executeTriggers(const Task& task, bool success, WorkflowContext& context, size_t trigger_depth = 0);
    bool executeTriggeredTask(const Task& task, WorkflowContext& context, size_t trigger_depth);
    bool executeTriggeredTask(const Task& task, WorkflowContext& context,
                             std::unordered_set<std::string>& active_stack,
                             bool dependency_only, size_t trigger_depth);
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
    struct ScheduledTaskRun {
        std::shared_future<bool> completion;
        std::thread::id owner;
    };
    // Shared by DAG scheduling and inline trigger dependencies for this run.
    std::unordered_map<std::string, ScheduledTaskRun> scheduled_task_runs_;

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
    void mergeForkedContext(WorkflowContext& target, const WorkflowContext& child);

    std::vector<Task> all_tasks_;
    std::unordered_map<std::string, Task> all_task_lookup_;
    std::unordered_set<std::string> trigger_only_tasks_;
    // Task names for which a __TASK__:RUNNING status line was already emitted in
    // this workflow run. A task can execute many times (each/matrix iterations,
    // trigger re-entry); RUNNING is reported once per run to avoid log flooding.
    std::unordered_set<std::string> running_reported_;
    std::mutex status_log_mutex_;
    bool schedule_trigger_tasks_ = false;
};
