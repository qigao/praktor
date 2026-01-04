#ifndef __ENV_PARSER_HPP__
#define __ENV_PARSER_HPP__

#include "string_utils.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Prakter::util {

/**
 * @brief Parse a .env file and return key-value pairs
 * @param file_path Path to the .env file
 * @return Map of environment variable names to values
 * @throws std::runtime_error if file cannot be opened
 */
inline std::unordered_map<std::string, std::string> parseDotEnvFile(
    const std::filesystem::path& file_path) {

    std::unordered_map<std::string, std::string> result;
    std::ifstream input(file_path);

    if (!input.is_open()) {
        throw std::runtime_error("Failed to open .env file: " + file_path.string());
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        // Handle 'export VAR=value' syntax
        if (trimmed.rfind("export ", 0) == 0) {
            trimmed = trim(trimmed.substr(7));
        }

        // Find the equals sign
        size_t equals_pos = trimmed.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(trimmed.substr(0, equals_pos));
        std::string value = trim(trimmed.substr(equals_pos + 1));

        // Remove surrounding quotes if present
        if (value.size() >= 2) {
            bool quoted = (value.front() == '"' && value.back() == '"')
                       || (value.front() == '\'' && value.back() == '\'');
            if (quoted) {
                value = value.substr(1, value.size() - 2);
            }
        }

        if (!key.empty()) {
            result[key] = value;
        }
    }

    return result;
}

} // namespace Prakter::util

#endif // __ENV_PARSER_HPP__
