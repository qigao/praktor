#ifndef PRAKTOR_LOGGING_HPP
#define PRAKTOR_LOGGING_HPP

#include <tlog.h>

// Direct mapping to tlog macros
#define logd(...) TLOG_DEBUG(__VA_ARGS__)
#define logi(...) TLOG_INFO(__VA_ARGS__)
#define logw(...) TLOG_WARN(__VA_ARGS__)
#define loge(...) TLOG_ERROR(__VA_ARGS__)

#endif // PRAKTOR_LOGGING_HPP
