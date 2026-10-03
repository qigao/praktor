#include "praktor.h"

#include "workflow_plan.hpp"
#include "workflow_runner.hpp"
#include "workflow_contract.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct praktor_workflow_plan {
    explicit praktor_workflow_plan(Praktor::Plan::WorkflowPlan compiled)
        : value(std::move(compiled)) {}

    Praktor::Plan::WorkflowPlan value;
};

namespace {

constexpr uint64_t buildCapabilities() {
    uint64_t capabilities =
        PRAKTOR_CAPABILITY_JSON_WORKFLOW |
        PRAKTOR_CAPABILITY_EXECUTION_CONTROL |
        PRAKTOR_CAPABILITY_WORKFLOW_PLAN |
        PRAKTOR_CAPABILITY_EXECUTION_EVENTS |
        PRAKTOR_CAPABILITY_HOST_TOOL;
#if PRAKTOR_SCRIPT_ENGINE_ENABLED
    capabilities |= PRAKTOR_CAPABILITY_SCRIPT_ENGINE;
#endif
    return capabilities;
}

bool isPresent(const char* value) {
    return value && value[0] != '\0';
}

void clearError(praktor_error* error) {
    if (!error || error->struct_size < sizeof(praktor_error)) {
        return;
    }
    error->phase = PRAKTOR_ERROR_PHASE_NONE;
    error->message[0] = '\0';
}

void setError(praktor_error* error, praktor_error_phase phase, const char* message) {
    if (!error || error->struct_size < sizeof(praktor_error)) {
        return;
    }
    error->phase = phase;
    std::snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
}

bool validateError(praktor_error* error) {
    return !error || error->struct_size >= sizeof(praktor_error);
}

bool validateOutput(praktor_owned_json* output, praktor_error* error) {
    if (!output || output->struct_size < sizeof(praktor_owned_json)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST, "Invalid output structure");
        return false;
    }
    if (output->data) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Output must be empty before operation");
        return false;
    }
    return true;
}

bool validateRequest(const praktor_execute_request* request,
                     praktor_owned_json* output,
                     praktor_error* error) {
    if (!request || request->struct_size < sizeof(praktor_execute_request) ||
        !validateOutput(output, error)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST, "Invalid request or output structure");
        return false;
    }
    if (!validateError(error)) {
        return false;
    }
    if (!isPresent(request->workflow_path) || !request->input_json ||
        request->input_json_size == 0) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "workflow_path and non-empty input_json are required");
        return false;
    }
    return true;
}

bool validateControl(const praktor_execution_control* control,
                     praktor_error* error) {
    if (!control) {
        return true;
    }
    if (control->struct_size < sizeof(praktor_execution_control) ||
        control->reserved0 != 0) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Invalid execution control structure");
        return false;
    }
    for (const auto value : control->reserved) {
        if (value != 0) {
            setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                     "Execution control reserved fields must be zero");
            return false;
        }
    }
    if (control->timeout_ms >
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Execution control timeout_ms is too large");
        return false;
    }
    return true;
}

bool validateObserver(const praktor_execution_observer* observer,
                      praktor_error* error) {
    if (!observer) {
        return true;
    }
    if (observer->struct_size < sizeof(praktor_execution_observer)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Invalid execution observer structure");
        return false;
    }
    for (const auto value : observer->reserved) {
        if (value != 0) {
            setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                     "Execution observer reserved fields must be zero");
            return false;
        }
    }
    return true;
}

bool validateHostToolExecutor(const praktor_host_tool_executor* host_tools,
                              praktor_error* error) {
    if (!host_tools) {
        return true;
    }
    if (host_tools->struct_size < sizeof(praktor_host_tool_executor) ||
        host_tools->abi_version != PRAKTOR_HOST_TOOL_EXECUTOR_ABI_VERSION ||
        !host_tools->validate || !host_tools->invoke) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Invalid HostTool executor structure");
        return false;
    }
    for (const auto value : host_tools->reserved) {
        if (value != 0) {
            setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                     "HostTool executor reserved fields must be zero");
            return false;
        }
    }
    return true;
}

