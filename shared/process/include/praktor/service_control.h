#ifndef PRAKTOR_SERVICE_CONTROL_H
#define PRAKTOR_SERVICE_CONTROL_H
#include "process_control.h"
#include "process_state.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum praktor_service_state {
  PRAKTOR_SERVICE_UNKNOWN, PRAKTOR_SERVICE_STOPPED, PRAKTOR_SERVICE_START_PENDING,
  PRAKTOR_SERVICE_STOP_PENDING, PRAKTOR_SERVICE_RUNNING
} praktor_service_state;
typedef enum praktor_service_error {
  PRAKTOR_SERVICE_NONE, PRAKTOR_SERVICE_INVALID, PRAKTOR_SERVICE_STATE_FAILED,
  PRAKTOR_SERVICE_SPAWN_FAILED, PRAKTOR_SERVICE_TIMEOUT, PRAKTOR_SERVICE_CANCELLED
} praktor_service_error;
typedef struct praktor_service_args { const char *const *values; size_t count; } praktor_service_args;
typedef struct praktor_service_profile {
  const char *program;
  praktor_service_args status, start, stop;
} praktor_service_profile;
typedef struct praktor_service_request {
  praktor_process_operation operation;
  const char *name, *profile_name;
  const praktor_service_profile *profile; /* NULL reports unknown profile. */
  praktor_service_args arguments;
  int64_t timeout_ms, poll_interval_ms; /* Positive, <= INT32_MAX. */
} praktor_service_request;
typedef struct praktor_service_command {
  const char *program;
  praktor_service_args arguments;
  uint32_t timeout_ms;
  size_t max_output_bytes;
} praktor_service_command;
typedef struct praktor_service_response {
  praktor_process_state state;
  int exit_code;
  const char *output;
  size_t output_size;
  const char *failure_message; /* Only used for a non-OK run callback return. */
} praktor_service_response;
typedef struct praktor_service_host {
  void *user;
  /* Synchronous, no throwing. Command/argv borrowed only for this call;
   * returned output remains valid until the next run or host destruction. */
  int (*run)(void *, const praktor_service_command *, praktor_service_response *);
  /* Both NULL selects Salts monotonic time/sleep. Otherwise supply both.
   * Host calls cannot be preempted; elapsed call time consumes the deadline. */
  uint64_t (*now_ns)(void *);
  void (*sleep_ms)(void *, uint32_t);
} praktor_service_host;
typedef struct praktor_service_config {
  size_t max_arguments, max_metadata_bytes, max_output_bytes;
} praktor_service_config;
typedef struct praktor_service_control { void *impl; } praktor_service_control;
typedef struct praktor_service_result {
  bool ok, changed, include_state, include_process;
  praktor_service_state state;
  praktor_service_error error;
  praktor_process_state process_state;
  int exit_code;
  const char *phase, *message;
  uint64_t duration_ms;
} praktor_service_result;
/* Allocate all render scratch at init. All three command templates are
 * validated/rendered before the first run; overflow cannot partially restart.
 * Single driver, including destroy; reentrant execute returns EBUSY.
 * Request storage is borrowed for execute. Results contain no borrowed output;
 * diagnostic storage survives until the next execute or destroy. No per-poll
 * allocation, shell expansion, implicit Legacy dispatch or native service API. */
int praktor_service_control_init(praktor_service_control *, const praktor_service_config *);
/* A reentrant destroy during run is ignored; retry after execute returns. */
void praktor_service_control_destroy(praktor_service_control *);
int praktor_service_control_execute(praktor_service_control *, const praktor_service_request *,
                                    const praktor_service_host *, praktor_service_result *);
const praktor_service_profile *praktor_service_windows_scm_profile(void);
const char *praktor_service_state_name(praktor_service_state);
/* Binary-safe numeric STATE field parser. No allocation or locale text matching. */
int praktor_service_parse_state(const char *output, size_t size, praktor_service_state *);
#ifdef __cplusplus
}
#endif
#endif
