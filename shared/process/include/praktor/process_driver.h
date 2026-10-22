#ifndef PRAKTOR_PROCESS_DRIVER_H
#define PRAKTOR_PROCESS_DRIVER_H
#include <salts/process.h>
struct cflow_process_retainer;
#ifdef __cplusplus
extern "C" {
#endif
/* One single-driver C11 owner. No worker, C++ object or OS process backend.
 * All capture/input/line buffers are allocated before native I/O admission. */
typedef struct praktor_process_driver { void *impl; } praktor_process_driver;
/* Return an error to stop execution. Never throw across this C boundary.
 * The callback/user are borrowed until close, which detaches them BEFORE
 * driving terminal callbacks or handing ownership to the explicit retainer. */
typedef int (*praktor_process_emit)(void *user, const char *line, size_t size);
typedef struct praktor_process_view {
  bool active, closing, done, failed, output_limited;
  cmeta_process_result_t result;
  const char *stdout_data, *stderr_data, *error;
  size_t stdout_size, stderr_size;
} praktor_process_view;
/* Input is copied. Process options are borrowed only during start (Salts
 * copies admission metadata). max_output_bytes is a combined stdout/stderr
 * bound. NULL retainer selects synchronous ownership: release refuses live
 * leases and the caller must continue closing; no implicit recovery owner.
 * A non-NULL retainer requires the explicit PRAKTOR_PROCESS_RETAINER build
 * capability (the P1 SDK); otherwise admission returns ENOTSUP before spawn.
 * The baseline SDK profile uses the same Process owner without P1 APIs. */
int praktor_process_driver_start(praktor_process_driver *driver,
    const cmeta_process_options_t *options, const void *input, size_t input_size,
    size_t max_output_bytes, struct cflow_process_retainer *retainer,
    praktor_process_emit emit, void *emit_user);
int praktor_process_driver_tick(praktor_process_driver *driver, bool *done);
/* Synchronous compatibility operations. run polls a borrowed cancellation
 * predicate on the driver thread. drain waits until native destruction; a
 * persistent provider fault deliberately retains ownership and blocks. */
int praktor_process_driver_run(praktor_process_driver *driver,
                               int (*cancelled)(void *user), void *user);
int praktor_process_driver_drain(praktor_process_driver *driver);
int praktor_process_driver_terminate(praktor_process_driver *driver);
int praktor_process_driver_view(const praktor_process_driver *driver,
                                praktor_process_view *view);
/* Close admission first, then at most 4096 shutdown steps within budget_ms.
 * Zero budget closes admission without a synchronous drain. EBUSY retains
 * all state. Repeated calls are safe. No per-tick/close/release allocations. */
int praktor_process_driver_close(praktor_process_driver *driver, uint32_t budget_ms);
/* Free only after native destroy, or transfer the C payload into a reservation
 * acquired BEFORE spawn. On failure the handle and all leases remain owned. */
int praktor_process_driver_release(praktor_process_driver *driver);
#ifdef __cplusplus
}
#endif
#endif
