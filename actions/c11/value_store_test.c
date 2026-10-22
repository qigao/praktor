#include "value_store.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; \
  } \
} while (0)

int main(void) {
  praktor_c11_value_store store = {0};
  char output[9];
  size_t output_size = 0u;
  int contains = 0;

  CHECK(praktor_c11_value_store_init(NULL, 1u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_init(&store, 0u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_init(&store, 2u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_init(&store, 2u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);

  CHECK(praktor_c11_value_store_set(&store, NULL, "x") ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_set(&store, "x", NULL) ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_set(&store, "", "x") ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_set(&store, "alpha", "one") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&store, "alpha", "two") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_get(&store, "alpha", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_OK);
  CHECK(output_size == 3u && strcmp(output, "two") == 0);

  CHECK(praktor_c11_value_store_set(&store, "alpha", "123456789") ==
        PRAKTOR_C11_VALUE_ENOBUFS);
  CHECK(praktor_c11_value_store_get(&store, "alpha", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_OK);
  CHECK(output_size == 3u && strcmp(output, "two") == 0);
  CHECK(praktor_c11_value_store_set(&store, "123456789", "x") ==
        PRAKTOR_C11_VALUE_ENOBUFS);

  CHECK(praktor_c11_value_store_set(&store, "beta", "2") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&store, "gamma", "3") ==
        PRAKTOR_C11_VALUE_ENOSPC);
  CHECK(praktor_c11_value_store_get(&store, "alpha", output, 3u,
                                    &output_size) == PRAKTOR_C11_VALUE_ENOBUFS);
  CHECK(output_size == 3u);
  CHECK(praktor_c11_value_store_contains(&store, "beta", &contains) ==
        PRAKTOR_C11_VALUE_OK && contains);
  CHECK(praktor_c11_value_store_erase(&store, "beta") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&store, "gamma", "3") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_get(&store, "missing", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_ENOTFOUND);
  CHECK(praktor_c11_value_store_size(&store) == 2u);

  praktor_c11_value_store_destroy(&store);
  CHECK(store.impl == NULL && praktor_c11_value_store_size(&store) == 0u);
  praktor_c11_value_store_destroy(&store);
  return 0;
}
