#include "projection.hpp"

#include <idl_contract.h>
#include <schema_cmeta.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace Praktor::Schema {
namespace {

void merge(WorkflowValue& destination, const WorkflowValue& source) {
    for (const auto& member : source.object_range()) {
        const auto& key = member.key();
        if (!destination.contains(key)) {
            destination[key] = member.value();
        } else if (key == "required" && destination[key].is_array()) {
            auto required = destination[key].get();
            for (const auto& item : member.value().array_range()) {
                for (const auto& existing : required.array_range()) {
                    if (existing.as_string() == item.as_string()) {
                        throw std::runtime_error("Duplicate IDL/editor required field: " + item.as_string());
                    }
                }
                required.push_back(item);
            }
            destination[key] = std::move(required);
        } else if (destination[key].is_object() && member.value().is_object()) {
            auto child = destination[key].get();
            merge(child, member.value());
            destination[key] = std::move(child);
        } else {
            throw std::runtime_error("Editor overlay duplicates IDL-owned keyword: " + key);
        }
    }
}

WorkflowValue fieldShape(const IdlContract& contract, const IdlField& field) {
    schema_cmeta_field_type resolved{};
    if (!schema_cmeta_field_resolve(&contract, &field, &resolved)) {
        throw std::runtime_error("Unresolved IDL field: " + std::string(field.name));
    }
    auto shape = WorkflowValue::object();
    switch (resolved.kind) {
    case CMETA_DATA_BOOL: shape["type"] = "boolean"; break;
    case CMETA_DATA_SINT:
    case CMETA_DATA_UINT: shape["type"] = "integer"; break;
    case CMETA_DATA_FLOAT: shape["type"] = "number"; break;
    case CMETA_DATA_STRING: shape["type"] = "string"; break;
    case CMETA_DATA_STRUCT:
        if (!idl_contract_find_data(&contract, field.type_name)) {
            throw std::runtime_error("Unknown IDL record reference");
        }
        shape["$ref"] = std::string_view(field.type_name) == "Workflow"
            ? "#" : std::string("#/$defs/") + field.type_name;
        break;
    case CMETA_DATA_SEQUENCE: {
        if (field.collection_kind != IDL_COLLECTION_LIST || !field.inner_type) {
            throw std::runtime_error("Only IDL lists are supported by the editor projection");
        }
        IdlField element{};
        element.name = field.name;
        element.type_name = field.inner_type;
        shape["type"] = "array";
        shape["items"] = fieldShape(contract, element);
        break;
    }
    default:
        throw std::runtime_error("Unsupported IDL shape in editor projection: " + std::string(field.name));
    }
    // Presence and validation belong to IDL, not the native storage descriptor.
    if (field.nullable) {
        throw std::runtime_error("Nullable IDL fields need an explicit editor projection");
    }
    for (size_t i = 0; i < field.constraint_count; ++i) {
        const auto& constraint = field.constraints[i];
        const std::string_view kind(constraint.kind);
        if (kind == "min" || kind == "max") {
            shape[kind == "min" ? "minimum" : "maximum"] = WorkflowValue::parse(constraint.value);
        } else if (kind == "size") {
            const bool string = resolved.kind == CMETA_DATA_STRING;
            if (!string && resolved.kind != CMETA_DATA_SEQUENCE) {
                throw std::runtime_error("Size constraint requires a string or list");
            }
            if (constraint.minimum) {
                shape[string ? "minLength" : "minItems"] = WorkflowValue::parse(constraint.minimum);
            }
            if (constraint.maximum) {
                shape[string ? "maxLength" : "maxItems"] = WorkflowValue::parse(constraint.maximum);
            }
        } else if (kind == "pattern") {
            shape["pattern"] = constraint.pattern;
        } else {
            throw std::runtime_error("Unsupported IDL constraint: " + std::string(kind));
        }
    }
    if (field.default_value) {
        shape["default"] = resolved.kind == CMETA_DATA_STRING
            ? WorkflowValue(field.default_value) : WorkflowValue::parse(field.default_value);
    }
    return shape;
}

}

WorkflowValue project(std::string_view idl, WorkflowValue editor) {
    IdlContract* raw = nullptr;
    IdlDiagnostic diagnostic = IDL_DIAGNOSTIC_INIT;
    const bool parsed = idl_contract_parse(idl.data(), idl.size(), &raw, &diagnostic) != 0;
    std::unique_ptr<IdlContract, decltype(&idl_contract_destroy)> contract(raw, idl_contract_destroy);
    if (!parsed || !contract) {
        throw std::runtime_error(std::string("Invalid workflow IDL: ") + diagnostic.message);
    }
    if (!editor.is_object() || !editor["$defs"].is_object() ||
        !idl_contract_find_data(contract.get(), "Workflow")) {
        throw std::runtime_error("Workflow IDL and editor definitions are required");
    }
    if (contract->service_count || contract->channel_count || contract->component_count) {
        throw std::runtime_error("Editor projection accepts data contracts only");
    }
    for (size_t i = 0; i < contract->data_count; ++i) {
        const auto& declaration = contract->data[i];
        if (declaration.kind != IDL_DATA_MESSAGE || declaration.annotation_count) {
            throw std::runtime_error("Editor projection requires unannotated IDL messages");
        }
        auto schema = WorkflowValue::object({{"type", "object"}});
        auto properties = WorkflowValue::object();
        auto required = WorkflowValue::array();
        for (size_t j = 0; j < declaration.field_count; ++j) {
            const auto& field = declaration.fields[j];
            const char* name = field.name;
            if (idl_annotation_count(field.annotations, field.annotation_count, "name") > 1) {
                throw std::runtime_error("An IDL field may have only one projected name");
            }
            for (size_t k = 0; k < field.annotation_count; ++k) {
                const auto& annotation = field.annotations[k];
                if (std::string_view(annotation.name) != "name" || annotation.argument_count != 1) {
                    throw std::runtime_error("Unsupported IDL field annotation");
                }
                name = idl_annotation_argument(&annotation, 0);
            }
            if (!name || !*name || properties.contains(name)) {
                throw std::runtime_error("Duplicate or empty projected IDL field name");
            }
            properties[name] = fieldShape(*contract, field);
            if (!field.optional) required.push_back(name);
        }
        schema["properties"] = std::move(properties);
        if (required.size()) schema["required"] = std::move(required);
        if (std::string_view(declaration.name) == "Workflow") {
            merge(editor, schema);
        } else {
            if (!editor["$defs"].contains(declaration.name)) {
                throw std::runtime_error("IDL message has no editor policy: " + std::string(declaration.name));
            }
            auto definition = editor["$defs"][declaration.name].get();
            merge(definition, schema);
            editor["$defs"][declaration.name] = std::move(definition);
        }
    }
    return editor;
}

}
