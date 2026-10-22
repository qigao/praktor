#ifndef PRAKTOR_PROCESS_CONTROL_H
#define PRAKTOR_PROCESS_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <salts/error_codes.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous control of an application identified by image/session/instance.
 * Distinct from process_worker: stopping this controller does not kill a
 * successfully started persistent application. No allocation or C++ runtime. */
typedef enum praktor_process_operation {
  PRAKTOR_PROCESS_STATUS, PRAKTOR_PROCESS_START,
  PRAKTOR_PROCESS_STOP, PRAKTOR_PROCESS_RESTART
} praktor_process_operation;
typedef enum praktor_process_control_error {
  PRAKTOR_PROCESS_CONTROL_NONE, PRAKTOR_PROCESS_CONTROL_INVALID_PARAMETERS,
  PRAKTOR_PROCESS_CONTROL_BACKEND_FAILURE, PRAKTOR_PROCESS_CONTROL_TIMEOUT,
  PRAKTOR_PROCESS_CONTROL_UNSUPPORTED_PLATFORM
} praktor_process_control_error;
typedef struct praktor_process_snapshot {
  bool running;
  uint32_t pid, session_id;
  uint64_t instance_token;
  const char *canonical_image_name;
} praktor_process_snapshot;
typedef struct praktor_process_control_request {
  praktor_process_operation operation;
  const char *image_name, *executable;
  const char *const *arguments;
  size_t argument_count;
  const char *working_directory;
  int64_t startup_timeout_ms, stop_timeout_ms;
  bool force_terminate;
} praktor_process_control_request;
typedef struct praktor_process_backend_result {
  bool ok;
  int native_error;
  const char *message;
} praktor_process_backend_result;
typedef struct praktor_process_control_host {
  void *user;
  praktor_process_backend_result (*query)(void *, const char *image_name,
                                          praktor_process_snapshot *);
  praktor_process_backend_result (*start)(void *, const praktor_process_control_request *,
                                          praktor_process_snapshot *);
  praktor_process_backend_result (*request_stop)(void *, const praktor_process_snapshot *);
  praktor_process_backend_result (*terminate)(void *, const praktor_process_snapshot *);
  /* Both NULL selects Salts monotonic time and sleep. Otherwise supply both.
   * The clock must advance monotonically; sleep may return early. */
  uint64_t (*now_ns)(void *);
  void (*sleep_ms)(void *, uint32_t);
  uint64_t poll_interval_ms;
} praktor_process_control_host;
typedef struct praktor_process_control_result {
  bool ok, changed;
  praktor_process_snapshot snapshot;
  praktor_process_control_error error;
  int native_error;
  const char *phase, *message;
  uint64_t duration_ms;
} praktor_process_control_result;
/* Request/host storage is borrowed for the call. Query and start image strings
 * must remain valid until the NEXT SUCCESSFUL invocation of that SAME callback,
 * including across calls to the other callbacks. This pins the exact started instance
 * while queries observe replacements. Error messages survive until the next
 * backend call. Returned image/message are borrowed with those same lifetimes;
 * consume/copy them before another call or destroying host storage. A failed
 * query/start must preserve its preceding successful image storage.
 * Host callbacks must not throw across this C boundary. Host calls themselves
 * must be bounded: their elapsed time consumes the deadline, but cannot be
 * preempted here. Return SALTS_OK for a completed execution (also on domain
 * failure); EINVAL means a malformed API call, with no backend dispatch. */
int praktor_process_control_execute(const praktor_process_control_request *,
                                    const praktor_process_control_host *,
                                    praktor_process_control_result *);
#ifdef __cplusplus
}
#endif
#endif
