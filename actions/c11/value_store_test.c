#include "value_store.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; \
  } \
} while (0)

static int expect_value(const praktor_c11_value_store *store,
                         const char *key, const char *expected) {
  char output[64];
  size_t size = 0u;
  if (praktor_c11_value_store_get(store, key, output, sizeof(output), &size) !=
      PRAKTOR_C11_VALUE_OK) return 0;
  return size == strlen(expected) && strcmp(output, expected) == 0;
}

static int test_clone_is_independent_snapshot(void) {
  praktor_c11_value_store baseline = {0};
  praktor_c11_value_store branch = {0};
  CHECK(praktor_c11_value_store_init(&baseline, 3u, 16u, 32u) ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&baseline, "input", "seed") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&baseline, "stable", "unchanged") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_clone(&branch, &baseline) ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_clone(&branch, &baseline) ==
        PRAKTOR_C11_VALUE_EINVAL);
  CHECK(praktor_c11_value_store_set(&branch, "input", "branch-value") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&branch, "branch-only", "local") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_erase(&branch, "stable") ==
        PRAKTOR_C11_VALUE_OK);
  CHECK(expect_value(&baseline, "input", "seed"));
  CHECK(expect_value(&baseline, "stable", "unchanged"));
  { int contains_branch_only = 1;
    CHECK(praktor_c11_value_store_contains(&baseline, "branch-only",
                                            &contains_branch_only) ==
          PRAKTOR_C11_VALUE_OK);
    CHECK(contains_branch_only == 0);
  }
  CHECK(!expect_value(&branch, "stable", "unchanged"));
  CHECK(expect_value(&branch, "input", "branch-value"));
  praktor_c11_value_store_destroy(&branch);
  praktor_c11_value_store_destroy(&baseline);
  return 0;
}

static int test_transaction_commit_conflict_and_abort(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn a = {0}, b = {0}, c = {0};
  CHECK(praktor_c11_value_store_init(&target, 4u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "left", "base") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "right", "base") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_begin(&b, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&a, "left", "A") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&b, "right", "B") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_commit(&a) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_commit(&b) == PRAKTOR_C11_VALUE_OK);
  CHECK(expect_value(&target, "left", "A") && expect_value(&target, "right", "B"));
  CHECK(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_begin(&c, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&a, "left", "winner") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&c, "left", "loser") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_commit(&a) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_commit(&c) == PRAKTOR_C11_VALUE_ECONFLICT);
  CHECK(expect_value(&target, "left", "winner"));
  praktor_c11_value_txn_abort(&c);
  CHECK(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&a, "left", "discard") == PRAKTOR_C11_VALUE_OK);
  praktor_c11_value_txn_abort(&a);
  CHECK(expect_value(&target, "left", "winner"));
  praktor_c11_value_store_destroy(&target);
  return 0;
}

static int test_transaction_capacity_failure_is_atomic(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn txn = {0};
  CHECK(praktor_c11_value_store_init(&target, 4u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "base-a", "A") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "base-b", "B") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_begin(&txn, &target) == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_set(&txn, "branch-key", "branch") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "external-c", "C") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_store_set(&target, "external-d", "D") == PRAKTOR_C11_VALUE_OK);
  CHECK(praktor_c11_value_txn_commit(&txn) == PRAKTOR_C11_VALUE_ENOSPC);
  CHECK(praktor_c11_value_store_size(&target) == 4u);
  CHECK(expect_value(&target, "base-a", "A"));
  CHECK(expect_value(&target, "base-b", "B"));
  CHECK(expect_value(&target, "external-c", "C"));
  CHECK(expect_value(&target, "external-d", "D"));
  { int contains = 1;
    CHECK(praktor_c11_value_store_contains(&target, "branch-key", &contains) ==
          PRAKTOR_C11_VALUE_OK);
    CHECK(contains == 0);
  }
  praktor_c11_value_txn_abort(&txn);
  praktor_c11_value_store_destroy(&target);
  return 0;
}

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

  CHECK(test_clone_is_independent_snapshot() == 0);
  CHECK(test_transaction_commit_conflict_and_abort() == 0);
  CHECK(test_transaction_capacity_failure_is_atomic() == 0);
  praktor_c11_value_store_destroy(&store);
  CHECK(store.impl == NULL && praktor_c11_value_store_size(&store) == 0u);
  praktor_c11_value_store_destroy(&store);
  return 0;
}
