#pragma once

#include <stdexcept>   // For std::runtime_error
#include <string>
#include <vector>

// Platform-specific headers
#ifdef _WIN32
#include <windows.h>
#ifndef MAX_PATH
#define MAX_PATH 260   // Define MAX_PATH if not already defined (e.g., for some MinGW setups)
#endif
#elif __linux__
#include <limits.h>   // For PATH_MAX
#include <unistd.h>   // For readlink
#elif __APPLE__
#include <mach-o/dyld.h>   // For _NSGetExecutablePath
// For path_buffer allocation, _NSGetExecutablePath will tell us the required size.
#else
// For other OS, this function will throw an error.
// You might consider a fallback using argv[0] if available,
// but it's not guaranteed to be an absolute or canonical path.
#endif

/**
 * @brief Gets the full path to the current executable.
 * @return std::string The full path to the executable.
 * @throws std::runtime_error if the path cannot be determined or on an unsupported OS.
 */
std::string getCurrentExecutablePath() {
#ifdef _WIN32
    std::vector<char> buffer(MAX_PATH);
    DWORD current_buffer_size = static_cast<DWORD>(buffer.size());
    DWORD path_len = GetModuleFileNameA(NULL, buffer.data(), current_buffer_size);

    // Loop if the buffer was too small.
    // path_len will be equal to current_buffer_size if the buffer is too small and the path was truncated.
    // path_len will be less than current_buffer_size if successful.
    // path_len will be 0 if the function fails.
    while (path_len == current_buffer_size) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            buffer.resize(buffer.size() * 2);
            current_buffer_size = static_cast<DWORD>(buffer.size());
            path_len = GetModuleFileNameA(NULL, buffer.data(), current_buffer_size);
        } else {
            // Some other error occurred, or buffer is huge and path is exactly that size (highly unlikely)
            throw std::runtime_error(
                "Failed to get executable path (Windows), unexpected error or buffer issue. WinAPI Error: " +
                std::to_string(GetLastError()));
        }
    }

    if (path_len == 0) {
        throw std::runtime_error("Failed to get executable path (Windows). WinAPI Error: " +
                                 std::to_string(GetLastError()));
    }
    // path_len is the length of the string copied, not including the terminating null character.
    return std::string(buffer.data(), path_len);

#elif __linux__
    std::vector<char> buffer(PATH_MAX);
    // readlink does not guarantee null-termination, so we specify buffer.size() - 1
    // to leave space for a null terminator.
    ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (len != -1) {
        buffer[len] = '\0';   // Manually null-terminate
        return std::string(buffer.data());
    } else {
        throw std::runtime_error("Failed to get executable path (Linux). errno: " + std::to_string(errno));
    }

#elif __APPLE__
    uint32_t bufsize = 0;
    // First call with a null buffer to get the required buffer size.
    // _NSGetExecutablePath returns -1 and sets bufsize if the buffer is too small.
    if (_NSGetExecutablePath(nullptr, &bufsize) != -1) {
        // This is unexpected as the first call should return -1.
        throw std::runtime_error("Failed to get executable path buffer size (macOS) - _NSGetExecutablePath did not "
                                 "return -1 on first call.");
    }
    if (bufsize == 0) {   // Should not happen for a valid executable
        throw std::runtime_error("Failed to get executable path buffer size (macOS) - reported size is zero.");
    }

    std::vector<char> path_buffer(bufsize);
    // Second call to actually get the path.
    // bufsize is an in-out parameter; it should contain the size of path_buffer.
    if (_NSGetExecutablePath(path_buffer.data(), &bufsize) == 0) {
        // The path copied to path_buffer is null-terminated by the system.
        return std::string(path_buffer.data());
    } else {
        throw std::runtime_error("Failed to get executable path (macOS) on second call to _NSGetExecutablePath.");
    }
#else
    throw std::runtime_error("Unsupported OS: Cannot determine executable path.");
#endif
}

// Example of how you might also get the directory containing the executable:
std::string getCurrentExecutableDirectory() {
    std::string exePath = getCurrentExecutablePath();
    // Find the last path separator
    size_t lastSlashPos = exePath.find_last_of("/\\");
    if (std::string::npos != lastSlashPos) { return exePath.substr(0, lastSlashPos); }
    // If no separator is found, it might mean the path is just the filename
    // (e.g., if CWD is where the exe is, and it was invoked by name only).
    // In such a case, the "directory" could be considered the current working directory,
    // or an empty string if you strictly want the directory part of the given path.
    return "";   // Or consider returning "." for the current directory
}
