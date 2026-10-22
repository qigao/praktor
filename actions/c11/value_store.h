#ifndef PRAKTOR_C11_VALUE_STORE_H
#define PRAKTOR_C11_VALUE_STORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct praktor_c11_value_store {
  void *impl;
} praktor_c11_value_store;

typedef struct praktor_c11_value_txn {
  void *impl;
} praktor_c11_value_txn;

typedef enum praktor_c11_value_status {
  PRAKTOR_C11_VALUE_OK = 0,
  PRAKTOR_C11_VALUE_EINVAL = 1,
  PRAKTOR_C11_VALUE_ENOMEM = 2,
  PRAKTOR_C11_VALUE_ENOSPC = 3,
  PRAKTOR_C11_VALUE_ENOBUFS = 4,
  PRAKTOR_C11_VALUE_ENOTFOUND = 5,
  PRAKTOR_C11_VALUE_ECONFLICT = 6
} praktor_c11_value_status;

/*
 * Initialize a zero-initialized store. Capacity and maximum key/value byte
 * lengths are fixed for the store lifetime; all backing storage is allocated
 * here. This API is single-driver and requires external synchronization if
 * shared between threads. A failed set never changes an existing value.
 */
int praktor_c11_value_store_init(praktor_c11_value_store *store,
                                 size_t capacity,
                                 size_t max_key_bytes,
                                 size_t max_value_bytes);
void praktor_c11_value_store_destroy(praktor_c11_value_store *store);
/* Deep-copy an immutable baseline into an independent store with equal bounds. */
int praktor_c11_value_store_clone(praktor_c11_value_store *destination,
                                  const praktor_c11_value_store *source);
int praktor_c11_value_store_set(praktor_c11_value_store *store,
                                const char *key, const char *value);
int praktor_c11_value_store_get(const praktor_c11_value_store *store,
                                const char *key, char *out_value,
                                size_t out_capacity, size_t *out_size);
int praktor_c11_value_store_contains(const praktor_c11_value_store *store,
                                     const char *key, int *out_contains);
int praktor_c11_value_store_erase(praktor_c11_value_store *store,
                                  const char *key);
size_t praktor_c11_value_store_size(const praktor_c11_value_store *store);

/*
 * Single-driver optimistic transaction. begin captures a bounded baseline;
 * writes are isolated in a private snapshot. commit detects per-key write/write
 * conflicts, preflights capacity on a staged copy, then publishes atomically.
 * A failed commit leaves the target unchanged and the transaction active so
 * the caller can abort or retry. Unrelated key updates do not conflict.
 */
int praktor_c11_value_txn_begin(praktor_c11_value_txn *txn, praktor_c11_value_store *target);
int praktor_c11_value_txn_set(praktor_c11_value_txn *txn, const char *key, const char *value);
int praktor_c11_value_txn_erase(praktor_c11_value_txn *txn, const char *key);
int praktor_c11_value_txn_commit(praktor_c11_value_txn *txn);
void praktor_c11_value_txn_abort(praktor_c11_value_txn *txn);
/* All-or-nothing commit of disjoint transactions sharing one target. */
int praktor_c11_value_txn_commit_group(praktor_c11_value_txn *txns, size_t count);

#ifdef __cplusplus
}
#endif

#endif
