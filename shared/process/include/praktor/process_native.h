#ifndef PRAKTOR_PROCESS_NATIVE_H
#define PRAKTOR_PROCESS_NATIVE_H
#include "process_control.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Persistent application control, NOT the Shell/Program owned-process backend.
 * Windows: bounded UTF-16 paths/command line (32768 units), UTF-8 metadata
 * (131072 bytes), diagnostics (1024 bytes), one failed-admission cleanup slot.
 * All storage is allocated at init; callbacks do not allocate. A successful
 * application outlives this host. Other platforms explicitly report unsupported.
 * Single driver: no calls, including close, may overlap. */
typedef struct praktor_process_native { void *impl; } praktor_process_native;
int praktor_process_native_init(praktor_process_native *native);
int praktor_process_native_bind(praktor_process_native *native,
                                praktor_process_control_host *out);
/* Close admission, recover failed startup, then free only after actual exit.
 * EBUSY/EIO keeps the handle and storage owned for retry. Budget zero polls.
 * Bound hooks and returned strings are invalid after successful close.
 * This does NOT stop applications whose startup succeeded. */
int praktor_process_native_close(praktor_process_native *native, uint32_t budget_ms);
/* Explicit synchronous compatibility teardown. Permanent native faults may
 * block; it never abandons a failed-start process or its owned handle. */
void praktor_process_native_destroy_join(praktor_process_native *native);
praktor_process_backend_result praktor_process_native_match_candidate(
    praktor_process_native *native, const char *configured, const char *canonical,
    uint32_t candidate_session, uint32_t current_session, bool *matches);
praktor_process_backend_result praktor_process_native_match_snapshot(
    praktor_process_native *native, const praktor_process_snapshot *expected,
    const praktor_process_snapshot *observed, uint32_t current_session, bool *matches);
/* Borrowed Windows process handle; only a signaled object proves exit after
 * evidence loss. One bounded 25 ms probe, preserving errors on live/invalid
 * objects. Also exposed for native contract tests; does not close the handle. */
praktor_process_backend_result praktor_process_native_observe_failure(
    praktor_process_native *native, void *process, uint32_t pid, uint32_t error,
    const char *message, praktor_process_snapshot *out);
#ifdef __cplusplus
}
#endif
#endif
