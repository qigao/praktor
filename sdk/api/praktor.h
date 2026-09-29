#pragma once

#include "praktor_export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #define PRAKTOR_CALL __cdecl
#else
  #define PRAKTOR_CALL
#endif

typedef enum praktor_result {
    PRAKTOR_RESULT_SUCCESS = 0,
    PRAKTOR_RESULT_EXECUTION_FAILED = 1,
    PRAKTOR_RESULT_CANCELLED = 2,
    PRAKTOR_RESULT_TIMED_OUT = 3,
    PRAKTOR_RESULT_INVALID_ARGUMENT = -1,
    PRAKTOR_RESULT_INVALID_JSON = -2,
    PRAKTOR_RESULT_OUT_OF_MEMORY = -3,
    PRAKTOR_RESULT_INTERNAL_ERROR = -4
} praktor_result;

#define PRAKTOR_ABI_MAJOR 2u
#define PRAKTOR_ABI_MINOR 1u

#define PRAKTOR_CAPABILITY_JSON_WORKFLOW (UINT64_C(1) << 0)
#define PRAKTOR_CAPABILITY_SCRIPT_ENGINE (UINT64_C(1) << 1)
#define PRAKTOR_CAPABILITY_EXECUTION_CONTROL (UINT64_C(1) << 2)

typedef enum praktor_error_phase {
    PRAKTOR_ERROR_PHASE_NONE = 0,
    PRAKTOR_ERROR_PHASE_REQUEST = 1,
    PRAKTOR_ERROR_PHASE_INPUT_JSON = 2,
    PRAKTOR_ERROR_PHASE_EXECUTION = 3,
    PRAKTOR_ERROR_PHASE_RESULT_JSON = 4
} praktor_error_phase;

/**
 * JSON workflow execution request.
 *
 * input_json is borrowed for the duration of the call and must contain one JSON object.
 * The object members become the workflow inputs without schema coercion.
 */
typedef struct praktor_execute_request {
    uint32_t struct_size;
    const char* workflow_path;
    const char* input_json;
    size_t input_json_size;
} praktor_execute_request;

/**
 * Cooperative execution control for one synchronous workflow call.
 *
 * timeout_ms is relative to entry into praktor_execute_workflow_controlled(); zero disables
 * the deadline. cancel_probe is optional. When present it may be called synchronously from
 * workflow worker threads, but Praktor serializes concurrent probe invocations. The callback
 * must be non-blocking; cancel_user_data is borrowed until the controlled call returns.
 */
typedef int32_t (PRAKTOR_CALL *praktor_cancel_probe_fn)(void* user_data);

typedef struct praktor_execution_control {
    uint32_t struct_size;
    uint32_t reserved0;
    uint64_t timeout_ms;
    praktor_cancel_probe_fn cancel_probe;
    void* cancel_user_data;
    uint64_t reserved[4];
} praktor_execution_control;

/** Canonical JSON owned by Praktor. Release it exactly once with praktor_release_json(). */
typedef struct praktor_owned_json {
    uint32_t struct_size;
    char* data;
    size_t size;
} praktor_owned_json;

/** Error diagnostics for request validation, JSON processing, or workflow execution. */
typedef struct praktor_error {
    uint32_t struct_size;
    praktor_error_phase phase;
    char message[512];
} praktor_error;

#define PRAKTOR_EXECUTE_REQUEST_INIT {sizeof(praktor_execute_request), NULL, NULL, 0}
#define PRAKTOR_EXECUTION_CONTROL_INIT \
    {sizeof(praktor_execution_control), 0, 0, NULL, NULL, {0, 0, 0, 0}}
#define PRAKTOR_OWNED_JSON_INIT {sizeof(praktor_owned_json), NULL, 0}
#define PRAKTOR_ERROR_INIT {sizeof(praktor_error), PRAKTOR_ERROR_PHASE_NONE, {0}}

typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_fn)(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error);
typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_controlled_fn)(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error);
typedef void (PRAKTOR_CALL *praktor_release_json_fn)(praktor_owned_json* data);

typedef struct praktor_api {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t capabilities;
    praktor_execute_workflow_fn execute_workflow;
    praktor_release_json_fn release_json;
    praktor_execute_workflow_controlled_fn execute_workflow_controlled;
} praktor_api;

typedef const praktor_api* (PRAKTOR_CALL *praktor_get_api_fn)(void);

PRAKTOR_C_API const praktor_api* PRAKTOR_CALL praktor_get_api(void);
/**
 * Execute a workflow and return its canonical JSON result.
 *
 * PRAKTOR_RESULT_SUCCESS and PRAKTOR_RESULT_EXECUTION_FAILED both return owned JSON. Negative
 * results leave output empty. The result contains workflow_status, task states, and explicit task
 * outputs; it never contains the complete input or environment snapshot.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error);
/**
 * Execute with cooperative cancellation and/or a relative deadline.
 *
 * SUCCESS, EXECUTION_FAILED, CANCELLED, and TIMED_OUT return owned canonical JSON.
 * The legacy praktor_execute_workflow() entry point remains unchanged.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow_controlled(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error);
/** Release canonical JSON and reset data/size. Passing NULL is valid. */
PRAKTOR_C_API void PRAKTOR_CALL praktor_release_json(praktor_owned_json* data);
