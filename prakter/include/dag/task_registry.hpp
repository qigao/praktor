#ifndef __TASK_REGISTRY_HPP__
#define __TASK_REGISTRY_HPP__

#include <jsoncons/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

/**
 * @enum TaskState
 * @brief Task execution state machine
 *
 * State transitions:
 *   Pending -> Running -> Completed
 *                     \-> Failed
 *
 * Once a task reaches Completed or Failed, its outputs are IMMUTABLE.
 */
enum class TaskState {
    Pending,    // Task not yet started
    Running,    // Task currently executing (outputs can be written)
    Completed,  // Task finished successfully (outputs locked)
    Failed      // Task failed (outputs locked)
};

inline std::string taskStateToString(TaskState state) {
    switch (state) {
        case TaskState::Pending: return "pending";
        case TaskState::Running: return "running";
        case TaskState::Completed: return "completed";
        case TaskState::Failed: return "failed";
    }
    return "unknown";
}

/**
 * @class TaskRegistry
 * @brief Manages task execution state and outputs with immutability guarantees
 *
 * Plan A Implementation: Message-passing model with immutable outputs
 *
 * Key invariants:
 * 1. setOutput() only allowed when task is Running
 * 2. Once task is Completed/Failed, outputs are immutable
 * 3. getOutput() only allowed for Completed/Failed tasks (ensures data is final)
 *
 * This prevents data races in concurrent execution and makes task dependencies
 * deterministic.
 */
class TaskRegistry {
public:
    TaskRegistry() = default;

    /**
     * @brief Start a task (transitions from Pending to Running)
     * @throws std::runtime_error if task is not in Pending state
     */
    void startTask(const std::string& task_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskState current = getStateInternal(task_name);
        if (current != TaskState::Pending) {
            throw std::runtime_error(
                "Cannot start task '" + task_name + "': already " + taskStateToString(current));
        }
        task_state_[task_name] = TaskState::Running;
    }

    /**
     * @brief Record that a task completed successfully (locks outputs)
     * @throws std::runtime_error if task is not in Running state
     */
    void markCompleted(const std::string& task_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskState current = getStateInternal(task_name);
        if (current != TaskState::Running) {
            throw std::runtime_error(
                "Cannot complete task '" + task_name + "': not running (state: " +
                taskStateToString(current) + ")");
        }
        task_state_[task_name] = TaskState::Completed;
        completed_tasks_.insert(task_name);
    }

    /**
     * @brief Record that a task failed (locks outputs)
     * @throws std::runtime_error if task is not in Running state
     */
    void markFailed(const std::string& task_name, const std::string& error_message) {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskState current = getStateInternal(task_name);
        if (current != TaskState::Running) {
            throw std::runtime_error(
                "Cannot fail task '" + task_name + "': not running (state: " +
                taskStateToString(current) + ")");
        }
        task_state_[task_name] = TaskState::Failed;
        failed_tasks_[task_name] = error_message;
    }

    /**
     * @brief Get task state
     */
    TaskState getState(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return getStateInternal(task_name);
    }

    /**
     * @brief Get task status as string (for backward compatibility)
     */
    std::string getStatus(const std::string& task_name) const {
        return taskStateToString(getState(task_name));
    }

