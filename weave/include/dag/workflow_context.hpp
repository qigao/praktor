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
#include "dag/variable_scope.hpp"
#include "dag/task_registry.hpp"
#include "util/system_info.hpp"
#include "yml/task_types.hpp"

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
    WorkflowContext(std::unordered_map<std::string, std::string> const& initialVars = {}) 
        : root_scope_(std::make_unique<VariableScope>())
        , current_scope_(root_scope_.get())
    {
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
     * 
     * Now delegates to VariableScope for proper scoping.
     */
    void setValue(std::string const& key, const WorkflowValue& value) {
        current_scope_->set(key, value);
        data_[key] = value;  // Keep for backward compatibility (temporary)
    }

    void unsetValue(const std::string& key) {
        current_scope_->remove(key);
        data_.erase(key);  // Keep for backward compatibility (temporary)
    }

    /**
     * @brief Gets a value from the context.
     * @tparam ValueType The expected type of the value.
     * @param key The key of the value to retrieve.
     * @return The value associated with the key, cast to ValueType.
     * @throws std::runtime_error if the key is not found or type mismatch.
     * 
     * Now delegates to VariableScope for proper scope chain lookup.
     */
    template <typename ValueType>
    ValueType getValue(std::string const& key) const {
        try {
            return current_scope_->get(key).as<ValueType>();
        } catch (const jsoncons::json_exception& e) {
            throw std::runtime_error("Invalid type for key '" + key + "': " + e.what());
        }
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
     * 
     * Now delegates to VariableScope for scope chain lookup.
     */
    bool hasKey(std::string const& key) const {
        return current_scope_->has(key);
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
        return getValueOrDefault<std::string>(key, "\"");
    }

    WorkflowValue getValueByPath(const std::string& path) const {
        if (path.empty()) {
            return WorkflowValue();
        }

        // Fast path: direct key lookup
        auto direct_it = data_.find(path);
        if (direct_it != data_.end()) {
            return direct_it->second;
        }

        // Split path by '.' to walk nested objects (supports simple array indices later if needed)
        std::vector<std::string> segments;
        std::string current;
        for (char ch : path) {
            if (ch == '.') {
                if (!current.empty()) {
                    segments.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty()) {
            segments.push_back(current);
        }

        if (segments.empty()) {
            return WorkflowValue();
        }

        auto it = data_.find(segments.front());
        if (it == data_.end()) {
            auto str = getValueOrDefault<std::string>(path, "");
            return str.empty() ? WorkflowValue() : WorkflowValue(str);
        }

        const WorkflowValue* current_value = &it->second;
        for (size_t i = 1; i < segments.size(); ++i) {
            const auto& key = segments[i];
            if (!current_value->is_object() || !current_value->contains(key)) {
                return WorkflowValue();
            }
            current_value = &current_value->at(key);
        }

        return *current_value;
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
     * @brief Sets task status.
     * 
     * Now delegates to TaskRegistry.
     */
    void setTaskStatus(const std::string& taskName, const std::string& status) {
        task_registry_.setStatus(taskName, status);
        
        // Keep data_ in sync for backward compatibility (temporary)
        WorkflowValue& tasks = data_["tasks"];
        if (!tasks.is_object()) {
            tasks = jsoncons::json::object();
        }
        WorkflowValue& task_entry = tasks[taskName];
        if (!task_entry.is_object()) {
            task_entry = jsoncons::json::object();
        }
        task_entry["status"] = status;
    }

    void setTaskOutput(const std::string& taskName, const std::string& key, const WorkflowValue& value) {
        task_registry_.setOutput(taskName, key, value);
        
        // Keep data_ in sync for backward compatibility (temporary)
        WorkflowValue& tasks = data_["tasks"];
        if (!tasks.is_object()) {
            tasks = jsoncons::json::object();
        }
        WorkflowValue& task_entry = tasks[taskName];
        if (!task_entry.is_object()) {
            task_entry = jsoncons::json::object();
        }
        WorkflowValue& outputs = task_entry["outputs"];
        if (!outputs.is_object()) {
            outputs = jsoncons::json::object();
        }
        outputs.insert_or_assign(key, value);
    }

    void mergeTaskOutputs(const std::string& taskName, const WorkflowValue& outputs) {
        task_registry_.mergeOutputs(taskName, outputs);
        
        // Keep data_ in sync for backward compatibility (temporary)
        if (!outputs.is_object()) {
            return;
        }
        for (const auto& item : outputs.object_range()) {
            WorkflowValue& tasks = data_["tasks"];
            if (!tasks.is_object()) {
                tasks = jsoncons::json::object();
            }
            WorkflowValue& task_entry = tasks[taskName];
            if (!task_entry.is_object()) {
                task_entry = jsoncons::json::object();
            }
            WorkflowValue& outputs_obj = task_entry["outputs"];
            if (!outputs_obj.is_object()) {
                outputs_obj = jsoncons::json::object();
            }
            outputs_obj.insert_or_assign(item.key(), item.value());
        }
    }
    void pushTaskScope(const std::string& taskName, std::optional<std::string> alias = std::nullopt) {
        task_scope_stack_.emplace_back(taskName, alias);
    }

    void popTaskScope() {
        if (!task_scope_stack_.empty()) {
            task_scope_stack_.pop_back();
        }
    }

    void setCurrentTaskOutput(const std::string& key, const WorkflowValue& value) {
        if (task_scope_stack_.empty()) {
            setTaskOutput("__root__", key, value);
            return;
        }

        const auto& scope = task_scope_stack_.back();
        setTaskOutput(scope.first, key, value);
        setValue("tasks." + scope.first + ".outputs." + key, value);
        if (scope.second && scope.second.value() != scope.first) {
            setTaskOutput(scope.second.value(), key, value);
            setValue("tasks." + scope.second.value() + ".outputs." + key, value);
        }
    }


    void addCompletedTask(std::string const& taskName) {
        task_registry_.markCompleted(taskName);
        
        // Keep data_ in sync for backward compatibility (temporary)
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
        task_registry_.markFailed(taskName, errorMessage);
        
        // Keep data_ in sync for backward compatibility (temporary)
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
     * 
     * Now delegates to TaskRegistry.
     */
    std::unordered_set<std::string> getCompletedTasks() const {
        return task_registry_.getCompletedTasks();
    }

    /**
     * @brief Gets the map of failed tasks and their error messages.
     * 
     * Now delegates to TaskRegistry.
     */
    std::unordered_map<std::string, std::string> getFailedTasks() const {
        return task_registry_.getFailedTasks();
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

    void setEmbeddedModules(const std::unordered_map<std::string, EmbeddedModule>& modules) {
        embedded_modules_ = modules;
    }

    bool hasEmbeddedModule(const std::string& name) const {
        return embedded_modules_.find(name) != embedded_modules_.end();
    }

    const EmbeddedModule* getEmbeddedModule(const std::string& name) const {
        auto it = embedded_modules_.find(name);
        if (it == embedded_modules_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const std::unordered_map<std::string, EmbeddedModule>& getEmbeddedModules() const {
        return embedded_modules_;
    }

private:
    // New components (Phase 2 refactoring)
    std::unique_ptr<VariableScope> root_scope_;
    VariableScope* current_scope_;
    TaskRegistry task_registry_;
    
    // Original members
    std::vector<std::pair<std::string, std::optional<std::string>>> task_scope_stack_;
    std::unordered_map<std::string, EmbeddedModule> embedded_modules_;
    std::unordered_map<std::string, WorkflowValue> data_;  // TODO: Remove after full migration
    std::optional<TaskFailureContext> current_failure_context_;
};

#endif   // __WORKFLOW_CONTEXT_HPP__
