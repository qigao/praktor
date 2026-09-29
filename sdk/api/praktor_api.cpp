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
        PRAKTOR_CAPABILITY_EXECUTION_EVENTS;
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
    if (observer->struct_size < sizeof(praktor_execution_observer) ||
        observer->reserved0 != 0 || !observer->on_event) {
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

praktor_event_type eventType(
    Praktor::Execution::ExecutionEventType type) noexcept {
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

std::string copyOptionalText(const char* value) {
    return value ? std::string(value) : std::string();
}

std::shared_ptr<Praktor::Execution::ExecutionObserver> makeExecutionObserver(
    const praktor_execution_observer* observer) {
    if (!observer) {
        return {};
    }

    Praktor::Execution::ExecutionLineage lineage;
    lineage.thread_id = copyOptionalText(observer->thread_id);
    lineage.run_id = copyOptionalText(observer->run_id);
    lineage.turn_id = copyOptionalText(observer->turn_id);
    lineage.tool_call_id = copyOptionalText(observer->tool_call_id);

    const auto callback = observer->on_event;
    void* const user_data = observer->user_data;
    return std::make_shared<Praktor::Execution::ExecutionObserver>(
        std::move(lineage),
        [callback, user_data](
            const Praktor::Execution::ExecutionEvent& event,
            const Praktor::Execution::ExecutionLineage& event_lineage) {
            praktor_execution_event view{};
            view.struct_size = sizeof(view);
            view.type = eventType(event.type);
            view.sequence = event.sequence;
            view.task_name =
                event.task_name.empty() ? nullptr : event.task_name.c_str();
            view.status = event.status.empty() ? nullptr : event.status.c_str();
            view.payload_json =
                event.payload_json.empty() ? nullptr : event.payload_json.c_str();
            view.payload_json_size = event.payload_json.size();
            view.thread_id = event_lineage.thread_id.empty()
                ? nullptr : event_lineage.thread_id.c_str();
            view.run_id = event_lineage.run_id.empty()
                ? nullptr : event_lineage.run_id.c_str();
            view.turn_id = event_lineage.turn_id.empty()
                ? nullptr : event_lineage.turn_id.c_str();
            view.tool_call_id = event_lineage.tool_call_id.empty()
                ? nullptr : event_lineage.tool_call_id.c_str();
            callback(&view, user_data);
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
        execution = (execution_control || execution_observer)
            ? runner.executeObserved(
                  std::move(execution_control), std::move(execution_observer))
            : runner.execute();
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

int32_t PRAKTOR_CALL executeWorkflowObserved(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    return static_cast<int32_t>(
        praktor_execute_workflow_observed(
            request, control, observer, output, error));
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

} // namespace

praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowImpl(request, nullptr, nullptr, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_controlled(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowImpl(request, control, nullptr, output, error);
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

praktor_result executeWorkflowPlanImpl(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    clearError(error);
    if (!request || request->struct_size < sizeof(praktor_plan_execute_request) ||
        !request->plan || !request->input_json || request->input_json_size == 0 ||
        !validateError(error) || !validateOutput(output, error) ||
        !validateControl(control, error) || !validateObserver(observer, error)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Valid WorkflowPlan, non-empty input_json, and empty output are required");
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }

    std::string validation_error;
    if (!request->plan->value.validate(&validation_error)) {
        setError(error, PRAKTOR_ERROR_PHASE_PLAN, validation_error.c_str());
        return PRAKTOR_RESULT_PLAN_MISMATCH;
    }

    praktor_execute_request execute_request = PRAKTOR_EXECUTE_REQUEST_INIT;
    execute_request.workflow_path = request->plan->value.rootPath().c_str();
    execute_request.input_json = request->input_json;
    execute_request.input_json_size = request->input_json_size;
    return executeWorkflowImpl(
        &execute_request, control, observer, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_plan(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowPlanImpl(request, control, nullptr, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_observed(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowImpl(request, control, observer, output, error);
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_plan_observed(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error) {
    return executeWorkflowPlanImpl(request, control, observer, output, error);
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
        &executeWorkflowObserved,
        &executeWorkflowPlanObserved,
    };
    return &api;
}
