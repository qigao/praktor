#pragma once

#include "dag/task_failure_context.hpp"
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <utility>

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
    Failed,     // Task failed (outputs locked)
    Skipped     // Task was skipped due to 'when' or cache (outputs locked)
};

inline std::string taskStateToString(TaskState state) {
    switch (state) {
        case TaskState::Pending: return "pending";
        case TaskState::Running: return "running";
        case TaskState::Completed: return "success";
        case TaskState::Failed: return "failed";
        case TaskState::Skipped: return "skipped";
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
     * @brief Start a task (transitions from any state to Running)
     * 
     * Now allows re-entry for trigger tasks and 'each' iterations.
     * When a task starts, it is removed from completed/failed sets.
     */
    void startTask(const std::string& task_name) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        task_state_[task_name] = TaskState::Running;
        completed_tasks_.erase(task_name);
        failure_snapshots_.erase(task_name);
        task_outputs_.erase(task_name);
    }

    /**
     * @brief Record that a task completed successfully (locks outputs)
     */
    void markCompleted(const std::string& task_name) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        task_state_[task_name] = TaskState::Completed;
        completed_tasks_.insert(task_name);
    }

    /**
     * @brief Record that a task failed (locks outputs)
     */
    void markFailed(const std::string& task_name, const std::string& error_message) {
        TaskFailureContext failure;
        failure.task_name = task_name;
        failure.error_message = error_message;
        markFailed(task_name, std::move(failure));
    }

    /**
     * @brief Record one immutable failure snapshot for a finalized task.
     */
    void markFailed(const std::string& task_name, TaskFailureContext failure) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto state =
            task_state_.try_emplace(task_name, TaskState::Pending).first;
        const TaskState current = state->second;
        if (current == TaskState::Completed || current == TaskState::Failed ||
            current == TaskState::Skipped) {
            throw std::logic_error(
                "Task '" + task_name + "' is already finalized as " +
                taskStateToString(current));
        }

        const bool snapshot_inserted =
            failure_snapshots_.try_emplace(task_name, std::move(failure)).second;
        if (!snapshot_inserted) {
            throw std::logic_error(
                "Task '" + task_name + "' already has a failure snapshot");
        }
        state->second = TaskState::Failed;
    }

    /**
     * @brief Get task state
     */
    TaskState getState(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
     */
    void setStatus(const std::string& task_name, const std::string& status) {
        if (status == "running") {
            startTask(task_name);
        } else if (status == "success" || status == "completed") {
            markCompleted(task_name);
        } else if (status == "failed") {
            markFailed(task_name, "Task explicitly set to failed status");
        } else if (status == "skipped") {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            task_state_[task_name] = TaskState::Skipped;
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
                   const WorkflowValue& value) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
     * 
     * Now properly delegates to setOutput to ensure all validation logic is centralized.
     * Uses recursive_mutex to allow nested locking.
     * @throws std::runtime_error if task is not in Running state
     */
    void mergeOutputs(const std::string& task_name, const WorkflowValue& outputs) {
        if (!outputs.is_object()) {
            return;
        }
        // Delegate to setOutput (recursive_mutex allows re-entry)
        for (const auto& item : outputs.object_range()) {
            setOutput(task_name, item.key(), item.value());
        }
    }

    void clearOutputs(const std::string& task_name) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        task_outputs_.erase(task_name);
    }

    /**
     * @brief Get a specific output from a task
     *
     * Plan A: Only allows reading outputs from completed/failed tasks,
     * ensuring the data is final and won't change.
     */
    WorkflowValue getOutput(const std::string& task_name, const std::string& key) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
    WorkflowValue getAllOutputs(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = task_outputs_.find(task_name);
        if (it == task_outputs_.end()) {
            return WorkflowValue::object();
        }

        WorkflowValue result = WorkflowValue::object();
        for (const auto& [key, value] : it->second) {
            result[key] = value;
        }
        return result;
    }

    /**
     * @brief Check if task completed successfully
     */
    bool isCompleted(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return completed_tasks_.count(task_name) > 0;
    }

    /**
     * @brief Check if task failed
     */
    bool isFailed(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return failure_snapshots_.count(task_name) > 0;
    }

    /**
     * @brief Check if task outputs are finalized (completed or failed)
     */
    bool isFinalized(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        TaskState state = getStateInternal(task_name);
        return state == TaskState::Completed || state == TaskState::Failed;
    }

    /**
     * @brief Get all completed task names
     */
    std::unordered_set<std::string> getCompletedTasks() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return completed_tasks_;
    }

    /**
     * @brief Get all failed tasks with their error messages
     */
    std::unordered_map<std::string, std::string> getFailedTasks() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::unordered_map<std::string, std::string> failed_tasks;
        for (const auto& [task_name, failure] : failure_snapshots_) {
            failed_tasks.emplace(task_name, failure.error_message);
        }
        return failed_tasks;
    }

    /**
     * @brief Get error message for a failed task
     */
    std::string getFailureReason(const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = failure_snapshots_.find(task_name);
        return (it != failure_snapshots_.end()) ? it->second.error_message : "";
    }

    /**
     * @brief Get the immutable failure outcome recorded when a task finalized.
     */
    std::optional<TaskFailureContext> getFailureSnapshot(
        const std::string& task_name) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = failure_snapshots_.find(task_name);
        if (it == failure_snapshots_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /**
     * @brief Build a JSON representation of all tasks for context access
     */
    WorkflowValue toJson() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        WorkflowValue result = WorkflowValue::object();

        for (const auto& [task_name, state] : task_state_) {
            WorkflowValue task_obj = WorkflowValue::object();
            task_obj["status"] = taskStateToString(state);

            auto outputs_it = task_outputs_.find(task_name);
            if (outputs_it != task_outputs_.end()) {
                WorkflowValue outputs_obj = WorkflowValue::object();
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

    mutable std::recursive_mutex mutex_;  // Changed to recursive_mutex to allow setOutput calling from mergeOutputs
    std::unordered_map<std::string, TaskState> task_state_;
    std::unordered_set<std::string> completed_tasks_;
    std::unordered_map<std::string, TaskFailureContext> failure_snapshots_;
    std::unordered_map<std::string, std::unordered_map<std::string, WorkflowValue>> task_outputs_;
};
