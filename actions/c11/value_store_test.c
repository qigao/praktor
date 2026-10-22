#include "value_store.h"
#include <tinytest.h>

#include <stdio.h>
#include <string.h>

static int expect_value(const praktor_c11_value_store *store,
                         const char *key, const char *expected) {
  char output[64];
  size_t size = 0u;
  if (praktor_c11_value_store_get(store, key, output, sizeof(output), &size) !=
      PRAKTOR_C11_VALUE_OK) return 0;
  return size == strlen(expected) && strcmp(output, expected) == 0;
}

static void test_clone_is_independent_snapshot(void) {
  praktor_c11_value_store baseline = {0};
  praktor_c11_value_store branch = {0};
  check(praktor_c11_value_store_init(&baseline, 3u, 16u, 32u) ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&baseline, "input", "seed") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&baseline, "stable", "unchanged") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_clone(&branch, &baseline) ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_clone(&branch, &baseline) ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set(&branch, "input", "branch-value") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&branch, "branch-only", "local") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_erase(&branch, "stable") ==
        PRAKTOR_C11_VALUE_OK);
  check(expect_value(&baseline, "input", "seed"));
  check(expect_value(&baseline, "stable", "unchanged"));
  { int contains_branch_only = 1;
    check(praktor_c11_value_store_contains(&baseline, "branch-only",
                                            &contains_branch_only) ==
          PRAKTOR_C11_VALUE_OK);
    check(contains_branch_only == 0);
  }
  check(!expect_value(&branch, "stable", "unchanged"));
  check(expect_value(&branch, "input", "branch-value"));
  praktor_c11_value_store_destroy(&branch);
  praktor_c11_value_store_destroy(&baseline);
}

static void test_transaction_commit_conflict_and_abort(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn a = {0}, b = {0}, c = {0};
  check(praktor_c11_value_store_init(&target, 4u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "left", "base") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "right", "base") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&b, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&a, "left", "A") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&b, "right", "B") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit(&a) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit(&b) == PRAKTOR_C11_VALUE_OK);
  check(expect_value(&target, "left", "A") && expect_value(&target, "right", "B"));
  check(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&c, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&a, "left", "winner") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&c, "left", "loser") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit(&a) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit(&c) == PRAKTOR_C11_VALUE_ECONFLICT);
  check(expect_value(&target, "left", "winner"));
  praktor_c11_value_txn_abort(&c);
  check(praktor_c11_value_txn_begin(&a, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&a, "left", "discard") == PRAKTOR_C11_VALUE_OK);
  praktor_c11_value_txn_abort(&a);
  check(expect_value(&target, "left", "winner"));
  praktor_c11_value_store_destroy(&target);
}

static void test_transaction_capacity_failure_is_atomic(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn txn = {0};
  check(praktor_c11_value_store_init(&target, 4u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "base-a", "A") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "base-b", "B") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&txn, &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&txn, "branch-key", "branch") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "external-c", "C") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "external-d", "D") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit(&txn) == PRAKTOR_C11_VALUE_ENOSPC);
  check(praktor_c11_value_store_size(&target) == 4u);
  check(expect_value(&target, "base-a", "A"));
  check(expect_value(&target, "base-b", "B"));
  check(expect_value(&target, "external-c", "C"));
  check(expect_value(&target, "external-d", "D"));
  { int contains = 1;
    check(praktor_c11_value_store_contains(&target, "branch-key", &contains) ==
          PRAKTOR_C11_VALUE_OK);
    check(contains == 0);
  }
  praktor_c11_value_txn_abort(&txn);
  praktor_c11_value_store_destroy(&target);
}

static void test_transaction_group_commit_is_atomic(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn branches[2] = {{0}, {0}};
  check(praktor_c11_value_store_init(&target, 4u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "seed", "base") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[0], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[1], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[0], "left", "L") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[1], "right", "R") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit_group(branches, 2u) == PRAKTOR_C11_VALUE_OK);
  check(expect_value(&target, "left", "L") && expect_value(&target, "right", "R"));
  check(branches[0].impl == NULL && branches[1].impl == NULL);
  praktor_c11_value_store_destroy(&target);
}
static void test_transaction_group_conflict_publishes_nothing(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn branches[2] = {{0}, {0}};
  check(praktor_c11_value_store_init(&target, 3u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "shared", "base") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[0], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[1], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[0], "shared", "left") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[1], "shared", "right") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[1], "new", "must-not-leak") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit_group(branches, 2u) == PRAKTOR_C11_VALUE_ECONFLICT);
  check(expect_value(&target, "shared", "base"));
  { int present = 1; check(praktor_c11_value_store_contains(&target, "new", &present) == PRAKTOR_C11_VALUE_OK); check(present == 0); }
  praktor_c11_value_txn_abort(&branches[0]); praktor_c11_value_txn_abort(&branches[1]);
  praktor_c11_value_store_destroy(&target);
}
static void test_transaction_group_capacity_failure_is_atomic(void) {
  praktor_c11_value_store target = {0};
  praktor_c11_value_txn branches[2] = {{0}, {0}};
  check(praktor_c11_value_store_init(&target, 3u, 16u, 32u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "seed", "base") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[0], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_begin(&branches[1], &target) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[0], "left", "L") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_set(&branches[1], "right", "R") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&target, "external", "E") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_txn_commit_group(branches, 2u) == PRAKTOR_C11_VALUE_ENOSPC);
  check(praktor_c11_value_store_size(&target) == 2u);
  check(expect_value(&target, "seed", "base"));
  check(expect_value(&target, "external", "E"));
  { int present = 1;
    check(praktor_c11_value_store_contains(&target, "left", &present) == PRAKTOR_C11_VALUE_OK);
    check(present == 0);
    check(praktor_c11_value_store_contains(&target, "right", &present) == PRAKTOR_C11_VALUE_OK);
    check(present == 0);
  }
  praktor_c11_value_txn_abort(&branches[0]);
  praktor_c11_value_txn_abort(&branches[1]);
  praktor_c11_value_store_destroy(&target);
}

