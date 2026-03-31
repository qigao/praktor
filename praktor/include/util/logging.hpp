#ifndef PRAKTOR_LOGGING_HPP
#define PRAKTOR_LOGGING_HPP

#include <tlog.h>
#include <string>
#include <string_view>

// Direct mapping to tlog macros
#define logd(...) TLOG_DEBUG(__VA_ARGS__)
#define logi(...) TLOG_INFO(__VA_ARGS__)
#define logw(...) TLOG_WARN(__VA_ARGS__)
#define loge(...) TLOG_ERROR(__VA_ARGS__)

namespace Praktor::Logging {

inline bool g_verbose_enabled = false;
inline bool g_color_enabled = false;

inline void configure(bool verbose, bool use_color) {
    g_verbose_enabled = verbose;
    g_color_enabled = use_color;
}

inline bool isVerboseEnabled() {
    return g_verbose_enabled;
}

inline const char* color(std::string_view name) {
    if (!g_color_enabled) {
        return "";
    }

    if (name == "muted") return "\x1b[90m";
    if (name == "task") return "\x1b[96m";
    if (name == "running") return "\x1b[34m";
    if (name == "success") return "\x1b[32m";
    if (name == "warn") return "\x1b[33m";
    if (name == "error") return "\x1b[31m";
    return "";
}

inline const char* reset() {
    return g_color_enabled ? "\x1b[0m" : "";
}

inline void emitConsoleEvent(std::string_view message) {
    TLOG_INFO("{}", std::string(message));
}

inline void printTaskStatus(std::string_view task_name, std::string_view status) {
    std::string message = "__TASK__:";
    message += std::string(task_name);
    message += ":";
    message += std::string(status);
    emitConsoleEvent(message);
}

inline void printWorkflowStatus(std::string_view status) {
    std::string message = "__WORKFLOW__:";
    message += std::string(status);
    emitConsoleEvent(message);
}

inline void printScriptMessage(std::string_view message) {
    std::string event = "__SCRIPT__:";
    event += std::string(message);
    emitConsoleEvent(event);
}

} // namespace Praktor::Logging

#endif // PRAKTOR_LOGGING_HPP
