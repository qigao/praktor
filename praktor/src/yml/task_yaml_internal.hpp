#pragma once

#include "yaml_document.hpp"
#include "yml/task_yaml.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace TaskYamlDetail {

inline std::string ltrim_copy(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return s;
}

inline std::string rtrim_copy(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

inline std::string trim_copy(std::string s) {
    return rtrim_copy(ltrim_copy(std::move(s)));
}

inline std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::string normalize_lookup_key(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[noreturn]] inline void throw_parse_error(const YamlNodeRef& node,
                                           const std::string& message) {
    throw std::runtime_error("Parse error" + node.location() + ": " + message);
}

constexpr size_t MAX_BTDSL_PARAM_LENGTH = 10 * 1024;
constexpr int MAX_BTDSL_NESTING_DEPTH = 100;

inline std::string read_scalar_or_throw(const YamlNodeRef& node,
                                        const std::string& message) {
    if (!node.has_val()) {
        throw_parse_error(node, message);
    }

    std::string value;
    node >> value;
    if (value.length() > MAX_BTDSL_PARAM_LENGTH) {
        throw_parse_error(node, "Parameter value exceeds maximum length of " +
                                std::to_string(MAX_BTDSL_PARAM_LENGTH) + " bytes");
    }
    return value;
}

inline bool read_bool_or_throw(const YamlNodeRef& node, const char* field_name) {
    const std::string message = std::string("'") + field_name + "' must be a boolean";
    const std::string value = to_lower_copy(read_scalar_or_throw(node, message));
    if (value == "true" || value == "yes" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "0") {
        return false;
    }
    throw_parse_error(node, message);
}

inline void check_unknown_keys(const YamlNodeRef& node,
                               const std::unordered_set<std::string>& allowed_keys) {
    if (!node.is_map()) {
        return;
    }

    for (const auto& child : node) {
        std::string key = child.key();
        if (allowed_keys.find(key) == allowed_keys.end()) {
            throw_parse_error(child, "Unknown key: '" + key + "'");
        }
    }
}

template <typename T>
T get_optional(const YamlNodeRef& node, const char* key, T default_value) {
    if (!node.has_child(key)) {
        return default_value;
    }
    T result;
    node[key] >> result;
    return result;
}

OrchNode parse_orch_node(const YamlNodeRef& node, int depth = 0);

} // namespace TaskYamlDetail

StrList node_to_string_vector(const TaskYamlDetail::YamlNodeRef& node);
Vars node_to_string_map(const TaskYamlDetail::YamlNodeRef& node);
Each parse_each(const TaskYamlDetail::YamlNodeRef& node);
Triggers parse_triggers(const TaskYamlDetail::YamlNodeRef& node);
RunCommandParams parse_run_command_params(const TaskYamlDetail::YamlNodeRef& node);
ProgramParams parse_program_params(const TaskYamlDetail::YamlNodeRef& node);
DownloadParams parse_download_params(const TaskYamlDetail::YamlNodeRef& node);
ServiceParams parse_service_params(const TaskYamlDetail::YamlNodeRef& node);
ManagedProcessParams parse_managed_process_params(const TaskYamlDetail::YamlNodeRef& node);
UsesParams parse_uses_params(const TaskYamlDetail::YamlNodeRef& node);
DynamicTasksParams parse_dynamic_tasks_params(const TaskYamlDetail::YamlNodeRef& node);
OrchParams parse_orch_params(const TaskYamlDetail::YamlNodeRef& task_node,
                             const std::string& node_type);
TaskDefaults parse_defaults(const TaskYamlDetail::YamlNodeRef& node);
bool isBtdslControlNode(const TaskYamlDetail::YamlNodeRef& node);
bool isBtdslLeafNode(const TaskYamlDetail::YamlNodeRef& node);
OrchNode parseOrchNode(const TaskYamlDetail::YamlNodeRef& yaml, int depth = 0);
Task parse_task(const TaskYamlDetail::YamlNodeRef& node, const std::string& source_path);
Workflow parse_workflow(const TaskYamlDetail::YamlNodeRef& node,
                        const std::string& source_path);
