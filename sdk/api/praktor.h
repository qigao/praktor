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
    PRAKTOR_RESULT_PLAN_MISMATCH = -6,
    PRAKTOR_RESULT_INPUT_CONTRACT = -7,
    PRAKTOR_RESULT_HOST_TOOL_REJECTED = -8
} praktor_result;

#define PRAKTOR_ABI_MAJOR 2u
#define PRAKTOR_ABI_MINOR 6u

#define PRAKTOR_CAPABILITY_JSON_WORKFLOW (UINT64_C(1) << 0)
#define PRAKTOR_CAPABILITY_SCRIPT_ENGINE (UINT64_C(1) << 1)
#define PRAKTOR_CAPABILITY_EXECUTION_CONTROL (UINT64_C(1) << 2)
#define PRAKTOR_CAPABILITY_WORKFLOW_PLAN (UINT64_C(1) << 3)
#define PRAKTOR_CAPABILITY_EXECUTION_EVENTS (UINT64_C(1) << 4)
#define PRAKTOR_CAPABILITY_HOST_TOOL (UINT64_C(1) << 5)
#define PRAKTOR_CAPABILITY_INLINE_WORKFLOW_PLAN (UINT64_C(1) << 6)

/** Hard upper bound on additional HostTool attempts encoded by retries.count. */
#define PRAKTOR_HOST_TOOL_MAX_RETRIES 1024u