praktor_event_type toPublicEventType(
    Praktor::Execution::ExecutionEventType type) {
    using Type = Praktor::Execution::ExecutionEventType;
    switch (type) {
    case Type::WorkflowStarted:
        return PRAKTOR_EVENT_WORKFLOW_STARTED;
    case Type::TaskStarted:
        return PRAKTOR_EVENT_TASK_STARTED;
    case Type::TaskProgress:
        return PRAKTOR_EVENT_TASK_PROGRESS;
    case Type::TaskCompleted:
        return PRAKTOR_EVENT_TASK_COMPLETED;
    case Type::TaskFailed:
        return PRAKTOR_EVENT_TASK_FAILED;
    case Type::WorkflowCompleted:
        return PRAKTOR_EVENT_WORKFLOW_COMPLETED;
    }
    return PRAKTOR_EVENT_WORKFLOW_COMPLETED;
}

std::shared_ptr<Praktor::Execution::ExecutionObserver> makeExecutionObserver(
    const praktor_execution_observer* observer) {
    if (!observer || !observer->on_event) {
        return {};
    }

    return std::make_shared<Praktor::Execution::ExecutionObserver>(
        [observer](const Praktor::Execution::ExecutionEvent& event) {
            praktor_execution_event public_event{};
            public_event.struct_size = sizeof(public_event);
            public_event.sequence = event.sequence;
            public_event.type = toPublicEventType(event.type);
            public_event.task_name =
                event.task_name.empty() ? nullptr : event.task_name.c_str();
            public_event.status =
                event.status.empty() ? nullptr : event.status.c_str();
            public_event.message =
                event.message.empty() ? nullptr : event.message.c_str();
            public_event.thread_id = observer->thread_id;
            public_event.run_id = observer->run_id;
            public_event.turn_id = observer->turn_id;
            public_event.tool_call_id = observer->tool_call_id;
            try {
                observer->on_event(&public_event, observer->user_data);
            } catch (...) {
                // Observation must never alter workflow execution semantics.
            }
        });
}

struct CancellationProbeBridge {
    praktor_cancel_probe_fn probe = nullptr;
    void* user_data = nullptr;
};

bool pollCancellationProbe(void* user_data) noexcept {
    const auto* bridge = static_cast<const CancellationProbeBridge*>(user_data);
    if (!bridge || !bridge->probe) {
        return false;
    }
    try {
        return bridge->probe(bridge->user_data) != 0;
    } catch (...) {
        return true;
    }
}

std::shared_ptr<Praktor::Execution::ExecutionControl> makeExecutionControl(
    const praktor_execution_control* control,
    CancellationProbeBridge& bridge) {
    if (!control) {
        return {};
    }

    using Control = Praktor::Execution::ExecutionControl;
    std::optional<Control::Clock::time_point> deadline;
    if (control->timeout_ms != 0) {
        const auto now = Control::Clock::now();
        const auto max_remaining_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Control::Clock::time_point::max() - now).count();
        const auto requested_ms =
            static_cast<int64_t>(control->timeout_ms);
        deadline = requested_ms >= max_remaining_ms
            ? Control::Clock::time_point::max()
            : now + std::chrono::duration_cast<Control::Clock::duration>(
                        std::chrono::milliseconds(requested_ms));
    }

    bridge.probe = control->is_cancelled;
    bridge.user_data = control->user_data;
    return std::make_shared<Control>(
        deadline,
        bridge.probe ? &pollCancellationProbe : nullptr,
        bridge.probe ? static_cast<void*>(&bridge) : nullptr);
}

struct HostToolSinkBuffer {
    std::string json;
    bool called = false;
};

int32_t PRAKTOR_CALL collectHostToolResult(
    const char* json, size_t json_size, void* user_data) {
    auto* buffer = static_cast<HostToolSinkBuffer*>(user_data);
    if (!buffer || buffer->called || (!json && json_size != 0)) {
        return -1;
    }
    try {
        buffer->json.assign(json ? json : "", json_size);
        buffer->called = true;
        return 0;
    } catch (...) {
        return -1;
    }
}

