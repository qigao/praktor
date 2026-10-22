#ifndef PRAKTOR_C11_VALUE_STORE_H
#define PRAKTOR_C11_VALUE_STORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct praktor_c11_value_store {
  void *impl;
} praktor_c11_value_store;

typedef enum praktor_c11_value_status {
  PRAKTOR_C11_VALUE_OK = 0,
  PRAKTOR_C11_VALUE_EINVAL = 1,
  PRAKTOR_C11_VALUE_ENOMEM = 2,
  PRAKTOR_C11_VALUE_ENOSPC = 3,
  PRAKTOR_C11_VALUE_ENOBUFS = 4,
  PRAKTOR_C11_VALUE_ENOTFOUND = 5
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

#ifdef __cplusplus
}
#endif

#endif
