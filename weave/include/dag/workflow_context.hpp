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
     * Fully delegates to VariableScope.
     */
    void setValue(std::string const& key, const WorkflowValue& value) {
        current_scope_->set(key, value);
    }

    void unsetValue(const std::string& key) {
        current_scope_->remove(key);
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
     * 
     * Now uses VariableScope.
     */
    template <typename ValueType>
    ValueType getValueOrDefault(std::string const& key, ValueType const& defaultValue) const {
        try {
            return current_scope_->get(key).as<ValueType>();
        } catch (...) {
            return defaultValue;
        }
    }

    /**
     * @brief Gets a JSON value from the context using a JMESPath query.
     * @param key The key of the top-level JSON value.
     * @param jmespath_query The JMESPath query string.
     * @return The result of the JMESPath query as a WorkflowValue.
     * @throws std::runtime_error if the key is not found or query fails.
     * 
     * Now uses VariableScope.
     */
    WorkflowValue getJsonValue(std::string const& key, std::string const& jmespath_query) const {
        try {
            auto value = current_scope_->get(key);
            return jsoncons::jmespath::search(value, jmespath_query);
        } catch (const jsoncons::jmespath::jmespath_error& e) {
            throw std::runtime_error("JMESPath query failed for key '" + key + "': " + e.what());
        }
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

        // Special case: "tasks.*" paths should use TaskRegistry
        if (path.rfind("tasks.", 0) == 0) {
            // Parse "tasks.task_name.outputs.key" or "tasks.task_name.status"
            auto first_dot = path.find('.', 6);  // After "tasks."
            if (first_dot == std::string::npos) {
                // Just "tasks" - return all tasks as JSON
                return task_registry_.toJson();
            }
            
            std::string task_name = path.substr(6, first_dot - 6);
            std::string rest = path.substr(first_dot + 1);
            
            if (rest == "status") {
                return WorkflowValue(task_registry_.getStatus(task_name));
            } else if (rest.rfind("outputs.", 0) == 0) {
                std::string output_key = rest.substr(8);
                try {
                    return task_registry_.getOutput(task_name, output_key);
                } catch (...) {
                    return WorkflowValue();
                }
            } else if (rest == "outputs") {
                return task_registry_.getAllOutputs(task_name);
            }
        }

        // Try direct lookup from VariableScope
        try {
            return current_scope_->get(path);
        } catch (...) {
            // Path might be nested - try splitting
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

            // Try getting the root key
            try {
                WorkflowValue root_value = current_scope_->get(segments.front());
                const WorkflowValue* current_value = &root_value;
                
                for (size_t i = 1; i < segments.size(); ++i) {
                    const auto& key = segments[i];
                    if (!current_value->is_object() || !current_value->contains(key)) {
                        return WorkflowValue();
                    }
                    current_value = &current_value->at(key);
                }
                return *current_value;
            } catch (...) {
                return WorkflowValue();
            }
        }
    }



    /**
     * @brief Gets all variables as a string map (only simple string values).
     * 
     * Now uses VariableScope.getAllVisible().
     */
    std::unordered_map<std::string, std::string> getAllVariables() const {
        std::unordered_map<std::string, std::string> result;
        auto all_visible = current_scope_->getAllVisible();
        for (auto const& [key, value] : all_visible) {
            if (value.is_string()) {
                result[key] = value.as<std::string>();
            }
        }
        return result;
    }

    /**
     * @brief Gets all visible variables as WorkflowValue (for collecting outputs).
     * 
     * Returns all variables visible in current scope (including inherited from parent scopes).
     */
    std::unordered_map<std::string, WorkflowValue> getAllVisibleValues() const {
        return current_scope_->getAllVisible();
    }

    /**
     * @brief Sets task status.
     * 
     * Fully delegates to TaskRegistry.
     */
    void setTaskStatus(const std::string& taskName, const std::string& status) {
        task_registry_.setStatus(taskName, status);
    }

    void setTaskOutput(const std::string& taskName, const std::string& key, const WorkflowValue& value) {
        task_registry_.setOutput(taskName, key, value);
    }

    void mergeTaskOutputs(const std::string& taskName, const WorkflowValue& outputs) {
        task_registry_.mergeOutputs(taskName, outputs);
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
    }

    /**
     * @brief Adds a failed task to the context.
     */
    void addFailedTask(std::string const& taskName, std::string const& errorMessage) {
        task_registry_.markFailed(taskName, errorMessage);
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
                current_scope_->remove(key);
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
    // Core components (Phase 4: data_ removed!)
    std::unique_ptr<VariableScope> root_scope_;
    VariableScope* current_scope_;
    TaskRegistry task_registry_;
    
    // Supporting members
    std::vector<std::pair<std::string, std::optional<std::string>>> task_scope_stack_;
    std::unordered_map<std::string, EmbeddedModule> embedded_modules_;
    std::optional<TaskFailureContext> current_failure_context_;
};

#endif   // __WORKFLOW_CONTEXT_HPP__
