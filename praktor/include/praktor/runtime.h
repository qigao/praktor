#ifndef PRAKTOR_RUNTIME_H
#define PRAKTOR_RUNTIME_H
#include "praktor.h"
#include <stdint.h>
#include <cmeta/data.h>
#ifdef __cplusplus
#error "Praktor runtime is strictly C11; no C++ execution model"
#endif
#define PRAKTOR_RUNTIME_ABI 1u

praktor_runtime_limits praktor_runtime_limits_default(void);
const cmeta_data_desc *praktor_runtime_limits_reflection(void);
praktor_runtime_status praktor_runtime_execute_yaml(
    const char *yaml,size_t yaml_size,const praktor_runtime_limits *limits,
    praktor_cancel_probe cancel,void *cancel_context,
    praktor_runtime_result *out);
void praktor_runtime_result_destroy(praktor_runtime_result *result);
#endif
