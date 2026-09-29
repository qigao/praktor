#include "workflow_contract.hpp"

#include "dag/workflow_context.hpp"
#include "util/variable_substitution.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
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
    if (field.enum_values.empty()) {
        return true;
    }
    const std::string encoded = value.to_string();
    return std::any_of(field.enum_values.begin(), field.enum_values.end(),
        [&](const WorkflowValue& candidate) {
            return candidate.to_string() == encoded;
        });
}

void validateFieldValue(const std::string& name,
                        const WorkflowContractField& field,
                        const WorkflowValue& value,
                        const char* surface) {
    if (!valueMatchesType(value, field.type)) {
        throw WorkflowContractError(
            std::string(surface) + " '" + name + "' must be " + field.type);
    }
    if (!enumContains(field, value)) {
        throw WorkflowContractError(
            std::string(surface) + " '" + name + "' is not one of the allowed enum values");
    }
}

WorkflowValue schemaForField(const WorkflowContractField& field) {
    WorkflowValue schema = WorkflowValue::object();
    schema["type"] = field.type;
    if (!field.description.empty()) {
        schema["description"] = field.description;
    }
    if (field.default_value.has_value()) {
        schema["default"] = *field.default_value;
    }
    if (!field.enum_values.empty()) {
        WorkflowValue values = WorkflowValue::array();
        for (const auto& value : field.enum_values) {
            values.push_back(value);
        }
        schema["enum"] = std::move(values);
    }
    return schema;
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
        if (!path.empty()) {
            return context.getValueByPath(path);
        }
    }

    return WorkflowValue(substituteVariables(*field.value, context));
}

} // namespace

bool valueMatchesType(const WorkflowValue& value, const std::string& type) {
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_bool();
    if (type == "integer") return value.is_int64() || value.is_uint64();
    if (type == "number") return value.is_number();
    if (type == "array") return value.is_array();
    if (type == "object") return value.is_object();
    return false;
}

InputValues validateAndApplyInputs(const Workflow& workflow,
                                   const InputValues& inputs) {
    if (workflow.inputs.empty()) {
        return inputs;
    }

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
