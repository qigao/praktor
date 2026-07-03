#pragma once

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

[[noreturn]] inline void throw_parse_error(const ryml::ConstNodeRef& /*node*/,
                                           const std::string& message) {
    throw std::runtime_error("Parse error: " + message);
}

constexpr size_t MAX_BTDSL_PARAM_LENGTH = 10 * 1024;
constexpr int MAX_BTDSL_NESTING_DEPTH = 100;

inline std::string read_scalar_or_throw(const ryml::ConstNodeRef& node,
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

inline void check_unknown_keys(const ryml::ConstNodeRef& node,
                               const std::unordered_set<std::string>& allowed_keys) {
    if (!node.is_map()) {
        return;
    }

    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        if (allowed_keys.find(key) == allowed_keys.end()) {
            throw_parse_error(child, "Unknown key: '" + key + "'");
        }
    }
}

OrchNode parse_orch_node(const ryml::ConstNodeRef& node, int depth = 0);

} // namespace TaskYamlDetail
