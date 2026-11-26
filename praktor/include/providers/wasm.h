#ifndef PRAKTOR_WASM_SERVICE_H
#define PRAKTOR_WASM_SERVICE_H
#include "providers/execution.h"
#include <stdint.h>

typedef struct praktor_wasm_service {
    const praktor_wasm_host *host;
    void *binding;
    const char *output_key;
    int fuel;
    uint64_t timeout_ns, workflow_deadline;
    praktor_cancel_probe cancel;
    void *cancel_context;
    size_t jobs;
} praktor_wasm_service;

praktor_execution_service praktor_wasm_service_bind(praktor_wasm_service *service);
int praktor_wasm_validate_inputs(const praktor_execution_admission *admission);
const cmeta_data_desc *praktor_wasm_service_reflection(void);
#endif