Praktor::Execution::HostToolStatus hostToolStatus(int32_t status) {
    using Status = Praktor::Execution::HostToolStatus;
    switch (status) {
    case PRAKTOR_HOST_TOOL_OK:
        return Status::Ok;
    case PRAKTOR_HOST_TOOL_NOT_FOUND:
        return Status::NotFound;
    case PRAKTOR_HOST_TOOL_DENIED:
        return Status::Denied;
    case PRAKTOR_HOST_TOOL_CANCELLED:
        return Status::Cancelled;
    case PRAKTOR_HOST_TOOL_TIMED_OUT:
        return Status::TimedOut;
    case PRAKTOR_HOST_TOOL_FAILED:
    default:
        return Status::Failed;
    }
}

class CHostToolHost final : public Praktor::Execution::HostToolHost {
public:
    CHostToolHost(const praktor_host_tool_executor* executor,
                  const praktor_execution_control* control,
                  const praktor_execution_observer* observer)
        : executor_(executor), control_(control), observer_(observer) {}

    bool validate(std::string_view tool_name,
                  const WorkflowValue& argument_template,
                  std::string* error_message) override {
        if (!executor_ || !executor_->validate) {
            if (error_message) *error_message = "HostTool executor is unavailable";
            return false;
        }

        const std::string tool(tool_name);
        const std::string arguments_json = argument_template.to_string();
        praktor_error callback_error = PRAKTOR_ERROR_INIT;
        int32_t status = PRAKTOR_HOST_TOOL_FAILED;
        try {
            status = executor_->validate(
                executor_->user_data, tool.c_str(),
                arguments_json.data(), arguments_json.size(),
                &callback_error);
        } catch (...) {
            status = PRAKTOR_HOST_TOOL_FAILED;
            std::snprintf(callback_error.message, sizeof(callback_error.message),
                          "%s", "HostTool validate callback threw");
        }

        if (status == PRAKTOR_HOST_TOOL_OK) {
            return true;
        }
        if (error_message) {
            if (callback_error.message[0]) {
                *error_message = callback_error.message;
            } else if (status == PRAKTOR_HOST_TOOL_NOT_FOUND) {
                *error_message = "HostTool identity not found: " + tool;
            } else if (status == PRAKTOR_HOST_TOOL_DENIED) {
                *error_message = "HostTool identity denied: " + tool;
            } else {
                *error_message = "HostTool preflight failed: " + tool;
            }
        }
        return false;
    }

    Praktor::Execution::HostToolResult invoke(
        std::string_view tool_name,
        const WorkflowValue& arguments,
        const std::shared_ptr<Praktor::Execution::ExecutionControl>&,
        const std::shared_ptr<Praktor::Execution::ExecutionObserver>&) override {
        using Result = Praktor::Execution::HostToolResult;
        using Status = Praktor::Execution::HostToolStatus;

        Result result;
        if (!executor_ || !executor_->invoke) {
            result.status = Status::Failed;
            result.error_message = "HostTool executor is unavailable";
            return result;
        }

        const std::string tool(tool_name);
        const std::string arguments_json = arguments.to_string();
        HostToolSinkBuffer sink;
        praktor_error callback_error = PRAKTOR_ERROR_INIT;
        int32_t callback_status = PRAKTOR_HOST_TOOL_FAILED;
        try {
            callback_status = executor_->invoke(
                executor_->user_data, tool.c_str(),
                arguments_json.data(), arguments_json.size(),
                control_, observer_,
                &collectHostToolResult, &sink,
                &callback_error);
        } catch (...) {
            callback_status = PRAKTOR_HOST_TOOL_FAILED;
            std::snprintf(callback_error.message, sizeof(callback_error.message),
                          "%s", "HostTool invoke callback threw");
        }

        result.status = hostToolStatus(callback_status);
        if (result.status == Status::Ok) {
            if (!sink.called) {
                result.status = Status::Failed;
                result.error_message =
                    "HostTool returned success without one result payload";
                return result;
            }
            try {
                result.value = WorkflowValue::parse(sink.json);
            } catch (const std::exception& exception) {
                result.status = Status::Failed;
                result.error_message =
                    std::string("HostTool returned invalid canonical JSON: ") +
                    exception.what();
            }
            return result;
        }

        if (callback_error.message[0]) {
            result.error_message = callback_error.message;
        }
        return result;
    }

private:
    const praktor_host_tool_executor* executor_;
    const praktor_execution_control* control_;
    const praktor_execution_observer* observer_;
};

