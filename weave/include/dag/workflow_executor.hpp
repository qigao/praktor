#ifndef __WORKFLOW_EXECUTOR_HPP__
#define __WORKFLOW_EXECUTOR_HPP__

#include "../util/logger.hpp"
#include "../util/thread_pool.hpp"
#include "../yml/task.hpp"
#include "enhanced_graph.hpp"
#include "workflow_context.hpp"
#include "task_executor_pool.hpp"

#include <functional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <condition_variable>

class WorkflowExecutor {
public:
    WorkflowExecutor(EnhancedGraph<Task>& graph, size_t num_threads = 1);

    void execute(WorkflowContext& context);
    
    // Helper for control flow executors to execute subtasks
    void executeSingleTaskAttempt(const Task& task, const WorkflowContext& context);

private:
    void initializeExecutors();
    void scheduleTask(const Task& task, const WorkflowContext& context);
    void onTaskFinished(const Task& task, const WorkflowContext& context, bool task_succeeded, 
                       const std::optional<TaskFailureContext>& failure_context = std::nullopt);
    void executeTaskBody(const Task& task, const WorkflowContext& context);
    bool evaluateWhenCondition(const std::string& condition, const WorkflowContext& context);
    const Task& getTaskByName(const std::string& taskName) const;
    
    std::string getTaskTypeName(const Task& task);

    EnhancedGraph<Task>& graph_;
    ThreadPool pool_;

    std::unordered_map<Task, int> in_degree_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> tasks_in_flight_ = 0;
    bool stop_on_failure_ = false;
};

#endif // __WORKFLOW_EXECUTOR_HPP__
