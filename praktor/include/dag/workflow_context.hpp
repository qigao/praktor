#ifndef __WORKFLOW_CONTEXT_HPP__
#define __WORKFLOW_CONTEXT_HPP__

// Standard library includes
#include <optional>
#include <sstream>
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
#include "util/logging.hpp"
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
        : scope_(std::make_unique<VariableScope>())
        , task_registry_(std::make_shared<TaskRegistry>())
    {
        // First populate with built-in system variables
        auto systemVars = Praktor::system::getAllSystemProperties();
        for (auto const& [key, value] : systemVars) {
            setValue(key, value);
        }

        // Then populate with user-provided initial variables (can override system vars)
        for (auto const& [key, value] : initialVars) {
            setValue(key, value);
        }
    }

    /**
     * @brief Creates a thread-local child context for parallel execution
     *
     * The child context:
     * - Shares the TaskRegistry (so outputs are visible to all)
     * - Inherits variables via a child VariableScope (writes are local)
     * - Copies modules and configuration
     */
    std::unique_ptr<WorkflowContext> fork() const {
        auto child = std::make_unique<WorkflowContext>();
        // Reset scope to be a child of this scope
        child->scope_ = std::make_unique<VariableScope>(this->scope_.get());
        child->task_registry_ = this->task_registry_;
        child->embedded_modules_ = this->embedded_modules_;
        child->task_scope_stack_ = this->task_scope_stack_;
        // Failure context is not inherited
        return child;
    }

    /**
     * @brief Sets a jsoncons::json value in the context.
     *
     * Fully delegates to VariableScope.
     */
    void setValue(std::string const& key, const WorkflowValue& value) {
         scope_->set(key, value);
     }

    void unsetValue(const std::string& key) {
        scope_->remove(key);
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
        WorkflowValue val = getValueByPath(key);
        if (val.is_null()) {
             throw std::runtime_error("Variable not found: " + key);
        }
        try {
            return val.as<ValueType>();
        } catch (const std::exception& e) {
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
     * Now uses getValue to support virtual variables.
     */
    template <typename ValueType>
    ValueType getValueOrDefault(std::string const& key, ValueType const& defaultValue) const {
        try {
            return getValue<ValueType>(key);
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
            auto value = scope_->get(key);
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
        if (key.rfind("tasks.", 0) == 0) {
            auto first_dot = key.find('.', 6);
            if (first_dot == std::string::npos) return true; // "tasks"

            std::string task_name = key.substr(6, first_dot - 6);
            std::string rest = key.substr(first_dot + 1);

            if (rest == "status") return true;
            if (rest == "outputs") return true;
            if (rest.rfind("outputs.", 0) == 0) {
                std::string output_key = rest.substr(8);
                try {
                    task_registry_->getOutput(task_name, output_key);
                    return true;
                } catch (...) {
                    return false;
                }
            }
        }
        return scope_->has(key);
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

    WorkflowValue getValueByPath(const std::string& path) const {
        if (path.empty()) return WorkflowValue::null();
        if (!scope_) {
            logw("getValueByPath: scope_ is null");
            return WorkflowValue::null();
        }
        size_t first = path.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return WorkflowValue::null();
    
    std::string trimmed_path = path.substr(first);
    auto last = trimmed_path.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) trimmed_path.erase(last + 1);

        if (trimmed_path.rfind("tasks.", 0) == 0) {
            auto first_dot = trimmed_path.find('.', 6);
            if (first_dot == std::string::npos) return task_registry_->toJson();
            std::string task_name = trimmed_path.substr(6, first_dot - 6);
            std::string rest = trimmed_path.substr(first_dot + 1);
            if (rest == "status") return WorkflowValue(task_registry_->getStatus(task_name));
            if (rest.rfind("outputs.", 0) == 0) {
                try { 
                    WorkflowValue val = task_registry_->getOutput(task_name, rest.substr(8)); 
                    logd("getValueByPath: 'tasks.{}.outputs.{}' found, value='{}'", task_name, rest.substr(8), val.to_string());
                    return val;
                } catch(...) {}
            } else if (rest == "outputs") {
                WorkflowValue outputs = task_registry_->getAllOutputs(task_name);
                logd("getValueByPath: 'tasks.{}.outputs' found, value='{}'", task_name, outputs.to_string());
                return outputs;
            }
        }

        // Check if the exact path exists as a key
        if (scope_->has(trimmed_path)) {
            try { 
                auto val = scope_->get(trimmed_path);
                logd("getValueByPath: basic key '{}' found, value='{}'", trimmed_path, val.to_string());
                return val;
            } catch (...) {
                logd("getValueByPath: basic key '{}' found but failed to retrieve", trimmed_path);
            }
        }

        logd("getValueByPath: key '{}' not found in scope, trying dotted traversal", trimmed_path);
        // Handle dotted paths by traversing nested objects
        if (trimmed_path.find('.') != std::string::npos) {
            std::vector<std::string> segments;
            std::string segment;
            std::stringstream ss(trimmed_path);
            while (std::getline(ss, segment, '.')) {
                if (!segment.empty()) segments.push_back(segment);
            }

            if (segments.size() >= 2 && scope_->has(segments[0])) {
                try {
                    WorkflowValue current = scope_->get(segments[0]);
                    logd("getValueByPath: starting dotted traversal from base key '{}', value='{}'", segments[0], current.to_string());
                    for (size_t i = 1; i < segments.size(); ++i) {
                        if (!current.is_object() || !current.contains(segments[i])) {
                            logd("getValueByPath: dotted traversal failed at segment '{}', current is not object or does not contain key", segments[i]);
                            return WorkflowValue::null();
                        }
                        WorkflowValue next = current.at(segments[i]);
                        logd("getValueByPath: traversed to segment '{}', current value='{}'", segments[i], next.to_string());
                        current = std::move(next);
                    }
                    logd("getValueByPath: dotted traversal successful for '{}', final value='{}'", trimmed_path, current.to_string());
                    return current;
                } catch (const std::exception& e) {
                    logw("getValueByPath: exception during traversal for '{}': {}", trimmed_path, e.what());
                }
            } else {
                logd("getValueByPath: dotted path '{}' has less than 2 segments or base key '{}' not found in scope", trimmed_path, segments[0]);
            }
        }
        logd("getValueByPath: key '{}' not found after all attempts, returning null", trimmed_path);
        return WorkflowValue::null();
    }



    /**
     * @brief Gets all variables as a string map (only simple string values).
     *
     * Now uses VariableScope.getAllVisible().
     */
    std::unordered_map<std::string, std::string> getAllVariables() const {
        logd("getAllVariables: retrieving all visible string variables");
        std::unordered_map<std::string, std::string> result;
        auto all_visible = scope_->getAllVisible();
        for (auto const& [key, value] : all_visible) {
            if (value.is_string()) {
                result[key] = value.as<std::string>();
            }
        }
        logd("getAllVariables: retrieved {} string variables", result.size());
        return result;
    }

    /**
     * @brief Gets all visible variables as WorkflowValue (for collecting outputs).
     *
     * Returns all variables visible in current scope (including inherited from parent scopes).
     */
    std::unordered_map<std::string, WorkflowValue> getAllVisibleValues() const {
        logd("getAllVisibleValues: retrieving all visible WorkflowValues");
        return scope_->getAllVisible();
    }

    /**
     * @brief Sets task status.
     *
     * Fully delegates to TaskRegistry.
     */
    void setTaskStatus(const std::string& taskName, const std::string& status) {
        logd("setTaskStatus: task='{}', status='{}'", taskName, status);
        task_registry_->setStatus(taskName, status);
    }

    std::string getTaskStatus(const std::string& taskName) const {
        std::string status = task_registry_->getStatus(taskName);
        logd("getTaskStatus: task='{}', status='{}'", taskName, status);
        return status;
    }

    void setTaskOutput(const std::string& taskName, const std::string& key, const WorkflowValue& value) {
        logd("setTaskOutput: task='{}', key='{}', value='{}'", taskName, key, value.to_string());
        task_registry_->setOutput(taskName, key, value);
    }

    void mergeTaskOutputs(const std::string& taskName, const WorkflowValue& outputs) {
        logd("mergeTaskOutputs: task='{}', outputs='{}'", taskName, outputs.to_string());
        task_registry_->mergeOutputs(taskName, outputs);
    }
    void pushTaskScope(const std::string& taskName, std::optional<std::string> alias = std::nullopt) {
        std::string alias_str = alias.has_value() ? alias.value() : "nullopt";
        logd("pushTaskScope: taskName='{}', alias='{}', stack_size={}", taskName, alias_str, task_scope_stack_.size() + 1);
        task_scope_stack_.emplace_back(taskName, alias);
    }

    void popTaskScope() {
        if (!task_scope_stack_.empty()) {
            const auto& scope = task_scope_stack_.back();
            std::string alias_str = scope.second.has_value() ? scope.second.value() : "nullopt";
            logd("popTaskScope: popping task='{}', alias='{}', stack_size={}", scope.first, alias_str, task_scope_stack_.size() - 1);
            task_scope_stack_.pop_back();
        } else {
            logw("popTaskScope: attempt to pop from empty task_scope_stack_");
        }
    }

    void setCurrentTaskOutput(const std::string& key, const WorkflowValue& value) {
        logd("setCurrentTaskOutput: key='{}', value='{}', stack_size={}", key, value.to_string(), task_scope_stack_.size());
        if (task_scope_stack_.empty()) {
            logd("setCurrentTaskOutput: empty stack, using __root__ task");
            setTaskOutput("__root__", key, value);
            setValue("tasks.__root__.outputs." + key, value);
            return;
        }

        const auto& scope = task_scope_stack_.back();
        std::string scope_second_str = scope.second.value_or("nullopt");
        logd("setCurrentTaskOutput: current task scope: task='{}', alias='{}'", scope.first, scope_second_str);

        // Set output for the actual task name
        setTaskOutput(scope.first, key, value);
        setValue("tasks." + scope.first + ".outputs." + key, value);

        // If there's an alias (e.g., for nested workflows), also set output for alias
        if (scope.second && scope.second.value() != scope.first) {
            logi("setCurrentTaskOutput: also setting alias={} for task={}", scope.second.value(), scope.first);
            setTaskOutput(scope.second.value(), key, value);
            setValue("tasks." + scope.second.value() + ".outputs." + key, value);
        }
    }


    void addCompletedTask(std::string const& taskName) {
        if (task_registry_->getState(taskName) == TaskState::Pending) {
            task_registry_->startTask(taskName);
        }
        task_registry_->markCompleted(taskName);
    }

    void addFailedTask(std::string const& taskName, std::string const& errorMessage) {
        if (task_registry_->getState(taskName) == TaskState::Pending) {
            task_registry_->startTask(taskName);
        }
        task_registry_->markFailed(taskName, errorMessage);
    }

    /**
     * @brief Gets the set of completed tasks.
     *
     * Now delegates to TaskRegistry.
     */
    std::unordered_set<std::string> getCompletedTasks() const {
        return task_registry_->getCompletedTasks();
    }

    /**
     * @brief Gets the map of failed tasks and their error messages.
     *
     * Now delegates to TaskRegistry.
     */
    std::unordered_map<std::string, std::string> getFailedTasks() const {
        return task_registry_->getFailedTasks();
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
                scope_->remove(key);
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
    // Core components
    std::unique_ptr<VariableScope> scope_;
    std::shared_ptr<TaskRegistry> task_registry_;

    // Supporting members
    std::vector<std::pair<std::string, std::optional<std::string>>> task_scope_stack_;
    std::unordered_map<std::string, EmbeddedModule> embedded_modules_;
    std::optional<TaskFailureContext> current_failure_context_;
};

#endif   // __WORKFLOW_CONTEXT_HPP__
