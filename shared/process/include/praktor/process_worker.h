#ifndef PRAKTOR_PROCESS_WORKER_H
#define PRAKTOR_PROCESS_WORKER_H
#include "process_driver.h"
#include "process_state.h"
#include <salts/error_codes.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_process_worker { void *impl; } praktor_process_worker;
typedef struct praktor_process_worker_config {
  cmeta_process_options_t options;
  const void *input;
  size_t input_size, max_input_bytes, max_output_bytes;
  size_t max_metadata_bytes, max_arguments, max_environment_entries;
  /* Borrowed until destroy joins the driver. Callbacks must not throw. The
   * optional setup runs on the driver before Process admission (e.g. TLS).
   * Cancellation accepted during setup prevents admission. */
  int (*thread_start)(void *user);
  praktor_process_emit emit;
  void *user;
} praktor_process_worker_config;
typedef struct praktor_process_worker_view {
  praktor_process_state state;
  cmeta_process_result_t result;
  const char *stdout_data, *stderr_data, *error, *diagnostic;
  size_t stdout_size, stderr_size;
  bool output_streamed_live;
} praktor_process_worker_view;
void praktor_process_worker_config_init(praktor_process_worker_config *config);
/* Copies all admission metadata and binary input before starting one joined
 * Salts thread. No C++ runtime or alternative process backend. Failed init
 * leaves output empty and admits no native Process. Native admission errors
 * are reported as terminal results. Handle must initially be zero. */
int praktor_process_worker_start(praktor_process_worker *worker,
                                 const praktor_process_worker_config *config);
/* Concurrent observers/cancel/wait are supported. Handle lifetime is external:
 * no external observer may race destroy. Driver callbacks remain valid until
 * its join, including supported reentry. Result bytes are immutable and borrowed until
 * destroy; view returns EBUSY before completion. */
int praktor_process_worker_state(const praktor_process_worker *worker, praktor_process_state *state);
int praktor_process_worker_cancel(praktor_process_worker *worker, bool *accepted);
/* Zero polls; UINT64_MAX waits indefinitely. A finite expiry returns
 * ETIMEDOUT without cancelling. Blocking wait/destroy on the driver thread
 * returns EBUSY. Reentrant state, view, poll and cancel remain safe. */
int praktor_process_worker_wait(const praktor_process_worker *worker, uint64_t timeout_ns);
int praktor_process_worker_result(const praktor_process_worker *worker, praktor_process_worker_view *view);
/* Cancel, join, then release C driver buffers only after native destruction.
 * Permanent cleanup faults deliberately keep ownership and can block join.
 * Join/release failure leaves the handle owned for retry; never detach/free. */
int praktor_process_worker_destroy(praktor_process_worker *worker);
const char *praktor_process_state_name(praktor_process_state state);
#ifdef __cplusplus
}
#endif
#endif
