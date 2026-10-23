#include "task_yaml_internal.hpp"
#include "yml/yaml_contract.h"

#include <new>
#include <stdexcept>
#include <string>

namespace TaskYamlDetail {
namespace {
struct ContractFieldError {
    praktor_yaml_field_error value{};
    ~ContractFieldError() { praktor_yaml_field_error_dispose(&value); }
    ContractFieldError() = default;
    ContractFieldError(const ContractFieldError&) = delete;
    ContractFieldError& operator=(const ContractFieldError&) = delete;
    void check(int status) const {
        if (status == SALTS_OK) return;
        if (value.origin == PRAKTOR_YAML_FAILURE_ALLOCATION) throw std::bad_alloc();
        throw std::runtime_error(value.text ? std::string(value.text, tstr_len(value.text))
                                            : std::string("Failed to parse workflow contract fields"));
    }
};
} // namespace

json_value_t* read_workflow_contract_fields_or_throw(
    const YamlNodeRef& node, bool output) {
    ContractFieldError error;
    json_value_t* result = nullptr;
    const int status = praktor_yaml_contract_fields_create(
        node.native(), output, &result, &error.value);
    error.check(status);
    return result;
}
} // namespace TaskYamlDetail
