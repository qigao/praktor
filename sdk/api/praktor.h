#ifndef PRAKTOR_V3_API_H
#define PRAKTOR_V3_API_H
#include <praktor/runtime.h>
#if defined(_WIN32)
#ifdef PRAKTOR_BUILD_SHARED
#define PRAKTOR_API __declspec(dllexport)
#else
#define PRAKTOR_API __declspec(dllimport)
#endif
#else
#define PRAKTOR_API __attribute__((visibility("default")))
#endif
#define PRAKTOR_ABI_MAJOR 3u
#define PRAKTOR_ABI_MINOR 0u
typedef struct praktor_execute_request {
    const char *yaml;
    size_t yaml_size;
    praktor_runtime_limits limits;
    praktor_cancel_probe is_cancelled;
    void *cancel_context;
} praktor_execute_request;
PRAKTOR_API praktor_runtime_status praktor_execute(
    const praktor_execute_request *request,praktor_runtime_result *result);
PRAKTOR_API void praktor_release_result(praktor_runtime_result *result);
PRAKTOR_API praktor_runtime_limits praktor_default_limits(void);
PRAKTOR_API const cmeta_data_desc *praktor_config_reflection(void);
#endif