static void test_store_bounds(void) {
  praktor_c11_value_store store = {0};
  char output[9];
  size_t output_size = 0u;
  int contains = 0;

  check(praktor_c11_value_store_init(NULL, 1u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_init(&store, 0u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_init(&store, 2u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_init(&store, 2u, 8u, 8u) ==
        PRAKTOR_C11_VALUE_EINVAL);

  check(praktor_c11_value_store_set(&store, NULL, "x") ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set(&store, "x", NULL) ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set(&store, "", "x") ==
        PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set(&store, "alpha", "one") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&store, "alpha", "two") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_get(&store, "alpha", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_OK);
  check(output_size == 3u && strcmp(output, "two") == 0);

  check(praktor_c11_value_store_set(&store, "alpha", "123456789") ==
        PRAKTOR_C11_VALUE_ENOBUFS);
  check(praktor_c11_value_store_get(&store, "alpha", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_OK);
  check(output_size == 3u && strcmp(output, "two") == 0);
  check(praktor_c11_value_store_set(&store, "123456789", "x") ==
        PRAKTOR_C11_VALUE_ENOBUFS);

  check(praktor_c11_value_store_set(&store, "beta", "2") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&store, "gamma", "3") ==
        PRAKTOR_C11_VALUE_ENOSPC);
  check(praktor_c11_value_store_get(&store, "alpha", output, 3u,
                                    &output_size) == PRAKTOR_C11_VALUE_ENOBUFS);
  check(output_size == 3u);
  check(praktor_c11_value_store_contains(&store, "beta", &contains) ==
        PRAKTOR_C11_VALUE_OK && contains);
  check(praktor_c11_value_store_erase(&store, "beta") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&store, "gamma", "3") ==
        PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_get(&store, "missing", output, sizeof(output),
                                    &output_size) == PRAKTOR_C11_VALUE_ENOTFOUND);
  check(praktor_c11_value_store_size(&store) == 2u);

  praktor_c11_value_store_destroy(&store);
  check(store.impl == NULL && praktor_c11_value_store_size(&store) == 0u);
  praktor_c11_value_store_destroy(&store);
}

static void test_many_writes_preflight(void) {
  praktor_c11_value_store store = {0};
  praktor_c11_value_write writes[2] = {{"old", "changed"}, {"new", "value"}};
  check(praktor_c11_value_store_init(&store, 2u, 8u, 8u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&store, "old", "baseline") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set(&store, "full", "occupied") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_ENOSPC);
  check(expect_value(&store, "old", "baseline"));
  check(expect_value(&store, "full", "occupied"));
  check(praktor_c11_value_store_erase(&store, "full") == PRAKTOR_C11_VALUE_OK);
  writes[1].key = "too-long-key";
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_ENOBUFS);
  check(expect_value(&store, "old", "baseline") && praktor_c11_value_store_size(&store) == 1u);
  writes[1].key = "new"; writes[1].value = "too-long-value";
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_ENOBUFS);
  check(expect_value(&store, "old", "baseline"));
  writes[1].key = "old"; writes[1].value = "value";
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_EINVAL);
  check(expect_value(&store, "old", "baseline"));
  writes[1].key = "";
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_EINVAL);
  writes[1].key = NULL;
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_EINVAL);
  writes[1].key = "new"; writes[1].value = NULL;
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set_many(&store, NULL, 1u) == PRAKTOR_C11_VALUE_EINVAL);
  check(praktor_c11_value_store_set_many(&store, NULL, 0u) == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_set_many(NULL, NULL, 0u) == PRAKTOR_C11_VALUE_EINVAL);
  check(expect_value(&store, "old", "baseline") && praktor_c11_value_store_size(&store) == 1u);
  writes[1].value = "12345678"; /* exact value bound */
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_OK);
  check(expect_value(&store, "old", "changed") && expect_value(&store, "new", "12345678"));
  writes[0].value = "next"; writes[1].value = "updated";
  check(praktor_c11_value_store_set_many(&store, writes, 2u) == PRAKTOR_C11_VALUE_OK);
  check(expect_value(&store, "old", "next") && expect_value(&store, "new", "updated"));
  check(praktor_c11_value_store_size(&store) == 2u);
  praktor_c11_value_store_destroy(&store);
}

spec("C11 value store") {
  it("preflights every key, value and slot before batch publication") { test_many_writes_preflight(); }
  it("enforces bounds and preserves failed writes") { test_store_bounds(); }
  it("clones an independent snapshot") { test_clone_is_independent_snapshot(); }
  it("commits disjoint transactions and rejects conflicts") { test_transaction_commit_conflict_and_abort(); }
  it("preserves target on transaction capacity exhaustion") { test_transaction_capacity_failure_is_atomic(); }
  it("publishes branch groups atomically") { test_transaction_group_commit_is_atomic(); }
  it("publishes nothing on branch conflict") { test_transaction_group_conflict_publishes_nothing(); }
  it("publishes nothing on group capacity exhaustion") { test_transaction_group_capacity_failure_is_atomic(); }
}
