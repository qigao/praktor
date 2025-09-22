#ifndef __WORKFLOW_CONTEXT_HPP__
#define __WORKFLOW_CONTEXT_HPP__

// Standard library includes
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <type_traits>

// jsoncons includes
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

// Project includes
#include "util/system_info.hpp"

// Define WorkflowValue as jsoncons::json
using WorkflowValue = jsoncons::json;

/**
 * @struct TaskFailureContext
 * @brief Captures detailed information about a failed task for on_failure handlers
 *
 * Linus approach: "Provide the information error handling actually needs"
 */
struct TaskFailureContext {
    std::string task_name;           // Name of the failed task
    std::string task_type;           // Type of the failed task
    int64_t exit_code = -1;          // Exit code (for run_command tasks)
    std::string stdout_data;         // Standard output captured
    std::string stderr_data;         // Standard error captured
    std::string error_message;       // Error message from TaskResult

    // Variables that were captured by the failed task before failure
    std::unordered_map<std::string, WorkflowValue> captured_outputs;

    // Convert to variables that can be accessed in on_failure tasks
    std::unordered_map<std::string, WorkflowValue> toFailureVariables() const {
        std::unordered_map<std::string, WorkflowValue> failure_vars;

        failure_vars["failed_task_name"] = task_name;
        failure_vars["failed_task_type"] = task_type;
        failure_vars["failed_task_exit_code"] = std::to_string(exit_code);
        failure_vars["failed_task_stdout"] = stdout_data;
        failure_vars["failed_task_stderr"] = stderr_data;
        failure_vars["failed_task_error"] = error_message;

        // Add captured outputs with prefix for easy access
        for (const auto& [key, value] : captured_outputs) {
            failure_vars["failed_task_outputs." + key] = value;
        }

        return failure_vars;
    }
};

/**
 * @class WorkflowContext
 * @brief A context for storing and sharing data between tasks in a workflow.
 *
 * This class provides a key-value store for sharing data between tasks.
 * It stores values as jsoncons::json, allowing for structured data.
 */
class WorkflowContext {
public:
    /**
     * @brief Constructs a WorkflowContext with initial variables.
     * @param initialVars A map of initial string variables to populate the context with.
     */
    WorkflowContext(std::unordered_map<std::string, std::string> const& initialVars = {}) {
        // First populate with built-in system variables
        auto systemVars = weave::system::getAllSystemProperties();
        for (auto const& [key, value] : systemVars) {
            setValue(key, value);
        }

        // Then populate with user-provided initial variables (can override system vars)
        for (auto const& [key, value] : initialVars) {
            setValue(key, value);
        }
    }

    /**
     * @brief Sets a jsoncons::json value in the context.
     */
    void setValue(std::string const& key, const WorkflowValue& value) {
        data_[key] = value;
    }