praktor_result decodeInputs(const char* input_json,
                            size_t input_json_size,
                            WorkflowInputs& inputs,
                            praktor_error* error) {
    try {
        const auto root = WorkflowValue::parse(
            std::string_view(input_json, input_json_size));
        if (!root.is_object()) {
            setError(error, PRAKTOR_ERROR_PHASE_INPUT_JSON,
                     "Workflow input JSON root must be an object");
            return PRAKTOR_RESULT_INVALID_JSON;
        }
        inputs.clear();
        inputs.reserve(root.size());
        for (const auto& member : root.object_range()) {
            inputs.emplace(member.key(), member.value());
        }
        return PRAKTOR_RESULT_SUCCESS;
    } catch (const std::bad_alloc&) {
        setError(error, PRAKTOR_ERROR_PHASE_INPUT_JSON,
                 "Out of memory while decoding workflow input JSON");
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    } catch (const std::exception& exception) {
        setError(error, PRAKTOR_ERROR_PHASE_INPUT_JSON, exception.what());
        return PRAKTOR_RESULT_INVALID_JSON;
    }
}

praktor_result validateWorkflowInputContract(
    const char* workflow_path,
    WorkflowInputs& inputs,
    praktor_error* error) {
    try {
        const std::filesystem::path path(workflow_path);
        const Workflow workflow = TaskParser::parseFileWithIncludes(
            path.string(), path.parent_path().string());
        inputs = Praktor::Contract::validateAndApplyInputs(workflow, inputs);
        return PRAKTOR_RESULT_SUCCESS;
    } catch (const Praktor::Contract::WorkflowContractError& exception) {
        setError(error, PRAKTOR_ERROR_PHASE_INPUT_CONTRACT, exception.what());
        return PRAKTOR_RESULT_INPUT_CONTRACT;
    } catch (...) {
        // Preserve legacy parse/execution error semantics for non-contract failures.
        return PRAKTOR_RESULT_SUCCESS;
    }
}

praktor_result encodeResult(const WorkflowValue& result,
                            praktor_owned_json& output,
                            praktor_error* error) {
    try {
        const std::string json = result.to_string();
        if (json.size() == std::numeric_limits<size_t>::max()) {
            setError(error, PRAKTOR_ERROR_PHASE_RESULT_JSON,
                     "Workflow result JSON is too large");
            return PRAKTOR_RESULT_OUT_OF_MEMORY;
        }
        auto* data = static_cast<char*>(std::malloc(json.size() + 1));
        if (!data) {
            setError(error, PRAKTOR_ERROR_PHASE_RESULT_JSON,
                     "Out of memory while encoding workflow result JSON");
            return PRAKTOR_RESULT_OUT_OF_MEMORY;
        }
        std::memcpy(data, json.data(), json.size());
        data[json.size()] = '\0';
        output.data = data;
        output.size = json.size();
        return PRAKTOR_RESULT_SUCCESS;
    } catch (const std::bad_alloc&) {
        setError(error, PRAKTOR_ERROR_PHASE_RESULT_JSON,
                 "Out of memory while encoding workflow result JSON");
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    } catch (const std::exception& exception) {
        setError(error, PRAKTOR_ERROR_PHASE_RESULT_JSON, exception.what());
        return PRAKTOR_RESULT_INTERNAL_ERROR;
    }
}

