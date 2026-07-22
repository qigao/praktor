#include "actions/output_parser.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>

#include <turbo_parser.h>

namespace actions {

namespace {

struct JsonDocumentDeleter {
    void operator()(turbo_json_doc_t* document) const noexcept {
        turbo_free_json(&document);
    }
};

struct SerializedJsonDeleter {
    void operator()(char* text) const noexcept { turbo_json_serialize_free(text); }
};

std::string trimCopy(const std::string& input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
        ++start;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
        --end;
    }

    return input.substr(start, end - start);
}

std::string normalizeJsonPath(std::string_view path) {
    if (path.empty()) {
        return "$";
    }
    if (path.front() == '$') {
        return std::string(path);
    }
    if (path.front() == '[') {
        return "$" + std::string(path);
    }
    return "$." + std::string(path);
}

} // namespace

bool OutputParser::parseRegex(
    const std::string& input,
    const std::string& pattern,
    std::vector<std::string>& captures
) {
    try {
        std::regex re(pattern);
        std::smatch match;

        if (std::regex_search(input, match, re)) {
            captures.clear();
            for (size_t i = 1; i < match.size(); i++) {
                captures.push_back(match[i].str());
            }
            return true;
        }
    } catch (const std::regex_error&) {
        return false;
    }

    return false;
}

bool OutputParser::parseJson(
    const std::string& input,
    const std::string& path,
    std::string& output
) {
    turbo_json_doc_t* parsed_document = nullptr;
    if (turbo_parse_json(reinterpret_cast<const uint8_t*>(input.data()), input.size(),
                         &parsed_document) != 0 || !parsed_document) {
        return false;
    }
    std::unique_ptr<turbo_json_doc_t, JsonDocumentDeleter> document(parsed_document);

    const std::string expression = normalizeJsonPath(trimCopy(path));
    const json_value_t* value = turbo_json_path_get(document.get(), expression.c_str());
    if (!value) {
        return false;
    }

    if (turbo_json_type(value) == TURBO_JSON_STRING) {
        const char* text = turbo_json_string(value);
        output.assign(text ? text : "", turbo_json_string_len(value));
    } else {
        size_t length = 0;
        std::unique_ptr<char, SerializedJsonDeleter> serialized(
            turbo_json_serialize(value, &length));
        if (!serialized) {
            return false;
        }
        output.assign(serialized.get(), length);
    }
    return true;
}

std::vector<std::string> OutputParser::parseLines(
    const std::string& input,
    const std::string& filter_pattern
) {
    std::vector<std::string> lines;
    std::istringstream stream(input);
    std::string line;

    std::regex filter_re;
    bool use_filter = !filter_pattern.empty();

    if (use_filter) {
        try {
            filter_re = std::regex(filter_pattern);
        } catch (const std::regex_error&) {
            use_filter = false;
        }
    }

    while (std::getline(stream, line)) {
        if (use_filter) {
            if (std::regex_search(line, filter_re)) {
                lines.push_back(line);
            }
        } else {
            lines.push_back(line);
        }
    }

    return lines;
}

std::map<std::string, std::string> OutputParser::parseKeyValue(
    const std::string& input,
    const std::string& delimiter,
    const std::string& line_separator
) {
    std::map<std::string, std::string> result;

    size_t pos = 0;
    while (pos < input.size()) {
        // Find next line separator
        size_t line_end = input.find(line_separator, pos);
        if (line_end == std::string::npos) {
            line_end = input.size();
        }

        std::string line = input.substr(pos, line_end - pos);

        // Find delimiter
        size_t delim_pos = line.find(delimiter);
        if (delim_pos != std::string::npos) {
            std::string key = line.substr(0, delim_pos);
            std::string value = line.substr(delim_pos + delimiter.size());

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            result[key] = value;
        }

        pos = line_end + line_separator.size();
    }

    return result;
}

bool OutputParser::contains(
    const std::string& input,
    const std::string& search
) {
    return input.find(search) != std::string::npos;
}

int OutputParser::countMatches(
    const std::string& input,
    const std::string& pattern
) {
    try {
        std::regex re(pattern);
        auto begin = std::sregex_iterator(input.begin(), input.end(), re);
        auto end = std::sregex_iterator();
        return static_cast<int>(std::distance(begin, end));
    } catch (const std::regex_error&) {
        return 0;
    }
}

} // namespace actions
