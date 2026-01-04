#ifndef __STRING_UTILS_HPP__
#define __STRING_UTILS_HPP__

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Prakter::util {

/**
 * @brief Trim whitespace from both ends of a string
 * @param str The string to trim
 * @return A new string with leading and trailing whitespace removed
 */
inline std::string trim(std::string_view str) {
    auto begin = str.begin();
    auto end = str.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

/**
 * @brief Trim whitespace from the left side of a string
 * @param str The string to trim
 * @return A new string with leading whitespace removed
 */
inline std::string trimLeft(std::string_view str) {
    auto begin = str.begin();
    auto end = str.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }

    return std::string(begin, end);
}

/**
 * @brief Trim whitespace from the right side of a string
 * @param str The string to trim
 * @return A new string with trailing whitespace removed
 */
inline std::string trimRight(std::string_view str) {
    auto begin = str.begin();
    auto end = str.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }

    return std::string(begin, end);
}

inline std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

} // namespace Prakter::util

#endif // __STRING_UTILS_HPP__
