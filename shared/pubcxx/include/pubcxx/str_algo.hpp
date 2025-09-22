#ifndef MY_STR_HPP
#define MY_STR_HPP
#include <string>
#include <vector>

inline std::string join_range(std::vector<std::string> const& strings, std::string const& delimiter) {
    if (strings.empty()) { return ""; }

    if (strings.size() == 1) { return strings[0]; }

    // Pre-calculate total size
    size_t total_size = 0;
    for (auto const& str : strings) { total_size += str.size(); }
    total_size += delimiter.size() * (strings.size() - 1);

    // Pre-allocate result string
    std::string result;
    result.reserve(total_size);

    // Join strings
    result += strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter;
        result += strings[i];
    }

    return result;
}

#endif   // MY_STR_HPP
