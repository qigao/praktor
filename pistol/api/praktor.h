#pragma once

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
    PRAKTOR_RESULT_INVALID_JSON = -2
} praktor_result;

#define PRAKTOR_API_SUCCESS PRAKTOR_RESULT_SUCCESS
#define PRAKTOR_API_EXECUTION_FAILED PRAKTOR_RESULT_EXECUTION_FAILED
#define PRAKTOR_API_INVALID_ARGUMENT PRAKTOR_RESULT_INVALID_ARGUMENT
#define PRAKTOR_API_INVALID_JSON PRAKTOR_RESULT_INVALID_JSON

#define PRAKTOR_ABI_MAJOR 1u
#define PRAKTOR_ABI_MINOR 0u

#define PRAKTOR_CAPABILITY_EXECUTE_WORKFLOW (UINT64_C(1) << 0)

typedef int32_t (PRAKTOR_CALL *praktor_execute_workflow_fn)(const char* workflow_path,
                                                            const char* json_vars);

typedef struct praktor_api_v1 {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint64_t capabilities;
    praktor_execute_workflow_fn execute_workflow;
} praktor_api_v1;

typedef const praktor_api_v1* (PRAKTOR_CALL *praktor_get_api_v1_fn)(void);

PRAKTOR_API const praktor_api_v1* PRAKTOR_CALL praktor_get_api_v1(void);
PRAKTOR_API praktor_result PRAKTOR_CALL praktor_execute_workflow(const char* workflow_path,
                                                                 const char* json_vars);

#ifdef __cplusplus
}

#include <string>

namespace praktor {
    inline praktor_result execute_workflow(const std::string& workflow_path, const std::string& json_vars = "{}") {
        return ::praktor_execute_workflow(workflow_path.c_str(), json_vars.c_str());
    }
}
#endif
