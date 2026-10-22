#ifndef PRAKTOR_C11_NATIVE_HOST_H
#define PRAKTOR_C11_NATIVE_HOST_H
#include "tree_adapter.h"
#include "value_store.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Single-driver owner: fixed-capacity values and preallocated parameter scratch.
 * No legacy objects or per-tick allocations. Borrow hooks only until destroy.
 * Parallel-branch requests and unsupported actions return ENOTSUP; there is no implicit legacy dispatch. */
typedef struct praktor_c11_native_host { void *impl; } praktor_c11_native_host;
/* Zero max_running_leaves admits stateless leaves and zero-duration Sleep.
 * Positive Sleep durations require a preallocated slot. NULL now_ns selects
 * Salts' monotonic clock; custom clock/user storage is borrowed for Host life. */
typedef struct praktor_c11_native_host_config {
  size_t value_capacity;
  size_t max_key_bytes;
  size_t max_value_bytes;
  size_t max_running_leaves;
  uint64_t (*now_ns)(void *user);
  void *clock_user;
} praktor_c11_native_host_config;
int praktor_c11_native_host_init_with_config(
    praktor_c11_native_host *host, const praktor_c11_native_host_config *config);
/* Convenience initializer with no slots for positive-duration Sleep. */
int praktor_c11_native_host_init(praktor_c11_native_host *host, size_t capacity,
                                 size_t max_key_bytes, size_t max_value_bytes);
void praktor_c11_native_host_destroy(praktor_c11_native_host *host);
int praktor_c11_native_host_bind(praktor_c11_native_host *host,
                                 praktor_c11_bt_host *out_hooks);
/* Borrowed value store. Do not destroy it independently. */
praktor_c11_value_store *praktor_c11_native_host_values(praktor_c11_native_host *host);
#ifdef __cplusplus
}
#endif
#endif