    /**
     * @brief Gets a value from the context.
     * @tparam ValueType The expected type of the value.
     * @param key The key of the value to retrieve.
     * @return The value associated with the key, cast to ValueType.
     * @throws std::runtime_error if the key is not found or type mismatch.
     */
    template <typename ValueType>
    ValueType getValue(std::string const& key) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return it->second.as<ValueType>();
            } catch (const jsoncons::json_exception& e) {
                throw std::runtime_error("Invalid type for key '" + key + "': " + e.what());
            }
        }
        throw std::runtime_error("Key not found in context: " + key);
    }

    /**
     * @brief Gets a value from the context, with a default value if the key is not found or type mismatch.
     * @tparam ValueType The expected type of the value.
     * @param key The key of the value to retrieve.
     * @param defaultValue The default value to return.
     * @return The value associated with the key, or the default value.
     */
    template <typename ValueType>
    ValueType getValueOrDefault(std::string const& key, ValueType const& defaultValue) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return it->second.as<ValueType>();
            } catch (const jsoncons::json_exception&) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    /**
     * @brief Gets a JSON value from the context using a JMESPath query.
     * @param key The key of the top-level JSON value.
     * @param jmespath_query The JMESPath query string.
     * @return The result of the JMESPath query as a WorkflowValue.
     * @throws std::runtime_error if the key is not found or query fails.
     */
    WorkflowValue getJsonValue(std::string const& key, std::string const& jmespath_query) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return jsoncons::jmespath::search(it->second, jmespath_query);
            } catch (const jsoncons::jmespath::jmespath_error& e) {
                throw std::runtime_error("JMESPath query failed for key '" + key + "': " + e.what());
            }
        }
        throw std::runtime_error("Key not found for JMESPath query: " + key);
    }

    /**
     * @brief Checks if a key exists in the context.
     */
    bool hasKey(std::string const& key) const {
        return data_.count(key) > 0;
    }

    /**
     * @brief Sets a variable in the context (alias for setValue).
     */
    void setVariable(std::string const& key, std::string const& value) {
        setValue(key, value);
    }

    /**
     * @brief Gets a variable from the context (alias for getValueOrDefault with string).
     */
    std::string getVariable(std::string const& key) const {
        return getValueOrDefault<std::string>(key, "");
    }

    /**
     * @brief Gets all variables as a string map (only simple string values).
     */
    std::unordered_map<std::string, std::string> getAllVariables() const {
        std::unordered_map<std::string, std::string> result;
        for (auto const& [key, value] : data_) {
            if (value.is_string()) {
                result[key] = value.as<std::string>();
            }
        }
        return result;
    }

    /**
     * @brief Adds a completed task to the context.
     */
    void addCompletedTask(std::string const& taskName) {
        WorkflowValue& completed_tasks_json = data_["completed_tasks_set"];
        if (!completed_tasks_json.is_array()) {
            completed_tasks_json = jsoncons::json::array();
        }
        completed_tasks_json.push_back(taskName);
    }

    /**
     * @brief Adds a failed task to the context.
     */
    void addFailedTask(std::string const& taskName, std::string const& errorMessage) {
        WorkflowValue& failed_tasks_json = data_["failed_tasks_set"];
        if (!failed_tasks_json.is_array()) {
            failed_tasks_json = jsoncons::json::array();
        }
        failed_tasks_json.push_back(taskName);

        WorkflowValue& failed_tasks_map_json = data_["failed_tasks_map"];
        if (!failed_tasks_map_json.is_object()) {
            failed_tasks_map_json = jsoncons::json::object();
        }
        failed_tasks_map_json.insert_or_assign(taskName, errorMessage);
    }

    /**
     * @brief Gets the set of completed tasks.
     */
    std::unordered_set<std::string> getCompletedTasks() const {
        std::unordered_set<std::string> result;
        auto it = data_.find("completed_tasks_set");
        if (it != data_.end() && it->second.is_array()) {
            for (const auto& item : it->second.array_range()) {
                if (item.is_string()) {
                    result.insert(item.as<std::string>());
                }
            }
        }
        return result;
    }

    /**
     * @brief Gets the map of failed tasks and their error messages.
     */
    std::unordered_map<std::string, std::string> getFailedTasks() const {
        std::unordered_map<std::string, std::string> result;
        auto it = data_.find("failed_tasks_map");
        if (it != data_.end() && it->second.is_object()) {
            for (const auto& member : it->second.object_range()) {
                if (member.value().is_string()) {
                    result[member.key()] = member.value().as<std::string>();
                }
            }
        }
        return result;
    }

    /**
     * @brief Sets failure context variables for on_failure task execution
     * @param failure_context The failure context containing task failure details
     */
    void setFailureContext(const TaskFailureContext& failure_context) {
        // Store the complete failure context
        current_failure_context_ = failure_context;

        // Set failure variables that can be accessed in on_failure tasks
        auto failure_vars = failure_context.toFailureVariables();
        for (const auto& [key, value] : failure_vars) {
            setValue(key, value);
        }
    }

    /**
     * @brief Clears failure context variables after on_failure task execution
     */
    void clearFailureContext() {
        if (current_failure_context_.has_value()) {
            auto failure_vars = current_failure_context_->toFailureVariables();
            for (const auto& [key, value] : failure_vars) {
                data_.erase(key);
            }
            current_failure_context_.reset();
        }
    }

    /**
     * @brief Gets the current failure context if available
     */
    std::optional<TaskFailureContext> getFailureContext() const {
        return current_failure_context_;
    }

    /**
     * @brief Checks if we're currently executing within a failure context
     */
    bool isInFailureContext() const {
        return current_failure_context_.has_value();
    }

private:
    std::unordered_map<std::string, WorkflowValue> data_;
    std::optional<TaskFailureContext> current_failure_context_;
};

#endif   // __WORKFLOW_CONTEXT_HPP__
