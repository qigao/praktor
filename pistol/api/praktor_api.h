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

// praktor_execute_workflow
// Evaluates the workflow at workflow_path. 
// Uses json_vars to inject starting variables from a remote payload.
// Returns PRAKTOR_API_SUCCESS on success.
#define PRAKTOR_API_SUCCESS 0
#define PRAKTOR_API_EXECUTION_FAILED 1
#define PRAKTOR_API_INVALID_ARGUMENT -1
#define PRAKTOR_API_INVALID_JSON -2

// Returns:
//   PRAKTOR_API_SUCCESS on success
//   PRAKTOR_API_EXECUTION_FAILED when the workflow runs but fails
//   PRAKTOR_API_INVALID_ARGUMENT when workflow_path is null/empty
//   PRAKTOR_API_INVALID_JSON when json_vars is present but not a JSON object
PRAKTOR_API int praktor_execute_workflow(const char* workflow_path, const char* json_vars);

#ifdef __cplusplus
}
#endif
