#include "turbowasm_host.h"
#include <execution.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_MEMORY_BYTES = 2 * 1024 * 1024,
    MAX_ALLOCATED_BYTES = 8 * 1024 * 1024,
    MAX_TABLE_ELEMENTS = 256,
    MAX_JSON_OUTPUT = 4096,
    FUEL_PER_STEP = 10000
};

typedef struct module_binding {
    turbowasm_module module;
    turbowasm_runtime_config config;
    uint32_t function;
    size_t allocated_bytes;
} module_binding;

typedef union allocation_header {
    max_align_t alignment;
    size_t size;
} allocation_header;

typedef struct invocation {
    turbowasm_instance instance;
    turbowasm_execution execution;
    const char *input;
    size_t input_size, input_budget, output_size;
    unsigned int fuel;
    char output[MAX_JSON_OUTPUT];
} invocation;

static void *resize(void *context, void *pointer, size_t size) {
    module_binding *binding = context;
    allocation_header *old = pointer ? (allocation_header *)pointer - 1 : NULL;
    size_t previous = old ? old->size : 0;
    size_t retained = binding->allocated_bytes - previous;
    if (size > MAX_ALLOCATED_BYTES - retained ||
        sizeof(allocation_header) > MAX_ALLOCATED_BYTES - retained - size)
        return NULL;
    size_t total = size + sizeof(allocation_header);
    allocation_header *block = realloc(old, total);
    if (!block) return NULL;
    block->size = total;
    binding->allocated_bytes = retained + total;
    return block + 1;
}

static void *allocate(void *context, size_t size) {
    return resize(context, NULL, size);
}

static void deallocate(void *context, void *pointer) {
    if (!pointer) return;
    module_binding *binding = context;
    allocation_header *header = (allocation_header *)pointer - 1;
    binding->allocated_bytes -= header->size;
    free(header);
}

static praktor_runtime_status map_status(turbowasm_status status) {
    switch (status) {
    case TURBOWASM_OK: return PRAKTOR_RUNTIME_OK;
    case TURBOWASM_OUT_OF_MEMORY:
    case TURBOWASM_FUEL_EXHAUSTED: return PRAKTOR_RUNTIME_RESOURCE;
    case TURBOWASM_UNSUPPORTED: return PRAKTOR_RUNTIME_UNSUPPORTED;
    case TURBOWASM_MALFORMED_MODULE:
    case TURBOWASM_INVALID_ARGUMENT:
    case TURBOWASM_LINK_ERROR:
    case TURBOWASM_TYPE_MISMATCH: return PRAKTOR_RUNTIME_INVALID;
    default: return PRAKTOR_RUNTIME_FAILED;
    }
}

static turbowasm_status return_i32(turbowasm_value *result, size_t *count, int32_t value) {
    *result = (turbowasm_value){.kind=TURBOWASM_VALUE_I32, .as.i32=value};
    *count = 1;
    return TURBOWASM_OK;
}

static turbowasm_status input_size(void *context, turbowasm_host_call *call,
    const turbowasm_value *args, size_t argc, turbowasm_value *results,
    size_t capacity, size_t *count, turbowasm_trap *trap) {
    (void)call; (void)args; (void)argc; (void)capacity; (void)trap;
    const invocation *job = context;
    return return_i32(results, count, (int32_t)job->input_size);
}

static turbowasm_status input_read(void *context, turbowasm_host_call *call,
    const turbowasm_value *args, size_t argc, turbowasm_value *results,
    size_t capacity, size_t *count, turbowasm_trap *trap) {
    (void)argc; (void)capacity;
    invocation *job = context;
    uint32_t offset = (uint32_t)args[0].as.i32;
    uint32_t destination = (uint32_t)args[1].as.i32;
    uint32_t length = (uint32_t)args[2].as.i32;
    if (offset > job->input_size || length > job->input_size - offset ||
        length > job->input_budget)
        return TURBOWASM_TRAPPED;
    turbowasm_status status = turbowasm_host_call_memory_write64(
        call, 0, destination, job->input + offset, length, trap);
    if (status != TURBOWASM_OK) return status;
    job->input_budget -= length;
    return return_i32(results, count, 0);
}

