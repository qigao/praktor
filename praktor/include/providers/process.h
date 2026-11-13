#ifndef PRAKTOR_PROCESS_SERVICE_H
#define PRAKTOR_PROCESS_SERVICE_H
#include <cmeta/data.h>
#include <cmeta/interface.h>
#include <stdbool.h>
#include <stddef.h>
#define PRAKTOR_PROCESS_MAX_JOBS 16u
#define PRAKTOR_PROCESS_MAX_ARGS 256u
#define PRAKTOR_PROCESS_MAX_ARG_BYTES (1024u * 1024u)
#define PRAKTOR_PROCESS_MAX_OUTPUT 4096u
/* Internal single-driver contract. Config strings borrow their definition
 * owner through Configurator stop. NativeIO copies argv/cwd during admission.
 * Each accepted job exclusively owns its process until drained and released. */
typedef struct praktor_process_config {
  const char *path, *working_dir;
  int timeout_ms;
} praktor_process_config;
typedef struct praktor_process_request {
  const char *const *args;
  size_t count;
} praktor_process_request;
typedef struct praktor_service_job {
  void *impl;
} praktor_service_job;
typedef struct praktor_service_result {
  bool done, success, cancelled;
  int exit_code;
  const char *output, *error_output, *error; /* Borrowed until job release. */
} praktor_service_result;
#define PRAKTOR_EXECUTION_METHODS(X, I)                                                            \
  X(I, R2, int, start, const praktor_process_request *, request, praktor_service_job *, out)       \
  X(I, R2, int, poll, praktor_service_job *, job, praktor_service_result *, out)                   \
  X(I, R1, int, cancel, praktor_service_job *, job)                                                \
  X(I, R1, int, release, praktor_service_job *, job)
CMETA_INTERFACE(praktor_execution_service, PRAKTOR_EXECUTION_METHODS);
typedef struct praktor_process_service {
  const char *path, *working_dir;
  int timeout_ms;
  size_t jobs;
  bool active;
} praktor_process_service;
const cmeta_data_desc *praktor_process_config_reflection(void);
const cmeta_data_desc *praktor_process_service_reflection(void);
praktor_execution_service praktor_process_service_bind(praktor_process_service *service);
#endif
