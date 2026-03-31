#include "btdsl/output_parser.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>

#include <jsoncons/json.hpp>

namespace btdsl {

namespace {

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

bool parsePathSegment(const std::string& segment, std::string& key, std::optional<size_t>& index) {
    key.clear();
    index.reset();

    size_t bracket = segment.find('[');
    if (bracket == std::string::npos) {
        key = segment;
        return !key.empty();
    }

    if (segment.back() != ']') {
        return false;
    }

    key = segment.substr(0, bracket);
    std::string index_str = segment.substr(bracket + 1, segment.size() - bracket - 2);
    if (index_str.empty()) {
        return false;
    }

    try {
        index = static_cast<size_t>(std::stoul(index_str));
    } catch (const std::exception&) {
        return false;
    }

    return true;
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
    try {
        jsoncons::json current = jsoncons::json::parse(input);
        std::string normalized = trimCopy(path);

        if (normalized.empty() || normalized == "$") {
            output = current.is_string() ? current.as<std::string>() : current.to_string();
            return true;
        }

        if (normalized.rfind("$.", 0) == 0) {
            normalized.erase(0, 2);
        } else if (!normalized.empty() && normalized.front() == '$') {
            normalized.erase(0, 1);
        }

        if (normalized.empty()) {
            output = current.is_string() ? current.as<std::string>() : current.to_string();
            return true;
        }

        size_t start = 0;
        while (start < normalized.size()) {
            size_t dot = normalized.find('.', start);
            std::string segment = normalized.substr(start, dot == std::string::npos ? std::string::npos : dot - start);

            std::string key;
            std::optional<size_t> index;
            if (!parsePathSegment(segment, key, index)) {
                return false;
            }

            if (!key.empty()) {
                if (!current.is_object() || !current.contains(key)) {
                    return false;
                }
                current = current.at(key);
            }

            if (index.has_value()) {
                if (!current.is_array() || index.value() >= current.size()) {
                    return false;
                }
                current = current.at(index.value());
            }

            if (dot == std::string::npos) {
                break;
            }
            start = dot + 1;
        }

        output = current.is_string() ? current.as<std::string>() : current.to_string();
        return true;
    } catch (const std::exception&) {
        return false;
    }
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

} // namespace btdsl
