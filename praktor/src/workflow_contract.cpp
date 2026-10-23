#include "workflow_contract.hpp"
#include "data/workflow_contract_value.h"

#include "dag/workflow_context.hpp"
#include "util/variable_substitution.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <new>
#include <string_view>
#include <utility>

namespace Praktor::Contract {
namespace {

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [](unsigned char ch) { return std::isspace(ch) == 0; }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [](unsigned char ch) { return std::isspace(ch) == 0; }).base(), value.end());
    return value;
}

bool enumContains(const WorkflowContractField& field, const WorkflowValue& value) {
    if (field.enum_values.empty()) return true;
    WorkflowValue values = WorkflowValue::array();
    for (const auto& candidate : field.enum_values) values.push_back(candidate);
    bool matches = false;
    const int status = praktor_contract_value_in_enum(value.raw(), values.raw(), &matches);
    if (status == SALTS_ENOMEM) throw std::bad_alloc();
    if (status != SALTS_OK) throw std::runtime_error("Failed to compare workflow contract enum");
    return matches;
}

void validateFieldValue(const std::string& name,
                        const WorkflowContractField& field,
                        const WorkflowValue& value,
                        const char* surface) {
    if (!praktor_contract_value_matches_type(value.raw(), field.type.c_str())) {
        throw WorkflowContractError(
            std::string(surface) + " '" + name + "' must be " + field.type);
    }
    if (!enumContains(field, value)) {
        throw WorkflowContractError(
            std::string(surface) + " '" + name + "' is not one of the allowed enum values");
    }
}

WorkflowValue schemaForField(const WorkflowContractField& field) {
    WorkflowValue values = WorkflowValue::array();
    for (const auto& value : field.enum_values) values.push_back(value);
    json_value_t *raw = nullptr;
    const int status = praktor_contract_schema_field(
        field.type.c_str(), field.description.c_str(), !field.description.empty(),
        field.default_value ? field.default_value->raw() : nullptr,
        field.enum_values.empty() ? nullptr : values.raw(), &raw);
    if (status == SALTS_ENOMEM) throw std::bad_alloc();
    if (status != SALTS_OK || raw == nullptr)
        throw std::runtime_error("Failed to construct workflow contract schema field");
    return WorkflowValue::adoptJson(raw);
}

WorkflowValue resolveOutputValue(const std::string& name,
                                 const WorkflowContractField& field,
                                 const WorkflowContext& context) {
    if (!field.value.has_value()) {
        return context.getValueByPath("tasks.__root__.outputs." + name);
    }

    const std::string expression = trim(*field.value);
    if (expression.size() >= 4 &&
        expression.rfind("{{", 0) == 0 &&
        expression.substr(expression.size() - 2) == "}}") {
        std::string path = trim(expression.substr(2, expression.size() - 4));
        if (!path.empty() && path.find("{{") == std::string::npos &&
            path.find("}}") == std::string::npos) {
            return context.getValueByPath(path);
        }
    }

    return WorkflowValue(substituteVariables(*field.value, context));
}

} // namespace

InputValues validateAndApplyInputs(const Workflow& workflow,
                                   const InputValues& inputs) {
    if (workflow.strict_inputs) {
        for (const auto& [name, _] : inputs) {
            if (workflow.inputs.find(name) == workflow.inputs.end()) {
                throw WorkflowContractError("Unknown workflow input '" + name + "'");
            }
        }
    }

    InputValues normalized = inputs;
    for (const auto& [name, field] : workflow.inputs) {
        auto found = normalized.find(name);
        if (found == normalized.end()) {
            if (field.default_value.has_value()) {
                normalized.emplace(name, *field.default_value);
                continue;
            }
            if (field.required) {
                throw WorkflowContractError("Missing required workflow input '" + name + "'");
            }
            continue;
        }
        validateFieldValue(name, field, found->second, "Workflow input");
    }
    return normalized;
}

WorkflowValue inputSchema(const Workflow& workflow) {
    WorkflowValue schema = WorkflowValue::object();
    schema["type"] = "object";

    WorkflowValue properties = WorkflowValue::object();
    WorkflowValue required = WorkflowValue::array();
    for (const auto& [name, field] : workflow.inputs) {
        properties[name] = schemaForField(field);
        if (field.required && !field.default_value.has_value()) {
            required.push_back(name);
        }
    }
    schema["properties"] = std::move(properties);
    schema["required"] = std::move(required);
    schema["additionalProperties"] = !workflow.strict_inputs;
    return schema;
}

WorkflowValue outputSchema(const Workflow& workflow) {
    WorkflowValue schema = WorkflowValue::object();
    schema["type"] = "object";

    WorkflowValue properties = WorkflowValue::object();
    WorkflowValue required = WorkflowValue::array();
    for (const auto& [name, field] : workflow.outputs) {
        properties[name] = schemaForField(field);
        if (field.required) {
            required.push_back(name);
        }
    }
    schema["properties"] = std::move(properties);
    schema["required"] = std::move(required);
    schema["additionalProperties"] = false;
    return schema;
}

WorkflowValue projectOutputs(const Workflow& workflow,
                             const WorkflowContext& context) {
    WorkflowValue outputs = WorkflowValue::object();
    for (const auto& [name, field] : workflow.outputs) {
        WorkflowValue value;
        try {
            value = resolveOutputValue(name, field, context);
        } catch (const std::exception&) {
            if (field.required) {
                throw WorkflowContractError(
                    "Missing required workflow output '" + name + "'");
            }
            continue;
        }

        if (value.is_null() && !field.required) {
            continue;
        }
        validateFieldValue(name, field, value, "Workflow output");
        outputs[name] = std::move(value);
    }
    return outputs;
}

} // namespace Praktor::Contract