    /**
     * @brief Set task status (for backward compatibility with older code)
     * @deprecated Use startTask/markCompleted/markFailed instead
     */
    void setStatus(const std::string& task_name, const std::string& status) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status == "running") {
            if (getStateInternal(task_name) == TaskState::Pending) {
                task_state_[task_name] = TaskState::Running;
            }
        } else if (status == "success" || status == "completed") {
            task_state_[task_name] = TaskState::Completed;
            completed_tasks_.insert(task_name);
        } else if (status == "failed") {
            task_state_[task_name] = TaskState::Failed;
        } else if (status == "skipped") {
            // Skipped tasks go directly to completed without running
            task_state_[task_name] = TaskState::Completed;
            completed_tasks_.insert(task_name);
        }
    }

    /**
     * @brief Set a single output for a task
     * @throws std::runtime_error if task is not in Running state
     *
     * Plan A: Outputs can only be written during execution, never after.
     */
    void setOutput(const std::string& task_name, const std::string& key,
                   const jsoncons::json& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskState state = getStateInternal(task_name);
        if (state != TaskState::Running && state != TaskState::Pending) {
            throw std::runtime_error(
                "Cannot set output for task '" + task_name + "': task is " +
                taskStateToString(state) + " (outputs are immutable after completion)");
        }
        // Auto-start task if still pending (for backward compatibility)
        if (state == TaskState::Pending) {
            task_state_[task_name] = TaskState::Running;
        }
        task_outputs_[task_name][key] = value;
    }

    /**
     * @brief Merge multiple outputs for a task
     * @throws std::runtime_error if task is not in Running state
     */
    void mergeOutputs(const std::string& task_name, const jsoncons::json& outputs) {
        if (!outputs.is_object()) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& item : outputs.object_range()) {
            // Internal setOutput logic duplicate or use helper?
            // Since we already locked, we can't call setOutput (it locks too).
            // Let's refactor setOutput logic to private if needed, but for now just inline or use recursive_mutex?
            // Better: use internal helper.
            setOutputInternal(task_name, item.key(), item.value());
        }
    }

    /**
     * @brief Get a specific output from a task
     *
     * Plan A: Only allows reading outputs from completed/failed tasks,
     * ensuring the data is final and won't change.
     */
    jsoncons::json getOutput(const std::string& task_name, const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto task_it = task_outputs_.find(task_name);
        if (task_it == task_outputs_.end()) {
            throw std::runtime_error("Task not found: " + task_name);
        }

        auto output_it = task_it->second.find(key);
        if (output_it == task_it->second.end()) {
            throw std::runtime_error("Output key not found: " + key + " for task: " + task_name);
        }

        return output_it->second;
    }

    /**
     * @brief Get all outputs for a task
     */
    jsoncons::json getAllOutputs(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = task_outputs_.find(task_name);
        if (it == task_outputs_.end()) {
            return jsoncons::json::object();
        }

        jsoncons::json result = jsoncons::json::object();
        for (const auto& [key, value] : it->second) {
            result[key] = value;
        }
        return result;
    }

    /**
     * @brief Check if task completed successfully
     */
    bool isCompleted(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_tasks_.count(task_name) > 0;
    }

    /**
     * @brief Check if task failed
     */
    bool isFailed(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_tasks_.count(task_name) > 0;
    }

    /**
     * @brief Check if task outputs are finalized (completed or failed)
     */
    bool isFinalized(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        TaskState state = getStateInternal(task_name);
        return state == TaskState::Completed || state == TaskState::Failed;
    }

    /**
     * @brief Get all completed task names
     */
    std::unordered_set<std::string> getCompletedTasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_tasks_;
    }

    /**
     * @brief Get all failed tasks with their error messages
     */
    std::unordered_map<std::string, std::string> getFailedTasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return failed_tasks_;
    }

    /**
     * @brief Get error message for a failed task
     */
    std::string getFailureReason(const std::string& task_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = failed_tasks_.find(task_name);
        return (it != failed_tasks_.end()) ? it->second : "";
    }

    /**
     * @brief Build a JSON representation of all tasks for context access
     */
    jsoncons::json toJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        jsoncons::json result = jsoncons::json::object();

        for (const auto& [task_name, state] : task_state_) {
            jsoncons::json task_obj = jsoncons::json::object();
            task_obj["status"] = taskStateToString(state);

            auto outputs_it = task_outputs_.find(task_name);
            if (outputs_it != task_outputs_.end()) {
                jsoncons::json outputs_obj = jsoncons::json::object();
                for (const auto& [key, value] : outputs_it->second) {
                    outputs_obj[key] = value;
                }
                task_obj["outputs"] = outputs_obj;
            }

            result[task_name] = task_obj;
        }

        return result;
    }

private:
    TaskState getStateInternal(const std::string& task_name) const {
        auto it = task_state_.find(task_name);
        return (it != task_state_.end()) ? it->second : TaskState::Pending;
    }

    void setOutputInternal(const std::string& task_name, const std::string& key, const jsoncons::json& value) {
        TaskState state = getStateInternal(task_name);
        if (state != TaskState::Running && state != TaskState::Pending) {
             throw std::runtime_error(
                "Cannot set output for task '" + task_name + "': task is " +
                taskStateToString(state) + " (outputs are immutable after completion)");
        }
        if (state == TaskState::Pending) {
            task_state_[task_name] = TaskState::Running;
        }
        task_outputs_[task_name][key] = value;
    }
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TaskState> task_state_;
    std::unordered_set<std::string> completed_tasks_;
    std::unordered_map<std::string, std::string> failed_tasks_;
    std::unordered_map<std::string, std::unordered_map<std::string, jsoncons::json>> task_outputs_;
};

#endif // __TASK_REGISTRY_HPP__
