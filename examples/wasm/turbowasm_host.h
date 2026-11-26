#ifndef PRAKTOR_EXAMPLE_TURBOWASM_HOST_H
#define PRAKTOR_EXAMPLE_TURBOWASM_HOST_H
#include <praktor.h>

enum { MAX_APPROVED_MODULES = 16, MAX_MODULE_BYTES = 256 * 1024, MAX_WASM_NAME = 128 };

/* The policy owns the verified bytes, which stay immutable through execution. */
typedef struct approved_wasm {
    char module[MAX_WASM_NAME + 1], entry[MAX_WASM_NAME + 1];
    unsigned char *bytes;
    size_t size;
    unsigned int max_fuel, max_timeout_ms;
} approved_wasm;

typedef struct approved_wasm_modules {
    approved_wasm modules[MAX_APPROVED_MODULES];
    size_t count;
} approved_wasm_modules;

praktor_wasm_host make_turbowasm_host(approved_wasm_modules *approved);
#endif
