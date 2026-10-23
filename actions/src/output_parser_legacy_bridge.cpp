#include "actions/output_parser.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <regex>
#include "core/output_scan.h"

#include <json_parser.h>

namespace actions {

namespace {

struct JsonDocumentDeleter {
    void operator()(json_value_t* document) const noexcept { json_free(document); }
};

struct SerializedJsonDeleter {
    void operator()(char* text) const noexcept { json_serialize_free(text); }
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
    json_value_t* parsed_document = json_parse(input.data(), input.size());
    if (!parsed_document) {
        return false;
    }
    std::unique_ptr<json_value_t, JsonDocumentDeleter> document(parsed_document);

    const std::string expression = normalizeJsonPath(trimCopy(path));
    const json_value_t* value = json_path_get(document.get(), expression.c_str());
    if (!value) {
        return false;
    }

    if (json_type(value) == JSON_STRING) {
        const char* text = json_string(value);
        output.assign(text ? text : "", json_string_len(value));
    } else {
        size_t length = 0;
        std::unique_ptr<char, SerializedJsonDeleter> serialized(
            json_serialize(value, &length));
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
    size_t offset = 0;
    praktor_output_span line;
    const praktor_output_span source{input.data(), input.size()};

    std::regex filter_re;
    bool use_filter = !filter_pattern.empty();

    if (use_filter) {
        try {
            filter_re = std::regex(filter_pattern);
        } catch (const std::regex_error&) {
            use_filter = false;
        }
    }

    while (praktor_output_next_line(source, &offset, &line) > 0) {
        std::string text(line.data, line.size);
        if (!use_filter || std::regex_search(text, filter_re)) {
            lines.push_back(std::move(text));
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

    size_t offset = 0;
    praktor_output_pair pair;
    int status;
    while ((status = praktor_output_next_pair(
        {input.data(), input.size()}, {delimiter.data(), delimiter.size()},
        {line_separator.data(), line_separator.size()}, &offset, &pair)) > 0) {
        result[std::string(pair.key.data, pair.key.size)] =
            std::string(pair.value.data, pair.value.size);
    }
    if (status < 0) throw std::invalid_argument("ParseKeyValue requires a nonempty line separator");

    return result;
}

bool OutputParser::contains(
    const std::string& input,
    const std::string& search
) {
    return praktor_output_contains({input.data(), input.size()},
                                  {search.data(), search.size()}) != 0;
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