praktor_result executeWorkflowImpl(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    std::shared_ptr<Praktor::Execution::HostToolHost> host_tool_host,
    praktor_owned_json* output,
    praktor_error* error) {
    clearError(error);
    if (!validateRequest(request, output, error) ||
        !validateControl(control, error) ||
        !validateObserver(observer, error)) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    output->size = 0;

    WorkflowInputs inputs;
    const auto input_status =
        decodeInputs(request->input_json, request->input_json_size, inputs, error);
    if (input_status != PRAKTOR_RESULT_SUCCESS) {
        return input_status;
    }

    const auto contract_status =
        validateWorkflowInputContract(request->workflow_path, inputs, error);
    if (contract_status != PRAKTOR_RESULT_SUCCESS) {
        return contract_status;
    }

    WorkflowExecutionResult execution;
    try {
        CancellationProbeBridge bridge;
        auto execution_control = makeExecutionControl(control, bridge);
        auto execution_observer = makeExecutionObserver(observer);
        WorkflowRunner runner(request->workflow_path, std::move(inputs));
        execution = runner.executeObservedWithHostTools(
            std::move(execution_control),
            std::move(execution_observer),
            std::move(host_tool_host));
    } catch (const std::bad_alloc&) {
        setError(error, PRAKTOR_ERROR_PHASE_EXECUTION,
                 "Out of memory while executing workflow");
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    } catch (const std::exception& exception) {
        setError(error, PRAKTOR_ERROR_PHASE_EXECUTION, exception.what());
        return PRAKTOR_RESULT_INTERNAL_ERROR;
    }

    const auto output_status = encodeResult(execution.value, *output, error);
    if (output_status != PRAKTOR_RESULT_SUCCESS) {
        return output_status;
    }

    if (execution.success) {
        return PRAKTOR_RESULT_SUCCESS;
    }

    setError(error, PRAKTOR_ERROR_PHASE_EXECUTION, execution.error_message.c_str());
    const std::string status =
        execution.value["workflow_status"].as<std::string>();
    if (status == "cancelled") {
        return PRAKTOR_RESULT_CANCELLED;
    }
    if (status == "timed_out") {
        return PRAKTOR_RESULT_TIMED_OUT;
    }
    return PRAKTOR_RESULT_EXECUTION_FAILED;
}

int32_t PRAKTOR_CALL executeWorkflow(const praktor_execute_request* request,
                                     praktor_owned_json* output,
                                     praktor_error* error) {
    return static_cast<int32_t>(praktor_execute_workflow(request, output, error));
}

int32_t PRAKTOR_CALL executeWorkflowControlled(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_execute_workflow_controlled(request, control, output, error));
}

int32_t PRAKTOR_CALL compileWorkflow(
    const praktor_compile_request* request,
    praktor_workflow_plan** out_plan,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_compile_workflow(request, out_plan, error));
}

int32_t PRAKTOR_CALL describeWorkflowPlan(
    const praktor_workflow_plan* plan,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_describe_workflow_plan(plan, output, error));
}

int32_t PRAKTOR_CALL executeWorkflowPlan(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_execute_workflow_plan(request, control, output, error));
}

int32_t PRAKTOR_CALL executeWorkflowPlanObserved(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_execute_workflow_plan_observed(
            request, control, observer, output, error));
}

int32_t PRAKTOR_CALL executeWorkflowPlanHostTools(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    const praktor_host_tool_executor* host_tools,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_execute_workflow_plan_host_tools(
            request, control, observer, host_tools, output, error));
}

} // namespace

praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowImpl(request, nullptr, nullptr, {}, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_controlled(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowImpl(request, control, nullptr, {}, output, error);
}

praktor_result PRAKTOR_CALL praktor_compile_workflow(
    const praktor_compile_request* request,
    praktor_workflow_plan** out_plan,
    praktor_error* error) {
    clearError(error);
    if (!request || request->struct_size < sizeof(praktor_compile_request) ||
        !out_plan || *out_plan != nullptr || !validateError(error) ||
        !isPresent(request->workflow_path)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Valid workflow_path and empty out_plan are required");
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }

    try {
        auto compiled = Praktor::Plan::WorkflowPlan::compile(request->workflow_path);
        *out_plan = new praktor_workflow_plan(std::move(compiled));
        return PRAKTOR_RESULT_SUCCESS;
    } catch (const std::bad_alloc&) {
        setError(error, PRAKTOR_ERROR_PHASE_PLAN,
                 "Out of memory while compiling WorkflowPlan");
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    } catch (const std::exception& exception) {
        setError(error, PRAKTOR_ERROR_PHASE_PLAN, exception.what());
        return PRAKTOR_RESULT_PLAN_INVALID;
    }
}

praktor_result PRAKTOR_CALL praktor_describe_workflow_plan(
    const praktor_workflow_plan* plan,
    praktor_owned_json* output,
    praktor_error* error) {
    clearError(error);
    if (!plan || !validateError(error) || !validateOutput(output, error)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Valid WorkflowPlan and empty output are required");
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    output->size = 0;
    return encodeResult(plan->value.toValue(), *output, error);
}

namespace {

praktor_result executeWorkflowPlanImpl(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    const praktor_host_tool_executor* host_tools,
    praktor_owned_json* output,
    praktor_error* error) {
    clearError(error);
    if (!request || request->struct_size < sizeof(praktor_plan_execute_request) ||
        !request->plan || !request->input_json || request->input_json_size == 0 ||
        !validateError(error) || !validateOutput(output, error) ||
        !validateControl(control, error) || !validateObserver(observer, error) ||
        !validateHostToolExecutor(host_tools, error)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Valid WorkflowPlan, execution controls, non-empty input_json, and empty output are required");
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }

    std::string validation_error;
    if (!request->plan->value.validate(&validation_error)) {
        setError(error, PRAKTOR_ERROR_PHASE_PLAN, validation_error.c_str());
        return PRAKTOR_RESULT_PLAN_MISMATCH;
    }

    std::shared_ptr<Praktor::Execution::HostToolHost> host_tool_host;
    const auto& reviewed_host_tools = request->plan->value.hostTools();
    if (!reviewed_host_tools.empty()) {
        if (!host_tools) {
            setError(error, PRAKTOR_ERROR_PHASE_HOST_TOOL,
                     "WorkflowPlan requires a HostTool executor");
            return PRAKTOR_RESULT_HOST_TOOL_REJECTED;
        }
        host_tool_host = std::make_shared<CHostToolHost>(
            host_tools, control, observer);
        for (const auto& reviewed : reviewed_host_tools) {
            std::string preflight_error;
            if (!host_tool_host->validate(
                    reviewed.tool_name, reviewed.argument_template,
                    &preflight_error)) {
                setError(error, PRAKTOR_ERROR_PHASE_HOST_TOOL,
                         preflight_error.c_str());
                return PRAKTOR_RESULT_HOST_TOOL_REJECTED;
            }
        }
    }

    praktor_execute_request execute_request = PRAKTOR_EXECUTE_REQUEST_INIT;
    execute_request.workflow_path = request->plan->value.rootPath().c_str();
    execute_request.input_json = request->input_json;
    execute_request.input_json_size = request->input_json_size;
    return executeWorkflowImpl(
        &execute_request, control, observer, std::move(host_tool_host),
        output, error);
}

} // namespace

praktor_result PRAKTOR_CALL praktor_execute_workflow_plan(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowPlanImpl(
        request, control, nullptr, nullptr, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_plan_observed(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowPlanImpl(
        request, control, observer, nullptr, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_plan_host_tools(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    const praktor_host_tool_executor* host_tools,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowPlanImpl(
        request, control, observer, host_tools, output, error);
}

void PRAKTOR_CALL praktor_release_workflow_plan(praktor_workflow_plan* plan) {
    delete plan;
}

void PRAKTOR_CALL praktor_release_json(praktor_owned_json* data) {
    if (!data || data->struct_size < sizeof(praktor_owned_json)) {
        return;
    }
    std::free(data->data);
    data->data = nullptr;
    data->size = 0;
}

const praktor_api* PRAKTOR_CALL praktor_get_api(void) {
    static const praktor_api api = {
        sizeof(praktor_api),
        PRAKTOR_ABI_MAJOR,
        PRAKTOR_ABI_MINOR,
        buildCapabilities(),
        &executeWorkflow,
        &praktor_release_json,
        &executeWorkflowControlled,
        &compileWorkflow,
        &describeWorkflowPlan,
        &executeWorkflowPlan,
        &praktor_release_workflow_plan,
        &executeWorkflowPlanObserved,
        &executeWorkflowPlanHostTools,
    };
    return &api;
}
