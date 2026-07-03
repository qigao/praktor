#pragma once

#include <optional>

#include "dag/task_failure_context.hpp"
#include "dag/variable_scope.hpp"

/**
 * @class FailureContextState
 * @brief Owns the active failure context and its mirrored scope variables.
 */
class FailureContextState {
public:
    void set(VariableScope& scope, const TaskFailureContext& failure_context) {
        current_failure_context_ = failure_context;
        applyVariables(scope, failure_context.toFailureVariables());
    }

    void clear(VariableScope& scope) {
        if (!current_failure_context_.has_value()) {
            return;
        }

        removeVariables(scope, current_failure_context_->toFailureVariables());
        current_failure_context_.reset();
    }

    std::optional<TaskFailureContext> get() const {
        return current_failure_context_;
    }

    bool isActive() const {
        return current_failure_context_.has_value();
    }

private:
    std::optional<TaskFailureContext> current_failure_context_;

    static void applyVariables(
        VariableScope& scope,
        const std::unordered_map<std::string, WorkflowValue>& variables) {
        for (const auto& [key, value] : variables) {
            scope.set(key, value);
        }
    }

    static void removeVariables(
        VariableScope& scope,
        const std::unordered_map<std::string, WorkflowValue>& variables) {
        for (const auto& [key, value] : variables) {
            (void)value;
            scope.remove(key);
        }
    }
};
