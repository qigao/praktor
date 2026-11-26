#ifndef PRAKTOR_PROCESS_SERVICE_H
#define PRAKTOR_PROCESS_SERVICE_H
#include "providers/execution.h"
#include <cmeta/data.h>
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
  const json_value_t *args;
} praktor_process_config;
typedef struct praktor_process_service {
  const char *path, *working_dir;
  int timeout_ms;
  const json_value_t *args;
  size_t jobs;
  bool active;
} praktor_process_service;
const cmeta_data_desc *praktor_process_config_reflection(void);
const cmeta_data_desc *praktor_process_service_reflection(void);
praktor_execution_service praktor_process_service_bind(praktor_process_service *service);
int praktor_process_validate_inputs(const json_value_t *args,
                                    const praktor_execution_admission *admission);
#endif
