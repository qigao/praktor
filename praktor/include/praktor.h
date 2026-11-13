#ifndef PRAKTOR_V3_API_H
#define PRAKTOR_V3_API_H
#include <stddef.h>
#include <cmeta/data.h>
#ifdef __cplusplus
#error "Praktor is strictly C11; no C++ execution model"
#endif
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
typedef enum praktor_runtime_status {
    PRAKTOR_RUNTIME_OK=0,
    PRAKTOR_RUNTIME_FAILED=1,
    PRAKTOR_RUNTIME_CANCELLED=2,
    PRAKTOR_RUNTIME_DEADLINE=3,
    PRAKTOR_RUNTIME_UNSUPPORTED=-1,
    PRAKTOR_RUNTIME_INVALID=-2,
    PRAKTOR_RUNTIME_RESOURCE=-3
} praktor_runtime_status;
typedef struct praktor_runtime_limits {
    int max_tasks;
    int value_capacity;
    int timeout_ms;
} praktor_runtime_limits;
typedef int (*praktor_cancel_probe)(void *context);
typedef struct praktor_runtime_result {
    praktor_runtime_status status;
    char *json;
    size_t json_size;
    char error[256];
} praktor_runtime_result;

typedef struct praktor_execute_request {
    const char *yaml;
    size_t yaml_size;
    praktor_runtime_limits limits;
    praktor_cancel_probe is_cancelled;
    void *cancel_context;
} praktor_execute_request;
/* Executes borrowed inline YAML. Explicit services references read independent
 * YAML declaration files relative to the caller's current directory. */
PRAKTOR_API praktor_runtime_status praktor_execute(
    const praktor_execute_request *request,praktor_runtime_result *result);
/* Execute one named task and its transitive dependencies. Other tasks appear
 * as skipped. Existing request/result layouts and all-task entrypoint stay
 * unchanged. task is borrowed, nonempty; unknown names return INVALID. */
PRAKTOR_API praktor_runtime_status praktor_execute_task(
    const praktor_execute_request *request,const char *task,praktor_runtime_result *result);
PRAKTOR_API void praktor_release_result(praktor_runtime_result *result);
PRAKTOR_API praktor_runtime_limits praktor_default_limits(void);
PRAKTOR_API const cmeta_data_desc *praktor_config_reflection(void);
/* Load up to 32 root dotEnv files from borrowed YAML, relative to the current
 * directory. Later files overwrite earlier/process values. This explicitly
 * mutates process-global environment: serialize it with other environment
 * access. No tasks run. Prior files are not rolled back if a later file fails.
 * Returns OK or INVALID (invalid input or loading/preparation failure).
 * Example: praktor_load_environment(yaml, size), then praktor_execute(...).
 * praktor_execute itself never loads dotenv files or mutates the environment. */
PRAKTOR_API praktor_runtime_status praktor_load_environment(const char *yaml,size_t yaml_size);
#endif
