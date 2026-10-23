#include "task_yaml_internal.hpp"
#include "yml/yaml_workflow_metadata.h"
#include <new>
#include <stdexcept>
#include <string>
namespace TaskYamlDetail {
namespace {
struct MetadataError {
    praktor_yaml_field_error value{};
    ~MetadataError() { praktor_yaml_field_error_dispose(&value); }
    MetadataError() = default;
    MetadataError(const MetadataError&) = delete;
    MetadataError& operator=(const MetadataError&) = delete;
    void check(int status) const {
        if (status == SALTS_OK) return;
        if (value.origin == PRAKTOR_YAML_FAILURE_ALLOCATION) throw std::bad_alloc();
        throw std::runtime_error(value.text ? std::string(value.text, tstr_len(value.text))
                                            : std::string("Failed to parse workflow metadata"));
    }
};
}
json_value_t* read_workflow_metadata_or_throw(const YamlNodeRef& node) {
    MetadataError error;
    json_value_t* result = nullptr;
    const int status = praktor_yaml_workflow_metadata_create(node.native(), &result, &error.value);
    error.check(status);
    return result;
}
}
