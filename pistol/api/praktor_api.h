#pragma once

#ifdef _WIN32
  #ifdef PRAKTOR_EXPORTS
    #define PRAKTOR_API __declspec(dllexport)
  #else
    #define PRAKTOR_API __declspec(dllimport)
  #endif
#else
  #define PRAKTOR_API __attribute__((visibility("default")))
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

// Returns:
//   PRAKTOR_API_SUCCESS on success
//   PRAKTOR_API_EXECUTION_FAILED when the workflow runs but fails
//   PRAKTOR_API_INVALID_ARGUMENT when workflow_path is null/empty
//   PRAKTOR_API_INVALID_JSON when json_vars is present but not a JSON object
PRAKTOR_API praktor_result praktor_execute_workflow(const char* workflow_path, const char* json_vars);

#ifdef __cplusplus
}

#include <string>

namespace praktor {
    inline praktor_result execute_workflow(const std::string& workflow_path, const std::string& json_vars = "{}") {
        return ::praktor_execute_workflow(workflow_path.c_str(), json_vars.c_str());
    }
}
#endif