static turbowasm_status output_write(void *context, turbowasm_host_call *call,
    const turbowasm_value *args, size_t argc, turbowasm_value *results,
    size_t capacity, size_t *count, turbowasm_trap *trap) {
    (void)argc; (void)capacity;
    invocation *job = context;
    uint32_t address = (uint32_t)args[0].as.i32;
    uint32_t length = (uint32_t)args[1].as.i32;
    if (job->output_size || !length || length > sizeof job->output)
        return TURBOWASM_TRAPPED;
    turbowasm_status status = turbowasm_host_call_memory_read64(
        call, 0, address, job->output, length, trap);
    if (status != TURBOWASM_OK) return status;
    job->output_size = length;
    return return_i32(results, count, 0);
}

static const struct host_import {
    const char *name;
    uint32_t params;
    turbowasm_host_function_fn call;
} imports[] = {
    {"input_size", 0, input_size},
    {"input_read", 3, input_read},
    {"output_write", 2, output_write}
};

static bool named(turbowasm_name name, const char *text) {
    size_t length = strlen(text);
    return name.size == length && !memcmp(name.bytes, text, length);
}

static turbowasm_name wasm_name(const char *text) {
    return (turbowasm_name){(const uint8_t *)text, (uint32_t)strlen(text)};
}

static bool has_signature(const turbowasm_module *module, uint32_t index, uint32_t params) {
    turbowasm_function_signature signature;
    const cmeta_type_desc *i32 = turbowasm_value_type_descriptor(TURBOWASM_VALUE_I32);
    if (!turbowasm_module_function_signature_get(module, index, &signature) ||
        signature.param_count != params || signature.result_count != 1 ||
        turbowasm_module_function_result_type(module, index, 0) != i32)
        return false;
    for (uint32_t i = 0; i < params; ++i)
        if (turbowasm_module_function_param_type(module, index, i) != i32) return false;
    return true;
}

static bool allowed_import(const turbowasm_module *module, const turbowasm_import_desc *item) {
    if (item->kind != TURBOWASM_EXTERN_FUNCTION || !named(item->module_name, "praktor"))
        return false;
    for (size_t i = 0; i < sizeof imports / sizeof *imports; ++i)
        if (named(item->name, imports[i].name))
            return has_signature(module, item->item_index, imports[i].params);
    return false;
}

static bool validate_module(module_binding *binding, const char *entry) {
    const turbowasm_module *module = &binding->module;
    turbowasm_module_summary summary;
    turbowasm_memory_desc memory;
    if (!turbowasm_module_summary_get(module, &summary) || summary.has_start ||
        summary.table_count > 1 || summary.memory_count != 1 ||
        !turbowasm_module_memory_at(module, 0, &memory) ||
        memory.imported || memory.shared || memory.memory64 || !memory.has_maximum ||
        (uint64_t)memory.maximum * memory.page_size > MAX_MEMORY_BYTES)
        return false;
    for (size_t i = 0; i < turbowasm_module_import_count(module); ++i)
        if (!allowed_import(module, turbowasm_module_import_at(module, i))) return false;
    for (size_t i = 0; i < turbowasm_module_export_count(module); ++i) {
        const turbowasm_export_desc *item = turbowasm_module_export_at(module, i);
        if (named(item->name, entry) && item->kind == TURBOWASM_EXTERN_FUNCTION &&
            item->item_index >= summary.imported_function_count &&
            has_signature(module, item->item_index, 0)) {
            binding->function = item->item_index;
            return true;
        }
    }
    return false;
}

static void unbind(void *context, void *opaque) {
    (void)context;
    module_binding *binding = opaque;
    turbowasm_module_destroy(&binding->module);
    free(binding);
}

static praktor_runtime_status bind(void *context, const char *module, const char *entry,
    unsigned int fuel, unsigned int timeout_ms, void **out) {
    const approved_wasm_modules *approved = context;
    size_t i = 0;
    while (i < approved->count && (strcmp(approved->modules[i].module, module) ||
           strcmp(approved->modules[i].entry, entry))) ++i;
    if (i == approved->count) return PRAKTOR_RUNTIME_INVALID;
    const approved_wasm *item = &approved->modules[i];
    if (fuel > item->max_fuel || timeout_ms > item->max_timeout_ms)
        return PRAKTOR_RUNTIME_INVALID;
    module_binding *binding = calloc(1, sizeof *binding);
    if (!binding) return PRAKTOR_RUNTIME_RESOURCE;
    binding->config = (turbowasm_runtime_config){
        .allocator = {binding, allocate, resize, deallocate},
        .limits = {MAX_MODULE_BYTES, MAX_ALLOCATED_BYTES, MAX_MEMORY_BYTES, MAX_TABLE_ELEMENTS}
    };
    turbowasm_status loaded = turbowasm_module_load_borrowed_with_config(
        &binding->module, item->bytes, item->size, &binding->config);
    if (loaded != TURBOWASM_OK || !validate_module(binding, entry)) {
        unbind(NULL, binding);
        return loaded != TURBOWASM_OK ? map_status(loaded) : PRAKTOR_RUNTIME_INVALID;
    }
    *out = binding;
    return PRAKTOR_RUNTIME_OK;
}

