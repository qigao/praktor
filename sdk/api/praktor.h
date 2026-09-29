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
    PRAKTOR_RESULT_INTERNAL_ERROR = -4,
    PRAKTOR_RESULT_PLAN_INVALID = -5,
    PRAKTOR_RESULT_PLAN_MISMATCH = -6
} praktor_result;

#define PRAKTOR_ABI_MAJOR 2u
#define PRAKTOR_ABI_MINOR 2u

#define PRAKTOR_CAPABILITY_JSON_WORKFLOW (UINT64_C(1) << 0)
#define PRAKTOR_CAPABILITY_SCRIPT_ENGINE (UINT64_C(1) << 1)
#define PRAKTOR_CAPABILITY_EXECUTION_CONTROL (UINT64_C(1) << 2)
#define PRAKTOR_CAPABILITY_WORKFLOW_PLAN (UINT64_C(1) << 3)

typedef enum praktor_error_phase {
    PRAKTOR_ERROR_PHASE_NONE = 0,
    PRAKTOR_ERROR_PHASE_REQUEST = 1,
    PRAKTOR_ERROR_PHASE_INPUT_JSON = 2,
    PRAKTOR_ERROR_PHASE_EXECUTION = 3,
    PRAKTOR_ERROR_PHASE_RESULT_JSON = 4,
    PRAKTOR_ERROR_PHASE_PLAN = 5
} praktor_error_phase;

/** Opaque immutable reviewed workflow identity owned by Praktor. */
typedef struct praktor_workflow_plan praktor_workflow_plan;

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
 * WorkflowPlan compilation request.
 *
 * workflow_path is borrowed for the duration of the call. The initial WorkflowPlan
 * root is the canonical parent directory of this file. Transitive workflow includes,
 * uses workflows, relative .tbs imports, and dotenv dependencies must resolve inside
 * that root and must be regular non-symlink files.
 */
typedef struct praktor_compile_request {
    uint32_t struct_size;
    const char* workflow_path;
} praktor_compile_request;

/**
 * WorkflowPlan execution request.
 *
 * The plan is borrowed for the duration of the call. input_json must be one JSON
 * object. The plan dependency closure is revalidated before workflow execution.
 */
typedef struct praktor_plan_execute_request {
    uint32_t struct_size;
    const praktor_workflow_plan* plan;
    const char* input_json;
    size_t input_json_size;
} praktor_plan_execute_request;

/**
 * Host-owned cooperative execution control.
 *
 * is_cancelled is optional and may be invoked synchronously from the calling
 * thread or workflow worker threads while the controlled call is active. The
 * callback must therefore be thread-safe, non-blocking, and must not throw.
 * user_data is borrowed for the duration of the controlled call.
 *
 * timeout_ms is a relative steady-clock deadline. Zero disables the deadline.
 * No polling/background thread is created by the public API.
 */
typedef int32_t (PRAKTOR_CALL *praktor_cancel_probe_fn)(void* user_data);

typedef struct praktor_execution_control {
    uint32_t struct_size;
    uint32_t reserved0;
    praktor_cancel_probe_fn is_cancelled;
    void* user_data;
    uint64_t timeout_ms;
    uint64_t reserved[4];
} praktor_execution_control;

/** Canonical JSON owned by Praktor. Release it exactly once with praktor_release_json(). */
typedef struct praktor_owned_json {
    uint32_t struct_size;
    char* data;
    size_t size;
} praktor_owned_json;

/** Error diagnostics for request validation, plan processing, JSON processing, or workflow execution. */
typedef struct praktor_error {
    uint32_t struct_size;
    praktor_error_phase phase;
    char message[512];
} praktor_error;

#define PRAKTOR_EXECUTE_REQUEST_INIT {sizeof(praktor_execute_request), NULL, NULL, 0}
#define PRAKTOR_COMPILE_REQUEST_INIT {sizeof(praktor_compile_request), NULL}
#define PRAKTOR_PLAN_EXECUTE_REQUEST_INIT {sizeof(praktor_plan_execute_request), NULL, NULL, 0}
#define PRAKTOR_EXECUTION_CONTROL_INIT {sizeof(praktor_execution_control), 0, NULL, NULL, 0, {0, 0, 0, 0}}
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
typedef int32_t (PRAKTOR_CALL *praktor_compile_workflow_fn)(
    const praktor_compile_request* request,
    praktor_workflow_plan** out_plan,
    praktor_error* error);
typedef int32_t (PRAKTOR_CALL *praktor_describe_workflow_plan_fn)(
    const praktor_workflow_plan* plan,
    praktor_owned_json* output,
    praktor_error* error);
typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_plan_fn)(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error);
typedef void (PRAKTOR_CALL *praktor_release_workflow_plan_fn)(
    praktor_workflow_plan* plan);
typedef void (PRAKTOR_CALL *praktor_release_json_fn)(praktor_owned_json* data);

typedef struct praktor_api {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t capabilities;
    praktor_execute_workflow_fn execute_workflow;
    praktor_release_json_fn release_json;
    praktor_execute_workflow_controlled_fn execute_workflow_controlled;
    praktor_compile_workflow_fn compile_workflow;
    praktor_describe_workflow_plan_fn describe_workflow_plan;
    praktor_execute_workflow_plan_fn execute_workflow_plan;
    praktor_release_workflow_plan_fn release_workflow_plan;
} praktor_api;

typedef const praktor_api* (PRAKTOR_CALL *praktor_get_api_fn)(void);

PRAKTOR_C_API const praktor_api* PRAKTOR_CALL praktor_get_api(void);

/**
 * Execute a workflow and return its canonical JSON result.
 *
 * All non-negative results return owned JSON. Negative
 * results leave output empty. The result contains workflow_status, task states, and explicit task
 * outputs; it never contains the complete input or environment snapshot.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_json* output,
    praktor_error* error);

/**
 * Execute a workflow with cooperative cancellation and/or a relative deadline.
 *
 * Non-negative results (SUCCESS, EXECUTION_FAILED, CANCELLED, TIMED_OUT) return
 * canonical owned JSON. Negative results leave output empty.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow_controlled(
    const praktor_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error);

/**
 * Compile a reviewed workflow into an immutable/revalidatable WorkflowPlan.
 *
 * On success, out_plan owns one Praktor allocation that must be released exactly
 * once with praktor_release_workflow_plan().
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_compile_workflow(
    const praktor_compile_request* request,
    praktor_workflow_plan** out_plan,
    praktor_error* error);

/**
 * Return canonical WorkflowPlan metadata JSON containing root path, root directory,
 * plan digest, and the sorted dependency closure with per-file SHA-256 digests.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_describe_workflow_plan(
    const praktor_workflow_plan* plan,
    praktor_owned_json* output,
    praktor_error* error);

/**
 * Revalidate a WorkflowPlan and execute it.
 *
 * PRAKTOR_RESULT_PLAN_MISMATCH is returned before workflow execution when any
 * reviewed dependency is missing, becomes a symlink/non-regular file, or changes
 * content.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow_plan(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    praktor_owned_json* output,
    praktor_error* error);

/** Release an opaque WorkflowPlan. Passing NULL is valid. */
PRAKTOR_C_API void PRAKTOR_CALL praktor_release_workflow_plan(
    praktor_workflow_plan* plan);

/** Release canonical JSON and reset data/size. Passing NULL is valid. */
PRAKTOR_C_API void PRAKTOR_CALL praktor_release_json(praktor_owned_json* data);
