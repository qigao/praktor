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

/* Callbacks run on the workflow owner and may call check only during step.
 * bind resolves an approved logical module and validates its export and
 * requested limits without running guest code. The reported timeout is the
 * service timeout in milliseconds; the workflow can end an invocation sooner.
 * start creates one invocation without running guest code.
 * On success each returns a non-NULL handle, owned until unbind or release.
 * On failure the callback cleans up its own work and returns a runtime status.
 * Input is borrowed and valid through release. Output capacity excludes NUL.
 * A successful step with done == 0 yields. With done == 1 it returns one
 * UTF-8 JSON document in output. Other statuses terminate the invocation.
 * The host enforces cumulative fuel and calls check during execution. Each
 * step must return promptly, release must synchronously reclaim a yielded job.
 * No callback may reenter Praktor. */
typedef struct praktor_wasm_host {
    void *context;
    praktor_runtime_status (*bind)(void *context, const char *module, const char *entry,
                                   unsigned int fuel, unsigned int timeout_ms,
                                   void **binding);
    void (*unbind)(void *context, void *binding);
    praktor_runtime_status (*start)(void *context, void *binding, unsigned int fuel,
                                    const char *input, size_t input_size, void **job);
    praktor_runtime_status (*step)(void *context, void *job,
                                   praktor_runtime_status (*check)(void *), void *check_context,
                                   char *output, size_t capacity, size_t *size, int *done);
    void (*release)(void *context, void *job);
} praktor_wasm_host;

typedef struct praktor_event_field {
    const char *name;
    const char *value;
} praktor_event_field;
typedef struct praktor_host_event {
    const char *name;
    const praktor_event_field *fields;
    size_t field_count;
} praktor_host_event;
/* Called on the workflow thread before a BT tick, for one live external-event
 * chart. task is NULL for the document chart. Return 1 with one event, 0 for
 * none, or a negative error. Do not block or reenter execution. Names and
 * values are NUL-terminated UTF-8 text, borrowed until the next poll or
 * execution return. A payload has at most 32 fields and 8 KiB of values. */
typedef int (*praktor_event_poll)(void *context,const char *task,praktor_host_event *event);
/* Executes borrowed inline YAML. Explicit services and includes references
 * read independent YAML files relative to the caller's current directory.
 * Dynamic tasks expand before task selection from task variables, document
 * variables or the inherited environment. The task limit covers expansion. */
PRAKTOR_API praktor_runtime_status praktor_execute(
    const praktor_execute_request *request,praktor_runtime_result *result);
/* Execute one named task and its transitive dependencies. Other tasks appear
 * as skipped. Existing request/result layouts and all-task entrypoint stay
 * unchanged. task is borrowed, nonempty; unknown names return INVALID. */
PRAKTOR_API praktor_runtime_status praktor_execute_task(
    const praktor_execute_request *request,const char *task,praktor_runtime_result *result);
/* A non-NULL poll binds source: external declarations. task optionally
 * selects one task and its dependencies, as in praktor_execute_task. */
PRAKTOR_API praktor_runtime_status praktor_execute_with_events(
    const praktor_execute_request *request,const char *task,praktor_event_poll poll,
    void *event_context,praktor_runtime_result *result);
/* Execute with a borrowed WASM host. task is optional, as with
 * praktor_execute_with_events. WASM declarations require this host. */
PRAKTOR_API praktor_runtime_status praktor_execute_with_wasm(
    const praktor_execute_request *request,const char *task,const praktor_wasm_host *host,
    praktor_runtime_result *result);
/* Check YAML and declarations for the whole workflow with request.limits.
 * Includes and services read local files. Dynamic tasks resolve values from
 * the document and inherited environment. No dotenv files, callbacks or
 * actions run. Runtime host bindings are checked during execution admission.
 * Use a zeroed result. Read its diagnostic on failure; json stays NULL. */
PRAKTOR_API praktor_runtime_status praktor_check(
    const praktor_execute_request *request,praktor_runtime_result *result);
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
