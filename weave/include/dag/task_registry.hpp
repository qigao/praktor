#ifndef __TASK_REGISTRY_HPP__
#define __TASK_REGISTRY_HPP__

#include <jsoncons/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

/**
 * @class TaskRegistry
 * @brief Manages task execution state and outputs
 *
 * Linus principle: "Single responsibility - do one thing well"
 * 
 * This class is responsible ONLY for tracking task state:
 * - Which tasks completed successfully
 * - Which tasks failed
 * - What outputs each task produced
 * - What the current status of each task is
 *
 * Before: Task state mixed with variables in a giant dictionary
 * After: Dedicated registry with type-safe operations
 */
class TaskRegistry {
public:
    TaskRegistry() = default;

    /**
     * @brief Record that a task completed successfully
     */
    void markCompleted(const std::string& task_name) {
        completed_tasks_.insert(task_name);
        setStatus(task_name, "success");
    }

    /**
     * @brief Record that a task failed
     */
    void markFailed(const std::string& task_name, const std::string& error_message) {
        failed_tasks_[task_name] = error_message;
        setStatus(task_name, "failed");
    }

    /**
     * @brief Set task status
     */
    void setStatus(const std::string& task_name, const std::string& status) {
        task_status_[task_name] = status;
    }

    /**
     * @brief Get task status
     */
    std::string getStatus(const std::string& task_name) const {
        auto it = task_status_.find(task_name);
        return (it != task_status_.end()) ? it->second : "unknown";
    }

    /**
     * @brief Set a single output for a task
     */
    void setOutput(const std::string& task_name, const std::string& key, const jsoncons::json& value) {
        task_outputs_[task_name][key] = value;
    }

    /**
     * @brief Merge multiple outputs for a task
     */
    void mergeOutputs(const std::string& task_name, const jsoncons::json& outputs) {
        if (!outputs.is_object()) {
            return;
        }
        for (const auto& item : outputs.object_range()) {
            task_outputs_[task_name][item.key()] = item.value();
        }
    }

    /**
     * @brief Get a specific output from a task
     */
    jsoncons::json getOutput(const std::string& task_name, const std::string& key) const {
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
        return completed_tasks_.count(task_name) > 0;
    }

    /**
     * @brief Check if task failed
     */
    bool isFailed(const std::string& task_name) const {
        return failed_tasks_.count(task_name) > 0;
    }

    /**
     * @brief Get all completed task names
     */
    const std::unordered_set<std::string>& getCompletedTasks() const {
        return completed_tasks_;
    }

    /**
     * @brief Get all failed tasks with their error messages
     */
    const std::unordered_map<std::string, std::string>& getFailedTasks() const {
        return failed_tasks_;
    }

    /**
     * @brief Get error message for a failed task
     */
    std::string getFailureReason(const std::string& task_name) const {
        auto it = failed_tasks_.find(task_name);
        return (it != failed_tasks_.end()) ? it->second : "";
    }

    /**
     * @brief Build a JSON representation of all tasks for context access
     * 
     * Returns structure like:
     * {
     *   "task_name": {
     *     "status": "success",
     *     "outputs": { ... }
     *   }
     * }
     */
    jsoncons::json toJson() const {
        jsoncons::json result = jsoncons::json::object();
        
        for (const auto& [task_name, status] : task_status_) {
            jsoncons::json task_obj = jsoncons::json::object();
            task_obj["status"] = status;
            
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
    std::unordered_set<std::string> completed_tasks_;
    std::unordered_map<std::string, std::string> failed_tasks_;
    std::unordered_map<std::string, std::string> task_status_;
    std::unordered_map<std::string, std::unordered_map<std::string, jsoncons::json>> task_outputs_;
};

#endif // __TASK_REGISTRY_HPP__
