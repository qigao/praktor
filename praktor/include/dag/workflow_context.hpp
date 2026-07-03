#pragma once

// Standard library includes
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <type_traits>

// jsoncons includes
#include <jsoncons/json.hpp>

// Project includes
#include "dag/failure_context_state.hpp"
#include "dag/task_failure_context.hpp"
#include "dag/task_registry.hpp"
#include "dag/variable_scope.hpp"
#include "dag/workflow_module_store.hpp"
#include "util/system_info.hpp"
#include "util/logging.hpp"
#include "util/workflow_path_resolver.hpp"
#include "yml/task_types.hpp"

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
        child->module_store_ = this->module_store_;
        child->task_scope_stack_ = this->task_scope_stack_;
        child->source_path_ = this->source_path_;
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
        if (!hasKey(key)) {
            return defaultValue;
        }
        return getValue<ValueType>(key);
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
        return Praktor::util::WorkflowPathResolver::getJsonValue(*scope_, key, jmespath_query);
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
                } catch (const std::exception&) {
                    return scope_->has("tasks." + task_name + ".outputs." + output_key);
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

    std::string getVariable(std::string const& key) const {
        return getValueOrDefault<std::string>(key, "");
    }

    WorkflowValue getValueByPath(const std::string& path) const {
        return Praktor::util::WorkflowPathResolver::getValueByPath(
            scope_.get(), *task_registry_, path);
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
     * @brief Merge only the child's local scope writes into this context.
     *
     * The child inherits visible values through its parent chain, so copying only locals
     * preserves writes without re-materializing inherited state.
     */
    void mergeLocalValuesFrom(const WorkflowContext& child) {
        if (!child.scope_) {
            return;
        }

        for (const auto& [key, value] : child.scope_->getLocalSnapshot()) {
            setValue(key, value);
        }
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
        logd("setTaskOutput: task='{}', key='{}', type={}", taskName, key, static_cast<int>(value.type()));
        task_registry_->setOutput(taskName, key, value);
    }

    void mergeTaskOutputs(const std::string& taskName, const WorkflowValue& outputs) {
        logd("mergeTaskOutputs: task='{}', outputs_type={}", taskName, static_cast<int>(outputs.type()));
        task_registry_->mergeOutputs(taskName, outputs);
    }

    void clearTaskOutputs(const std::string& taskName) {
        logd("clearTaskOutputs: task='{}'", taskName);
        task_registry_->clearOutputs(taskName);
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
        logd("setCurrentTaskOutput: key='{}', type={}, stack_size={}",
             key, static_cast<int>(value.type()), task_scope_stack_.size());
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
            logd("setCurrentTaskOutput: also setting alias={} for task={}", scope.second.value(), scope.first);
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
        failure_state_.set(*scope_, failure_context);
    }

    /**
     * @brief Clears failure context variables after on_failure task execution
     */
    void clearFailureContext() {
        failure_state_.clear(*scope_);
    }

    /**
     * @brief Gets the current failure context if available
     */
    std::optional<TaskFailureContext> getFailureContext() const {
        return failure_state_.get();
    }

    /**
     * @brief Checks if we're currently executing within a failure context
     */
    bool isInFailureContext() const {
        return failure_state_.isActive();
    }

    void setEmbeddedModules(const std::unordered_map<std::string, EmbeddedModule>& modules) {
        module_store_.setEmbeddedModules(modules);
    }

    bool hasEmbeddedModule(const std::string& name) const {
        return module_store_.hasEmbeddedModule(name);
    }

    const EmbeddedModule* getEmbeddedModule(const std::string& name) const {
        return module_store_.getEmbeddedModule(name);
    }

    const std::unordered_map<std::string, EmbeddedModule>& getEmbeddedModules() const {
        return module_store_.getEmbeddedModules();
    }

    void setNativeModules(const NativeModules& modules) {
        module_store_.setNativeModules(modules);
    }

    const NativeModules& getNativeModules() const {
        return module_store_.getNativeModules();
    }

    void setSourcePath(const std::string& path) {
        source_path_ = path;
    }

    const std::string& getSourcePath() const {
        return source_path_;
    }

private:
    // Core components
    std::unique_ptr<VariableScope> scope_;
    std::shared_ptr<TaskRegistry> task_registry_;

    // Supporting members
    std::vector<std::pair<std::string, std::optional<std::string>>> task_scope_stack_;
    WorkflowModuleStore module_store_;
    std::string source_path_;
    FailureContextState failure_state_;
};