static void release(void *context, void *opaque) {
    (void)context;
    invocation *job = opaque;
    turbowasm_execution_destroy(&job->execution);
    turbowasm_instance_destroy(&job->instance);
    free(job);
}

static praktor_runtime_status start(void *context, void *opaque, unsigned int fuel,
    const char *input, size_t size, void **out) {
    (void)context;
    module_binding *binding = opaque;
    invocation *job = calloc(1, sizeof *job);
    if (!job) return PRAKTOR_RUNTIME_RESOURCE;
    job->input = input;
    job->input_size = job->input_budget = size;
    job->fuel = fuel;
    turbowasm_linker linker = {0};
    turbowasm_status status = turbowasm_linker_init_with_config(&linker, &binding->config);
    static const turbowasm_value_kind i32[] = {TURBOWASM_VALUE_I32, TURBOWASM_VALUE_I32, TURBOWASM_VALUE_I32};
    for (size_t i = 0; status == TURBOWASM_OK && i < sizeof imports / sizeof *imports; ++i) {
        const turbowasm_host_function_type type = {i32, imports[i].params, i32, 1};
        status = turbowasm_linker_define_host_function(&linker, wasm_name("praktor"),
            wasm_name(imports[i].name), &type, imports[i].call, job);
    }
    if (status == TURBOWASM_OK)
        status = turbowasm_instance_create_linked(&job->instance, &binding->module, &linker);
    turbowasm_linker_destroy(&linker);
    if (status == TURBOWASM_OK)
        status = turbowasm_execution_create(&job->execution, &job->instance, binding->function, NULL, 0);
    if (status != TURBOWASM_OK) {
        release(NULL, job);
        return map_status(status);
    }
    *out = job;
    return PRAKTOR_RUNTIME_OK;
}

typedef struct interrupt {
    praktor_runtime_status (*check)(void *);
    void *context;
    praktor_runtime_status status;
} interrupt;

static bool interrupted(void *opaque) {
    interrupt *probe = opaque;
    probe->status = probe->check(probe->context);
    return probe->status != PRAKTOR_RUNTIME_OK;
}

static praktor_runtime_status step(void *context, void *opaque,
    praktor_runtime_status (*check)(void *), void *check_context,
    char *output, size_t capacity, size_t *size, int *done) {
    (void)context;
    invocation *job = opaque;
    unsigned int fuel = job->fuel < FUEL_PER_STEP ? job->fuel : FUEL_PER_STEP;
    interrupt probe = {check, check_context, PRAKTOR_RUNTIME_OK};
    const turbowasm_execution_options options = {fuel, true, interrupted, &probe};
    turbowasm_status status = turbowasm_execution_resume(&job->execution, &options);
    if (probe.status != PRAKTOR_RUNTIME_OK) return probe.status;
    if (status == TURBOWASM_YIELDED &&
        turbowasm_execution_yield_reason_get(&job->execution) == TURBOWASM_YIELD_FUEL) {
        job->fuel -= fuel;
        return job->fuel ? PRAKTOR_RUNTIME_OK : PRAKTOR_RUNTIME_RESOURCE;
    }
    if (status != TURBOWASM_OK) return map_status(status);
    const turbowasm_value *result = turbowasm_execution_result_at(&job->execution, 0);
    if (!result || result->kind != TURBOWASM_VALUE_I32 || result->as.i32 || !job->output_size)
        return PRAKTOR_RUNTIME_FAILED;
    if (job->output_size > capacity) return PRAKTOR_RUNTIME_RESOURCE;
    memcpy(output, job->output, job->output_size);
    *size = job->output_size;
    *done = 1;
    return PRAKTOR_RUNTIME_OK;
}

praktor_wasm_host make_turbowasm_host(approved_wasm_modules *approved) {
    return (praktor_wasm_host){approved, bind, unbind, start, step, release};
}
