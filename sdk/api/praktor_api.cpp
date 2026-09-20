#include "praktor.h"

#include "workflow_runner.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace {

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

bool validateRequest(const praktor_execute_request* request,
                     praktor_owned_json* output,
                     praktor_error* error) {
    if (!request || request->struct_size < sizeof(praktor_execute_request) || !output ||
        output->struct_size < sizeof(praktor_owned_json)) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST, "Invalid request or output structure");
        return false;
    }
    if (error && error->struct_size < sizeof(praktor_error)) {
        return false;
    }
    if (!isPresent(request->workflow_path) || !request->input_json ||
        request->input_json_size == 0) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "workflow_path and non-empty input_json are required");
        return false;
    }
    if (output->data) {
        setError(error, PRAKTOR_ERROR_PHASE_REQUEST,
                 "Output must be empty before workflow execution");
        return false;
    }
    return true;
}

praktor_result decodeInputs(const praktor_execute_request& request,
                            WorkflowInputs& inputs,
                            praktor_error* error) {
    try {
        const auto root = WorkflowValue::parse(
            std::string_view(request.input_json, request.input_json_size));
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

int32_t PRAKTOR_CALL executeWorkflow(const praktor_execute_request* request,
                                     praktor_owned_json* output,
                                     praktor_error* error) {
    return static_cast<int32_t>(praktor_execute_workflow(request, output, error));
}

} // namespace

praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error) {
    clearError(error);
    if (!validateRequest(request, output, error)) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    output->size = 0;

    WorkflowInputs inputs;
    const auto input_status = decodeInputs(*request, inputs, error);
    if (input_status != PRAKTOR_RESULT_SUCCESS) {
        return input_status;
    }

    WorkflowExecutionResult execution;
    try {
        WorkflowRunner runner(request->workflow_path, std::move(inputs));
        execution = runner.execute();
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

    if (!execution.success) {
        setError(error, PRAKTOR_ERROR_PHASE_EXECUTION, execution.error_message.c_str());
        return PRAKTOR_RESULT_EXECUTION_FAILED;
    }
    return PRAKTOR_RESULT_SUCCESS;
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
        PRAKTOR_CAPABILITY_JSON_WORKFLOW,
        &executeWorkflow,
        &praktor_release_json,
    };
    return &api;
}
