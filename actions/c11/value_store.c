#include "value_store.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct praktor_c11_value_entry {
  char *key;
  char *value;
  size_t value_size;
  int occupied;
} praktor_c11_value_entry;

typedef struct praktor_c11_value_store_impl {
  size_t capacity;
  size_t max_key_bytes;
  size_t max_value_bytes;
  size_t size;
  praktor_c11_value_entry *entries;
  char *key_storage;
  char *value_storage;
} praktor_c11_value_store_impl;

static praktor_c11_value_store_impl *store_impl(
    const praktor_c11_value_store *store) {
  return store == NULL ? NULL
                       : (praktor_c11_value_store_impl *)store->impl;
}

/* Return SIZE_MAX for NULL; otherwise stop after max_bytes + 1 bytes. */
static size_t bounded_length(const char *text, size_t max_bytes) {
  size_t length = 0u;
  if (text == NULL) return SIZE_MAX;
  while (length <= max_bytes && text[length] != '\0') ++length;
  return length;
}

static praktor_c11_value_entry *find_entry(
    praktor_c11_value_store_impl *impl, const char *key, size_t key_size) {
  size_t i;
  for (i = 0u; i < impl->capacity; ++i) {
    praktor_c11_value_entry *entry = &impl->entries[i];
    if (entry->occupied && strlen(entry->key) == key_size &&
        memcmp(entry->key, key, key_size) == 0) {
      return entry;
    }
  }
  return NULL;
}

int praktor_c11_value_store_init(praktor_c11_value_store *store,
                                 size_t capacity,
                                 size_t max_key_bytes,
                                 size_t max_value_bytes) {
  praktor_c11_value_store_impl *impl;
  size_t i;

  if (store == NULL || store->impl != NULL || capacity == 0u ||
      max_key_bytes == 0u || max_value_bytes == 0u ||
      max_key_bytes == SIZE_MAX || max_value_bytes == SIZE_MAX ||
      capacity > SIZE_MAX / sizeof(praktor_c11_value_entry) ||
      capacity > SIZE_MAX / (max_key_bytes + 1u) ||
      capacity > SIZE_MAX / (max_value_bytes + 1u)) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }

  impl = (praktor_c11_value_store_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return PRAKTOR_C11_VALUE_ENOMEM;

  impl->entries = (praktor_c11_value_entry *)calloc(
      capacity, sizeof(*impl->entries));
  impl->key_storage = (char *)calloc(capacity, max_key_bytes + 1u);
  impl->value_storage = (char *)calloc(capacity, max_value_bytes + 1u);
  if (impl->entries == NULL || impl->key_storage == NULL ||
      impl->value_storage == NULL) {
    free(impl->entries);
    free(impl->key_storage);
    free(impl->value_storage);
    free(impl);
    return PRAKTOR_C11_VALUE_ENOMEM;
  }

  impl->capacity = capacity;
  impl->max_key_bytes = max_key_bytes;
  impl->max_value_bytes = max_value_bytes;
  for (i = 0u; i < capacity; ++i) {
    impl->entries[i].key = impl->key_storage + i * (max_key_bytes + 1u);
    impl->entries[i].value = impl->value_storage + i * (max_value_bytes + 1u);
  }
  store->impl = impl;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_clone(praktor_c11_value_store *destination,
                                  const praktor_c11_value_store *source) {
  const praktor_c11_value_store_impl *src = store_impl(source);
  praktor_c11_value_store_impl *dst;
  size_t i;
  if (destination == NULL || source == NULL || src == NULL ||
      destination == source || destination->impl != NULL) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  if (praktor_c11_value_store_init(destination, src->capacity,
                                   src->max_key_bytes,
                                   src->max_value_bytes) != PRAKTOR_C11_VALUE_OK) {
    return PRAKTOR_C11_VALUE_ENOMEM;
  }
  dst = store_impl(destination);
  for (i = 0u; i < src->capacity; ++i) {
    const praktor_c11_value_entry *from = &src->entries[i];
    praktor_c11_value_entry *to = &dst->entries[i];
    if (!from->occupied) continue;
    memcpy(to->key, from->key, strlen(from->key) + 1u);
    memcpy(to->value, from->value, from->value_size + 1u);
    to->value_size = from->value_size;
    to->occupied = 1;
    ++dst->size;
  }
  return PRAKTOR_C11_VALUE_OK;
}

void praktor_c11_value_store_destroy(praktor_c11_value_store *store) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  if (impl == NULL) return;
  free(impl->entries);
  free(impl->key_storage);
  free(impl->value_storage);
  free(impl);
  store->impl = NULL;
}

int praktor_c11_value_store_set(praktor_c11_value_store *store,
                                const char *key, const char *value) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size, value_size, i;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL || value == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;

  key_size = bounded_length(key, impl->max_key_bytes);
  value_size = bounded_length(value, impl->max_value_bytes);
  if (key_size == 0u || key_size == SIZE_MAX)
    return PRAKTOR_C11_VALUE_EINVAL;
  if (key_size > impl->max_key_bytes ||
      value_size > impl->max_value_bytes) {
    return PRAKTOR_C11_VALUE_ENOBUFS;
  }

  entry = find_entry(impl, key, key_size);
  if (entry == NULL) {
    for (i = 0u; i < impl->capacity; ++i) {
      if (!impl->entries[i].occupied) {
        entry = &impl->entries[i];
        break;
      }
    }
    if (entry == NULL) return PRAKTOR_C11_VALUE_ENOSPC;
    memcpy(entry->key, key, key_size);
    entry->key[key_size] = '\0';
    entry->occupied = 1;
    ++impl->size;
  }

  /* All input/capacity validation precedes mutation: failed update is atomic. */
  memcpy(entry->value, value, value_size);
  entry->value[value_size] = '\0';
  entry->value_size = value_size;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_get(const praktor_c11_value_store *store,
                                const char *key, char *out_value,
                                size_t out_capacity, size_t *out_size) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL || out_size == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }

  entry = find_entry(impl, key, key_size);
  if (entry == NULL) return PRAKTOR_C11_VALUE_ENOTFOUND;
  *out_size = entry->value_size;
  if (out_value == NULL || out_capacity <= entry->value_size)
    return PRAKTOR_C11_VALUE_ENOBUFS;
  memcpy(out_value, entry->value, entry->value_size + 1u);
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_contains(const praktor_c11_value_store *store,
                                     const char *key, int *out_contains) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;

  if (impl == NULL || key == NULL || out_contains == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  *out_contains = find_entry(impl, key, key_size) != NULL;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_erase(praktor_c11_value_store *store,
                                  const char *key) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL) return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  entry = find_entry(impl, key, key_size);
  if (entry == NULL) return PRAKTOR_C11_VALUE_ENOTFOUND;

  memset(entry->key, 0, impl->max_key_bytes + 1u);
  memset(entry->value, 0, impl->max_value_bytes + 1u);
  entry->value_size = 0u;
  entry->occupied = 0;
  --impl->size;
  return PRAKTOR_C11_VALUE_OK;
}

size_t praktor_c11_value_store_size(const praktor_c11_value_store *store) {
  const praktor_c11_value_store_impl *impl =
      store == NULL ? NULL : (const praktor_c11_value_store_impl *)store->impl;
  return impl == NULL ? 0u : impl->size;
}
