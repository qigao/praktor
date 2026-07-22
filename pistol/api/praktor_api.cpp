#include "praktor.h"

#include "data_bind_adapter.hpp"
#include "workflow_runner.hpp"

#include <data_bind.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

bool isPresent(const char* value) {
    return value && value[0] != '\0';
}

bool isValidFormat(praktor_data_format format) {
    return format >= PRAKTOR_DATA_FORMAT_JSON && format <= PRAKTOR_DATA_FORMAT_BINARY;
}

void clearError(praktor_error* error) {
    if (!error || error->struct_size < sizeof(praktor_error)) {
        return;
    }
    error->data_bind_status = DATA_BIND_OK;
    error->line = -1;
    error->column = -1;
    error->path[0] = '\0';
    error->message[0] = '\0';
}

void setErrorMessage(praktor_error* error, const char* message) {
    if (!error || error->struct_size < sizeof(praktor_error)) {
        return;
    }
    std::snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
}

void copyDataBindError(praktor_error* destination, const DataBindError& source) {
    if (!destination || destination->struct_size < sizeof(praktor_error)) {
        return;
    }
    destination->data_bind_status = static_cast<int32_t>(source.code);
    destination->line = source.line;
    destination->column = source.column;
    std::snprintf(destination->path, sizeof(destination->path), "%s", source.path);
    std::snprintf(destination->message, sizeof(destination->message), "%s", source.message);
}

praktor_result mapCodecCreationStatus(DataBindStatus status) {
    if (status == DATA_BIND_ERR_INVALID_ARG) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    if (status == DATA_BIND_ERR_OOM) {
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    }
    if (status == DATA_BIND_ERR_TYPE_NOT_FOUND) {
        return PRAKTOR_RESULT_TYPE_NOT_FOUND;
    }
    return PRAKTOR_RESULT_INVALID_SCHEMA;
}

praktor_result mapInputStatus(DataBindStatus status) {
    if (status == DATA_BIND_ERR_INVALID_ARG) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    if (status == DATA_BIND_ERR_OOM) {
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    }
    if (status == DATA_BIND_ERR_TYPE_NOT_FOUND) {
        return PRAKTOR_RESULT_TYPE_NOT_FOUND;
    }
    return PRAKTOR_RESULT_INVALID_DATA;
}

praktor_result mapOutputStatus(DataBindStatus status) {
    if (status == DATA_BIND_ERR_INVALID_ARG) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }
    if (status == DATA_BIND_ERR_OOM) {
        return PRAKTOR_RESULT_OUT_OF_MEMORY;
    }
    if (status == DATA_BIND_ERR_TYPE_NOT_FOUND) {
        return PRAKTOR_RESULT_TYPE_NOT_FOUND;
    }
    return PRAKTOR_RESULT_SERIALIZATION_FAILED;
}

bool validateRequest(const praktor_execute_request* request,
                     praktor_owned_data* output,
                     praktor_error* error) {
    if (!request || request->struct_size < sizeof(praktor_execute_request) ||
        !output || output->struct_size < sizeof(praktor_owned_data)) {
        setErrorMessage(error, "Invalid request or output structure");
        return false;
    }
    if (error && error->struct_size < sizeof(praktor_error)) {
        return false;
    }
    if (!isPresent(request->workflow_path) || !request->input_data || request->input_size == 0 ||
        !isPresent(request->input_type) || !isPresent(request->output_type) ||
        !isValidFormat(request->input_format) || !isValidFormat(request->output_format)) {
        setErrorMessage(error, "Missing or invalid workflow data request field");
        return false;
    }
    if (output->data) {
        setErrorMessage(error, "Output must be empty before execution");
        return false;
    }
    return true;
}

WorkflowInputs parseLegacyJson(const char* json_vars) {
    WorkflowInputs inputs;
    if (!json_vars || json_vars[0] == '\0') {
        return inputs;
    }

    const auto root = WorkflowValue::parse(json_vars);
    if (!root.is_object()) {
        throw std::invalid_argument("Workflow variables must be a JSON object");
    }
    for (const auto& member : root.object_range()) {
        inputs.emplace(member.key(), member.value());
    }
    return inputs;
}

