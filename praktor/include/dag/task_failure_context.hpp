#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "data/workflow_value.hpp"


/**
 * @struct TaskFailureContext
 * @brief Captures detailed information about a failed task for on_failure handlers.
 */
struct TaskFailureContext {
    std::string task_name;
    std::string task_type;
    int64_t exit_code = -1;
    std::string stdout_data;
    std::string stderr_data;
    std::string error_message;
    std::unordered_map<std::string, WorkflowValue> captured_outputs;
    std::shared_ptr<TaskFailureContext> inner_failure;
    std::string error_code;
    std::string error_phase;
    WorkflowValue error_details{WorkflowValue::object()};

    bool hasInnerFailure() const {
        return static_cast<bool>(inner_failure);
    }

    std::unordered_map<std::string, WorkflowValue> toFailureVariables() const {
        std::unordered_map<std::string, WorkflowValue> failure_vars;
        WorkflowValue failed_outputs = buildOutputsJson(captured_outputs);

        failure_vars["failed_task_name"] = task_name;
        failure_vars["failed_task_type"] = task_type;
        failure_vars["failed_task_exit_code"] = std::to_string(exit_code);
        failure_vars["failed_task_stdout"] = stdout_data;
        failure_vars["failed_task_stderr"] = stderr_data;
        failure_vars["failed_task_error"] = error_message;
        failure_vars["failed_task_error_code"] = error_code;
        failure_vars["failed_task_error_phase"] = error_phase;
        failure_vars["failed_task_error_details"] = error_details;

        for (const auto& [key, value] : captured_outputs) {
            failure_vars["failed_task_outputs." + key] = value;
        }

        failure_vars["failed_task_outputs"] = failed_outputs;
        failure_vars["failed_task"] = toJson();

        if (hasInnerFailure()) {
            const TaskFailureContext& inner = *inner_failure;
            WorkflowValue failed_inner_outputs = buildOutputsJson(inner.captured_outputs);

            failure_vars["failed_inner_task_name"] = inner.task_name;
            failure_vars["failed_inner_task_type"] = inner.task_type;
            failure_vars["failed_inner_task_exit_code"] = std::to_string(inner.exit_code);
            failure_vars["failed_inner_task_stdout"] = inner.stdout_data;
            failure_vars["failed_inner_task_stderr"] = inner.stderr_data;
            failure_vars["failed_inner_task_error"] = inner.error_message;
            failure_vars["failed_inner_task_error_code"] = inner.error_code;
            failure_vars["failed_inner_task_error_phase"] = inner.error_phase;
            failure_vars["failed_inner_task_error_details"] = inner.error_details;

            for (const auto& [key, value] : inner.captured_outputs) {
                failure_vars["failed_inner_task_outputs." + key] = value;
            }

            failure_vars["failed_inner_task_outputs"] = failed_inner_outputs;
            failure_vars["failed_inner_task"] = inner.toJson();
        }

        return failure_vars;
    }

    WorkflowValue toJson() const {
        WorkflowValue failed_task = WorkflowValue::object();
        failed_task["name"] = task_name;
        failed_task["type"] = task_type;
        failed_task["exit_code"] = exit_code;
        failed_task["stdout"] = stdout_data;
        failed_task["stderr"] = stderr_data;
        failed_task["error"] = error_message;
        failed_task["error_code"] = error_code;
        failed_task["error_phase"] = error_phase;
        failed_task["error_details"] = error_details;
        failed_task["outputs"] = buildOutputsJson(captured_outputs);

        if (inner_failure) {
            failed_task["inner"] = inner_failure->toJson();
        }

        return failed_task;
    }

private:
    static WorkflowValue buildOutputsJson(
        const std::unordered_map<std::string, WorkflowValue>& outputs) {
        WorkflowValue json = WorkflowValue::object();
        for (const auto& [key, value] : outputs) {
            json[key] = value;
        }
        return json;
    }
};
