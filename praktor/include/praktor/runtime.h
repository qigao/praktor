#ifndef PRAKTOR_RUNTIME_H
#define PRAKTOR_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#include <cmeta/data.h>
#ifdef __cplusplus
#error "Praktor runtime is strictly C11; no C++ execution model"
#endif
#define PRAKTOR_C11_RUNTIME_ABI 1u
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
praktor_runtime_limits praktor_runtime_limits_default(void);
const cmeta_data_desc *praktor_runtime_limits_reflection(void);
praktor_runtime_status praktor_runtime_execute_yaml(
    const char *yaml,size_t yaml_size,const praktor_runtime_limits *limits,
    praktor_cancel_probe cancel,void *cancel_context,
    praktor_runtime_result *out);
void praktor_runtime_result_destroy(praktor_runtime_result *result);
#endif