int32_t PRAKTOR_CALL execute_workflow_v1(const char* workflow_path, const char* json_vars) {
    return static_cast<int32_t>(praktor_execute_workflow(workflow_path, json_vars));
}

int32_t PRAKTOR_CALL execute_workflow_v2(const praktor_execute_request* request,
                                         praktor_owned_data* output,
                                         praktor_error* error) {
    return static_cast<int32_t>(praktor_execute_workflow_data(request, output, error));
}

} // namespace

praktor_result PRAKTOR_CALL praktor_execute_workflow(const char* workflow_path,
                                                     const char* json_vars) {
    if (!isPresent(workflow_path)) {
        return PRAKTOR_API_INVALID_ARGUMENT;
    }

    try {
        WorkflowRunner runner(workflow_path, parseLegacyJson(json_vars));
        return runner.run() ? PRAKTOR_API_SUCCESS : PRAKTOR_API_EXECUTION_FAILED;
    } catch (const std::exception& exception) {
        std::cerr << "[Praktor API Error] Exception during workflow execution: "
                  << exception.what() << std::endl;
        return PRAKTOR_API_INVALID_JSON;
    }
}

praktor_result PRAKTOR_CALL praktor_execute_workflow_data(
    const praktor_execute_request* request,
    praktor_owned_data* output,
    praktor_error* error) {
    clearError(error);
    if (!validateRequest(request, output, error)) {
        return PRAKTOR_RESULT_INVALID_ARGUMENT;
    }

    output->size = 0;
    output->format = request->output_format;

    DataBindError data_bind_error = DATA_BIND_ERROR_INIT;
    praktor::api::DataBindAdapter adapter;
    DataBindStatus status = adapter.initialize(*request, data_bind_error);
    if (status != DATA_BIND_OK) {
        copyDataBindError(error, data_bind_error);
        return mapCodecCreationStatus(status);
    }

    WorkflowInputs inputs;
    status = adapter.decodeInput(*request, inputs, data_bind_error);
    if (status != DATA_BIND_OK) {
        copyDataBindError(error, data_bind_error);
        return mapInputStatus(status);
    }

    WorkflowExecutionResult execution;
    try {
        WorkflowRunner runner(request->workflow_path, std::move(inputs));
        execution = runner.execute();
    } catch (const std::exception& exception) {
        setErrorMessage(error, exception.what());
        return PRAKTOR_RESULT_INTERNAL_ERROR;
    }

    status = adapter.encodeResult(*request, execution.value, *output, data_bind_error);
    if (status != DATA_BIND_OK) {
        praktor::api::DataBindAdapter::release(*output);
        copyDataBindError(error, data_bind_error);
        return mapOutputStatus(status);
    }

    if (!execution.success) {
        setErrorMessage(error, execution.error_message.c_str());
        return PRAKTOR_RESULT_EXECUTION_FAILED;
    }
    return PRAKTOR_RESULT_SUCCESS;
}

void PRAKTOR_CALL praktor_release_data(praktor_owned_data* data) {
    if (!data || data->struct_size < sizeof(praktor_owned_data)) {
        return;
    }
    praktor::api::DataBindAdapter::release(*data);
}

const praktor_api_v1* PRAKTOR_CALL praktor_get_api_v1(void) {
    static const praktor_api_v1 api = {
        sizeof(praktor_api_v1),
        PRAKTOR_ABI_MAJOR,
        PRAKTOR_ABI_MINOR,
        PRAKTOR_CAPABILITY_EXECUTE_WORKFLOW,
        &execute_workflow_v1,
    };
    return &api;
}

const praktor_api_v2* PRAKTOR_CALL praktor_get_api_v2(void) {
    static const praktor_api_v2 api = {
        sizeof(praktor_api_v2),
        PRAKTOR_ABI_V2_MAJOR,
        PRAKTOR_ABI_V2_MINOR,
        PRAKTOR_CAPABILITY_EXECUTE_WORKFLOW | PRAKTOR_CAPABILITY_DATA_BIND,
        &execute_workflow_v2,
        &praktor_release_data,
    };
    return &api;
}