typedef enum praktor_error_phase {
    PRAKTOR_ERROR_PHASE_NONE = 0,
    PRAKTOR_ERROR_PHASE_REQUEST = 1,
    PRAKTOR_ERROR_PHASE_INPUT_JSON = 2,
    PRAKTOR_ERROR_PHASE_EXECUTION = 3,
    PRAKTOR_ERROR_PHASE_RESULT_JSON = 4,
    PRAKTOR_ERROR_PHASE_PLAN = 5,
    PRAKTOR_ERROR_PHASE_INPUT_CONTRACT = 6,
    PRAKTOR_ERROR_PHASE_HOST_TOOL = 7
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
 * In-memory reviewed WorkflowPlan compilation request.
 *
 * source_id is a logical identity (for example "turboagent:plan:<hash>"), not
 * a filesystem path. workflow_yaml is borrowed only for this call; the
 * resulting immutable plan owns its source bytes and parsed HostTool DAG.
 *
 * The first inline ABI accepts HostTool-only finite DAGs and rejects includes,
 * dotenv/filesystem dependencies, scripts, process runners, dynamic each/matrix
 * expansion, and trigger-driven topology.
 */
typedef struct praktor_compile_inline_request {
    uint32_t struct_size;
    const char* source_id;
    const char* workflow_yaml;
    size_t workflow_yaml_size;
} praktor_compile_inline_request;

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

typedef enum praktor_event_type {
    PRAKTOR_EVENT_WORKFLOW_STARTED = 1,
    PRAKTOR_EVENT_TASK_STARTED = 2,
    PRAKTOR_EVENT_TASK_PROGRESS = 3,
    PRAKTOR_EVENT_TASK_COMPLETED = 4,
    PRAKTOR_EVENT_TASK_FAILED = 5,
    PRAKTOR_EVENT_WORKFLOW_COMPLETED = 6
} praktor_event_type;

/**
 * Borrowed event view valid only for the duration of one callback.
 *
 * Events may be delivered from workflow worker threads. sequence is a monotonic
 * per-execution total-order key assigned before callback dispatch; callbacks
 * may overlap when workflow tasks run concurrently.
 */
typedef struct praktor_execution_event {
    uint32_t struct_size;
    uint64_t sequence;
    praktor_event_type type;
    const char* task_name;
    const char* status;
    const char* message;
    const char* thread_id;
    const char* run_id;
    const char* turn_id;
    const char* tool_call_id;
} praktor_execution_event;

typedef void (PRAKTOR_CALL *praktor_event_sink_fn)(
    const praktor_execution_event* event,
    void* user_data);

/**
 * Optional harness observation context.
 *
 * All strings and user_data are borrowed for the synchronous execution call.
 * Lineage is emitted back on events for tracing only and is never injected into
 * workflow variables or script context.
 */
typedef struct praktor_execution_observer {
    uint32_t struct_size;
    praktor_event_sink_fn on_event;
    void* user_data;
    const char* thread_id;
    const char* run_id;
    const char* turn_id;
    const char* tool_call_id;
    uint64_t reserved[4];
} praktor_execution_observer;

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

#define PRAKTOR_HOST_TOOL_EXECUTOR_ABI_VERSION 1u

typedef enum praktor_host_tool_status {
    PRAKTOR_HOST_TOOL_OK = 0,
    PRAKTOR_HOST_TOOL_NOT_FOUND = 1,
    PRAKTOR_HOST_TOOL_DENIED = 2,
    PRAKTOR_HOST_TOOL_FAILED = 3,
    PRAKTOR_HOST_TOOL_CANCELLED = 4,
    PRAKTOR_HOST_TOOL_TIMED_OUT = 5
} praktor_host_tool_status;

/**
 * Result sink owned by Praktor and called synchronously by the host.
 *
 * The host retains ownership of json bytes and may release them as soon as the
 * sink returns. The sink copies/parses the payload before returning.
 */
typedef int32_t (PRAKTOR_CALL *praktor_host_tool_result_sink_fn)(
    const char* json,
    size_t json_size,
    void* user_data);

/**
 * Preflight one reviewed HostTool identity before any workflow task is started.
 *
 * arguments_template_json is the frozen typed `with:` object from the reviewed
 * WorkflowPlan. The callback must not execute the tool or cause tool side
 * effects.
 */
typedef int32_t (PRAKTOR_CALL *praktor_host_tool_validate_fn)(
    void* user_data,
    const char* tool_name,
    const char* arguments_template_json,
    size_t arguments_template_json_size,
    praktor_error* error);

/**
 * Invoke one preflighted HostTool.
 *
 * Call result_sink exactly once when returning PRAKTOR_HOST_TOOL_OK. control and
 * observer are the original borrowed public execution contracts so the embedding
 * host can propagate cancellation/deadline and lineage into its own runtime.
 */
typedef int32_t (PRAKTOR_CALL *praktor_host_tool_invoke_fn)(
    void* user_data,
    const char* tool_name,
    const char* arguments_json,
    size_t arguments_json_size,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_host_tool_result_sink_fn result_sink,
    void* result_sink_user_data,
    praktor_error* error);

typedef struct praktor_host_tool_executor {
    uint32_t struct_size;
    uint32_t abi_version;
    void* user_data;
    praktor_host_tool_validate_fn validate;
    praktor_host_tool_invoke_fn invoke;
    uint64_t reserved[4];
} praktor_host_tool_executor;

#define PRAKTOR_EXECUTE_REQUEST_INIT {sizeof(praktor_execute_request), NULL, NULL, 0}
#define PRAKTOR_COMPILE_REQUEST_INIT {sizeof(praktor_compile_request), NULL}
#define PRAKTOR_COMPILE_INLINE_REQUEST_INIT {sizeof(praktor_compile_inline_request), NULL, NULL, 0}
#define PRAKTOR_PLAN_EXECUTE_REQUEST_INIT {sizeof(praktor_plan_execute_request), NULL, NULL, 0}
#define PRAKTOR_EXECUTION_CONTROL_INIT {sizeof(praktor_execution_control), 0, NULL, NULL, 0, {0, 0, 0, 0}}
#define PRAKTOR_EXECUTION_OBSERVER_INIT {sizeof(praktor_execution_observer), NULL, NULL, NULL, NULL, NULL, NULL, {0, 0, 0, 0}}
#define PRAKTOR_OWNED_JSON_INIT {sizeof(praktor_owned_json), NULL, 0}
#define PRAKTOR_ERROR_INIT {sizeof(praktor_error), PRAKTOR_ERROR_PHASE_NONE, {0}}
#define PRAKTOR_HOST_TOOL_EXECUTOR_INIT {sizeof(praktor_host_tool_executor), PRAKTOR_HOST_TOOL_EXECUTOR_ABI_VERSION, NULL, NULL, NULL, {0, 0, 0, 0}}

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
typedef int32_t (PRAKTOR_CALL *praktor_compile_workflow_inline_fn)(
    const praktor_compile_inline_request* request,
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
typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_plan_observed_fn)(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error);
typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_plan_host_tools_fn)(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    const praktor_host_tool_executor* host_tools,
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
    praktor_execute_workflow_plan_observed_fn execute_workflow_plan_observed;
    praktor_execute_workflow_plan_host_tools_fn execute_workflow_plan_host_tools;
    praktor_compile_workflow_inline_fn compile_workflow_inline;
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
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_compile_workflow_inline(
    const praktor_compile_inline_request* request,
    praktor_workflow_plan** out_plan,
    praktor_error* error);

/**
 * Return canonical WorkflowPlan metadata JSON. Inline plans additionally
 * publish source_kind="inline", source_id, an empty dependency closure, and a
 * digest over the owned logical identity + exact source bytes.
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

/**
 * Execute a reviewed WorkflowPlan with optional lifecycle observation.
 *
 * The observer is borrowed for the duration of this synchronous call. Event
 * delivery never changes workflow success/failure semantics; exceptions thrown
 * by a C++ callback behind this C ABI are swallowed at the observation bridge.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow_plan_observed(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_owned_json* output,
    praktor_error* error);

/**
 * Execute a reviewed WorkflowPlan with backend-neutral HostTool authority.
 *
 * All HostTool identities retained by the plan are validated before the
 * WorkflowRunner starts. Any missing/denied identity returns
 * PRAKTOR_RESULT_HOST_TOOL_REJECTED with zero workflow side effects.
 */
PRAKTOR_C_API praktor_result PRAKTOR_CALL praktor_execute_workflow_plan_host_tools(
    const praktor_plan_execute_request* request,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    const praktor_host_tool_executor* host_tools,
    praktor_owned_json* output,
    praktor_error* error);

/** Release an opaque WorkflowPlan. Passing NULL is valid. */
PRAKTOR_C_API void PRAKTOR_CALL praktor_release_workflow_plan(
    praktor_workflow_plan* plan);

/** Release canonical JSON and reset data/size. Passing NULL is valid. */
PRAKTOR_C_API void PRAKTOR_CALL praktor_release_json(praktor_owned_json* data);
