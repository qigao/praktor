#pragma once

#include "yaml_document.hpp"
#include "yml/task_yaml.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
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

inline std::optional<bool> parse_force_terminate_integer_compat(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }

    std::size_t position = 0;
    bool negative = false;
    if (value[position] == '+' || value[position] == '-') {
        negative = value[position] == '-';
        if (++position == value.size()) {
            return std::nullopt;
        }
    }

    int base = 10;
    if (position + 1 < value.size() && value[position] == '0') {
        switch (value[position + 1]) {
        case 'b': base = 2; break;
        case 'o': base = 8; break;
        case 'x': base = 16; break;
        default: break;
        }
        if (base != 10) {
            position += 2;
        }
    }
    if (position == value.size() ||
        (base == 10 && !std::isdigit(static_cast<unsigned char>(value[position])))) {
        return std::nullopt;
    }

    std::string digits;
    digits.reserve(value.size() - position);
    for (; position < value.size(); ++position) {
        if (value[position] != '_') {
            digits.push_back(value[position]);
        }
    }
    if (digits.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(
        digits.data(), digits.data() + digits.size(), parsed, base);
    if (error != std::errc{} || end != digits.data() + digits.size() ||
        (negative && parsed != 0)) {
        return std::nullopt;
    }
    if (parsed == 0) {
        return false;
    }
    if (parsed == 1) {
        return true;
    }
    return std::nullopt;
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

inline bool read_force_terminate_or_throw(const YamlNodeRef& node) {
    constexpr const char* field_name = "force_terminate";
    const std::string message = std::string("'") + field_name + "' must be a boolean";
    const std::string value = to_lower_copy(read_scalar_or_throw(node, message));
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        return false;
    }
    if (const auto semantic_value = node.bool_integer_value(); semantic_value.has_value()) {
        return *semantic_value;
    }
    if (const auto compatible_value = parse_force_terminate_integer_compat(value);
        compatible_value.has_value()) {
        return *compatible_value;
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
