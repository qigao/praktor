#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #ifdef PRAKTOR_EXPORTS
    #define PRAKTOR_API __declspec(dllexport)
  #else
    #define PRAKTOR_API __declspec(dllimport)
  #endif
#else
  #define PRAKTOR_API __attribute__((visibility("default")))
#endif

#ifdef _WIN32
  #define PRAKTOR_CALL __cdecl
#else
  #define PRAKTOR_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum praktor_result {
    PRAKTOR_RESULT_SUCCESS = 0,
    PRAKTOR_RESULT_EXECUTION_FAILED = 1,
    PRAKTOR_RESULT_INVALID_ARGUMENT = -1,
    PRAKTOR_RESULT_INVALID_JSON = -2,
    PRAKTOR_RESULT_INVALID_DATA = -3,
    PRAKTOR_RESULT_INVALID_SCHEMA = -4,
    PRAKTOR_RESULT_TYPE_NOT_FOUND = -5,
    PRAKTOR_RESULT_SERIALIZATION_FAILED = -6,
    PRAKTOR_RESULT_OUT_OF_MEMORY = -7,
    PRAKTOR_RESULT_INTERNAL_ERROR = -8
} praktor_result;

#define PRAKTOR_ABI_MAJOR 1u
#define PRAKTOR_ABI_MINOR 0u

#define PRAKTOR_CAPABILITY_EXECUTE_WORKFLOW (UINT64_C(1) << 0)
#define PRAKTOR_CAPABILITY_DATA_BIND (UINT64_C(1) << 1)

typedef enum praktor_data_format {
    PRAKTOR_DATA_FORMAT_JSON = 0,
    PRAKTOR_DATA_FORMAT_YAML = 1,
    PRAKTOR_DATA_FORMAT_XML = 2,
    PRAKTOR_DATA_FORMAT_CSV = 3,
    PRAKTOR_DATA_FORMAT_BINARY = 4
} praktor_data_format;

/**
 * Schema-bound workflow execution request.
 *
 * Provide exactly one trusted schema source: schema_path or schema_text/schema_text_size.
 * The schema must contain input_type and output_type. input_data is borrowed for the call.
 * csv_row is used only for CSV input.
 */
typedef struct praktor_execute_request {
    uint32_t struct_size;
    const char* workflow_path;
    const void* input_data;
    size_t input_size;
    praktor_data_format input_format;
    size_t csv_row;
    const char* schema_path;
    const char* schema_text;
    size_t schema_text_size;
    const char* input_type;
    const char* output_type;
    praktor_data_format output_format;
} praktor_execute_request;

/** Output owned by Praktor. Release it exactly once with praktor_release_data(). */
typedef struct praktor_owned_data {
    uint32_t struct_size;
    void* data;
    size_t size;
    praktor_data_format format;
} praktor_owned_data;

/** Detailed DataBind diagnostics populated when a schema or format operation fails. */
typedef struct praktor_error {
    uint32_t struct_size;
    int32_t data_bind_status;
    int32_t line;
    int32_t column;
    char path[260];
    char message[512];
} praktor_error;

#define PRAKTOR_EXECUTE_REQUEST_INIT                                                        \
    {sizeof(praktor_execute_request), NULL, NULL, 0, PRAKTOR_DATA_FORMAT_JSON, 0, NULL,    \
     NULL, 0, NULL, NULL, PRAKTOR_DATA_FORMAT_JSON}
#define PRAKTOR_OWNED_DATA_INIT {sizeof(praktor_owned_data), NULL, 0, PRAKTOR_DATA_FORMAT_JSON}
#define PRAKTOR_ERROR_INIT {sizeof(praktor_error), 0, -1, -1, {0}, {0}}

typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_fn)(
    const praktor_execute_request* request,
    praktor_owned_data* output,
    praktor_error* error);
typedef void (PRAKTOR_CALL *praktor_release_data_fn)(praktor_owned_data* data);

typedef struct praktor_api {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t capabilities;
    praktor_execute_workflow_fn execute_workflow;
    praktor_release_data_fn release_data;
} praktor_api;

typedef const praktor_api* (PRAKTOR_CALL *praktor_get_api_fn)(void);

PRAKTOR_API const praktor_api* PRAKTOR_CALL praktor_get_api(void);
/**
 * Execute a workflow with schema-bound input and serialize its minimal result.
 *
 * PRAKTOR_RESULT_SUCCESS and PRAKTOR_RESULT_EXECUTION_FAILED may both return owned output.
 * All negative results leave output empty. The result contains workflow_status, task states and
 * explicit task outputs; it never includes the complete input or environment snapshot.
 */
PRAKTOR_API praktor_result PRAKTOR_CALL praktor_execute_workflow(
    const praktor_execute_request* request,
    praktor_owned_data* output,
    praktor_error* error);
/** Release output storage and reset data/size. Passing NULL is valid. */
PRAKTOR_API void PRAKTOR_CALL praktor_release_data(praktor_owned_data* data);

#ifdef __cplusplus
}
#endif
