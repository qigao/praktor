#include "executors/host_tool_executor.hpp"

#include "execution/host_tool.hpp"
#include "util/variable_substitution.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Praktor::Execution {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool exactPathTemplate(std::string_view value, std::string& out_path) {
    value = trim(value);
    if (value.size() < 4 || value.substr(0, 2) != "{{" ||
        value.substr(value.size() - 2) != "}}") {
        return false;
    }

    std::string_view inner = trim(value.substr(2, value.size() - 4));
    if (inner.empty() || inner.find("{{") != std::string_view::npos ||
        inner.find("}}") != std::string_view::npos) {
        return false;
    }
    out_path.assign(inner);
    return true;
}

WorkflowValue resolveValue(const WorkflowValue& value,
                           const WorkflowContext& context) {
    if (value.is_string()) {
        const std::string source = value.as<std::string>();
        std::string path;
        if (exactPathTemplate(source, path)) {
            WorkflowValue resolved = context.getValueByPath(path);
            if (!resolved.is_null()) {
                return resolved;
            }
        }
        return WorkflowValue(substituteVariables(source, context));
    }

    if (value.is_array()) {
        WorkflowValue result = WorkflowValue::array();
        for (const auto& item : value.array_range()) {
            result.push_back(resolveValue(item, context));
        }
        return result;
    }

    if (value.is_object()) {
        WorkflowValue result = WorkflowValue::object();
        for (const auto& item : value.object_range()) {
            result[item.key()] = resolveValue(item.value(), context);
        }
        return result;
    }

    return value;
}

TaskResult failedResult(TaskErrorCode code,
                        std::string message,
                        const std::string& tool,
                        std::string_view status) {
    WorkflowValue details = WorkflowValue::object();
    details["tool"] = tool;
    details["host_status"] = std::string(status);
    return TaskResult::fail(code, "host_tool", std::move(message),
                            std::move(details));
}

} // namespace

TaskResult HostToolTaskExecutor::execute(const Task& task,
                                         WorkflowContext& context) {
    const HostToolParams* params = std::get_if<HostToolParams>(&task.specifics);
    if (!params || params->tool.empty()) {
        return failedResult(TaskErrorCode::SchemaInvalid,
                            "HostTool task has invalid typed parameters",
                            params ? params->tool : std::string{},
                            "invalid");
    }

    const auto host = context.getHostToolHost();
    if (!host) {
        return failedResult(
            TaskErrorCode::HostToolUnavailable,
            "HostTool execution requires a host executor supplied by reviewed-plan execution",
            params->tool, "unavailable");
    }

    const auto control = context.getExecutionControl();
    if (control && control->stopRequested()) {
        const auto reason = control->stopReason();
        return failedResult(
            reason == ExecutionControl::StopReason::DeadlineExceeded
                ? TaskErrorCode::Timeout
                : TaskErrorCode::Cancelled,
            reason == ExecutionControl::StopReason::DeadlineExceeded
                ? "HostTool execution deadline exceeded before invocation"
                : "HostTool execution cancelled before invocation",
            params->tool,
            reason == ExecutionControl::StopReason::DeadlineExceeded
                ? "timed_out"
                : "cancelled");
    }

    WorkflowValue arguments = resolveValue(params->arguments, context);
    if (!arguments.is_object()) {
        return failedResult(
            TaskErrorCode::SchemaInvalid,
            "HostTool arguments must resolve to one JSON object",
            params->tool, "invalid_arguments");
    }

    const std::uint64_t total_attempts =
        static_cast<std::uint64_t>(task.retry_count) + 1u;
    for (std::uint64_t attempt = 0; attempt < total_attempts; ++attempt) {
        if (control && control->stopRequested()) {
            const auto reason = control->stopReason();
            return failedResult(
                reason == ExecutionControl::StopReason::DeadlineExceeded
                    ? TaskErrorCode::Timeout
                    : TaskErrorCode::Cancelled,
                reason == ExecutionControl::StopReason::DeadlineExceeded
                    ? "HostTool execution deadline exceeded before retry invocation"
                    : "HostTool execution cancelled before retry invocation",
                params->tool,
                reason == ExecutionControl::StopReason::DeadlineExceeded
                    ? "timed_out"
                    : "cancelled");
        }

        HostToolResult host_result = host->invoke(
            params->tool, arguments, control, context.getExecutionObserver());

        switch (host_result.status) {
        case HostToolStatus::Ok:
            context.setCurrentTaskOutput("result", host_result.value);
            return TaskResult(true);
        case HostToolStatus::NotFound:
            return failedResult(
                TaskErrorCode::ResourceNotFound,
                host_result.error_message.empty()
                    ? "HostTool identity is no longer available"
                    : std::move(host_result.error_message),
                params->tool, "not_found");
        case HostToolStatus::Denied:
            return failedResult(
                TaskErrorCode::HostToolDenied,
                host_result.error_message.empty()
                    ? "HostTool invocation was denied by the embedding host"
                    : std::move(host_result.error_message),
                params->tool, "denied");
        case HostToolStatus::Cancelled:
            return failedResult(
                TaskErrorCode::Cancelled,
                host_result.error_message.empty()
                    ? "HostTool invocation was cancelled"
                    : std::move(host_result.error_message),
                params->tool, "cancelled");
        case HostToolStatus::TimedOut:
            return failedResult(
                TaskErrorCode::Timeout,
                host_result.error_message.empty()
                    ? "HostTool invocation exceeded its deadline"
                    : std::move(host_result.error_message),
                params->tool, "timed_out");
        case HostToolStatus::Failed:
            if (attempt + 1u < total_attempts) {
                continue;
            }
            return failedResult(
                TaskErrorCode::HostToolFailed,
                host_result.error_message.empty()
                    ? "HostTool invocation failed after finite retries"
                    : std::move(host_result.error_message),
                params->tool, "failed");
        }
    }

    return failedResult(TaskErrorCode::HostToolFailed,
                        "HostTool retry state is inconsistent",
                        params->tool, "unknown");
}

std::unique_ptr<TaskExecutor> createHostToolTaskExecutor() {
    return std::make_unique<HostToolTaskExecutor>();
}

} // namespace Praktor::Execution
