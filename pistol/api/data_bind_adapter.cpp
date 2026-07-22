#include "data_bind_adapter.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace praktor::api {
namespace {

class ObjectOwner final {
public:
    ~ObjectOwner() { data_bind_object_free(value); }
    DataBindObject** out() { return &value; }
    DataBindObject* get() const { return value; }

private:
    DataBindObject* value{nullptr};
};

class TextOwner final {
public:
    ~TextOwner() { data_bind_serialized_free(value); }
    char** out() { return &value; }
    const char* get() const { return value; }

private:
    char* value{nullptr};
};

void setAdapterError(DataBindError& error, DataBindStatus status, const char* message) {
    error = DATA_BIND_ERROR_INIT;
    error.code = status;
    std::snprintf(error.message, sizeof(error.message), "%s", message);
}

DataBindStatus bindInput(DataBind* codec,
                         const praktor_execute_request& request,
                         ObjectOwner& object,
                         DataBindError& error) {
    const auto* bytes = static_cast<const uint8_t*>(request.input_data);
    const auto* text = static_cast<const char*>(request.input_data);
    switch (request.input_format) {
    case PRAKTOR_DATA_FORMAT_JSON:
        return data_bind_object_from_json(
            codec, request.input_type, text, request.input_size, object.out(), &error);
    case PRAKTOR_DATA_FORMAT_YAML:
        return data_bind_object_from_yaml(
            codec, request.input_type, text, request.input_size, object.out(), &error);
    case PRAKTOR_DATA_FORMAT_XML:
        return data_bind_object_from_xml(
            codec, request.input_type, text, request.input_size, object.out(), &error);
    case PRAKTOR_DATA_FORMAT_CSV:
        return data_bind_object_from_csv(codec, request.input_type, text, request.input_size,
                                         request.csv_row, object.out(), &error);
    case PRAKTOR_DATA_FORMAT_BINARY:
        return data_bind_object_from_bin(
            codec, request.input_type, bytes, request.input_size, object.out(), &error);
    default:
        setAdapterError(error, DATA_BIND_ERR_INVALID_ARG, "Unsupported input format");
        return error.code;
    }
}

} // namespace

DataBindAdapter::~DataBindAdapter() {
    data_bind_free(codec_);
}

DataBindStatus DataBindAdapter::initialize(const praktor_execute_request& request,
                                           DataBindError& error) {
    const bool has_path = request.schema_path && request.schema_path[0] != '\0';
    const bool has_text = request.schema_text && request.schema_text_size != 0;
    if (has_path == has_text) {
        setAdapterError(error, DATA_BIND_ERR_INVALID_ARG,
                        "Provide exactly one trusted schema path or schema text");
        return error.code;
    }

    return has_path
        ? data_bind_create(request.schema_path, &codec_, &error)
        : data_bind_create_from_text(
              request.schema_text, request.schema_text_size, &codec_, &error);
}

DataBindStatus DataBindAdapter::decodeInput(const praktor_execute_request& request,
                                            WorkflowInputs& inputs,
                                            DataBindError& error) const {
    ObjectOwner object;
    DataBindStatus status = bindInput(codec_, request, object, error);
    if (status != DATA_BIND_OK) {
        return status;
    }

    if (data_bind_value_kind(data_bind_object_value(object.get())) != DATA_BIND_VALUE_OBJECT) {
        setAdapterError(error, DATA_BIND_ERR_TYPE_MISMATCH,
                        "Workflow input type must bind to an object");
        return error.code;
    }

    TextOwner canonical_json;
    size_t json_size = 0;
    status = data_bind_object_serialize_json(
        object.get(), canonical_json.out(), &json_size, &error);
    if (status != DATA_BIND_OK) {
        return status;
    }

    try {
        const auto root = WorkflowValue::parse(
            std::string_view(canonical_json.get(), json_size));
        if (!root.is_object()) {
            setAdapterError(error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "Workflow input type must serialize as an object");
            return error.code;
        }
        inputs.clear();
        for (const auto& member : root.object_range()) {
            inputs.emplace(member.key(), member.value());
        }
        return DATA_BIND_OK;
    } catch (const std::exception& exception) {
        setAdapterError(error, DATA_BIND_ERR_RUNTIME, exception.what());
        return error.code;
    }
}

DataBindStatus DataBindAdapter::encodeResult(const praktor_execute_request& request,
                                             const WorkflowValue& result,
                                             praktor_owned_data& output,
                                             DataBindError& error) const {
    const std::string json = result.to_string();
    ObjectOwner object;
    DataBindStatus status = data_bind_object_from_json(
        codec_, request.output_type, json.data(), json.size(), object.out(), &error);
    if (status != DATA_BIND_OK) {
        return status;
    }

    output.format = request.output_format;
    switch (request.output_format) {
    case PRAKTOR_DATA_FORMAT_JSON: {
        char* data = nullptr;
        status = data_bind_object_serialize_json(object.get(), &data, &output.size, &error);
        output.data = data;
        return status;
    }
    case PRAKTOR_DATA_FORMAT_YAML: {
        char* data = nullptr;
        status = data_bind_object_serialize_yaml(object.get(), &data, &output.size, &error);
        output.data = data;
        return status;
    }
    case PRAKTOR_DATA_FORMAT_XML: {
        char* data = nullptr;
        status = data_bind_object_serialize_xml(object.get(), &data, &output.size, &error);
        output.data = data;
        return status;
    }
    case PRAKTOR_DATA_FORMAT_CSV: {
        char* data = nullptr;
        status = data_bind_object_serialize_csv(object.get(), &data, &output.size, &error);
        output.data = data;
        return status;
    }
    case PRAKTOR_DATA_FORMAT_BINARY: {
        uint8_t* data = nullptr;
        status = data_bind_object_serialize_bin(
            codec_, object.get(), &data, &output.size, &error);
        output.data = data;
        return status;
    }
    default:
        setAdapterError(error, DATA_BIND_ERR_INVALID_ARG, "Unsupported output format");
        return error.code;
    }
}

void DataBindAdapter::release(praktor_owned_data& data) noexcept {
    if (data.data) {
        if (data.format == PRAKTOR_DATA_FORMAT_BINARY) {
            data_bind_binary_free(data.data);
        } else {
            data_bind_serialized_free(static_cast<char*>(data.data));
        }
    }
    data.data = nullptr;
    data.size = 0;
}

} // namespace praktor::api
