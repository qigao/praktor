#include "task_yaml_internal.hpp"
#include "yml/yaml_task_metadata.h"
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
namespace TaskYamlDetail {
namespace {
struct TaskMetadataError {
    praktor_yaml_field_error value{};
    ~TaskMetadataError() { praktor_yaml_field_error_dispose(&value); }
    TaskMetadataError() = default;
    TaskMetadataError(const TaskMetadataError&) = delete;
    TaskMetadataError& operator=(const TaskMetadataError&) = delete;
    void check(int status) const {
        if (status == SALTS_OK) return;
        if (value.origin == PRAKTOR_YAML_FAILURE_ALLOCATION) throw std::bad_alloc();
        throw std::runtime_error(value.text ? std::string(value.text, tstr_len(value.text))
                                            : std::string("Failed to parse task metadata"));
    }
};
}
json_value_t* read_task_metadata_or_throw(const YamlNodeRef& node) {
    TaskMetadataError error;
    json_value_t* result = nullptr;
    const int status = praktor_yaml_task_metadata_create(node.native(), &result, &error.value);
    error.check(status);
    return result;
}
std::optional<std::string> read_task_script_or_throw(const YamlNodeRef& node) {
    TaskMetadataError error;
    bool present = false;
    tstr text = nullptr;
    const int status = praktor_yaml_task_script_read(node.native(), &present, &text, &error.value);
    error.check(status);
    if (!present) return std::nullopt;
    std::string result;
    try { result.assign(text, tstr_len(text)); }
    catch (...) { tstr_free(text); throw; }
    tstr_free(text);
    return result;
}
}
