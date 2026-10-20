#include "tree_adapter.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
      return 1;                                                                \
    }                                                                          \
  } while (0)

enum { TEST_MAX_NODES = 8, TEST_MAX_STEPS = 4 };

typedef struct test_host_state {
  salts_bt_node_status steps[TEST_MAX_NODES][TEST_MAX_STEPS];
  size_t step_count[TEST_MAX_NODES];
  size_t next_step[TEST_MAX_NODES];
  size_t tick_calls[TEST_MAX_NODES];
  size_t halt_calls[TEST_MAX_NODES];
  int leaf_error_once[TEST_MAX_NODES];

  size_t enter_calls[TEST_MAX_NODES][TEST_MAX_NODES];
  size_t first_calls[TEST_MAX_NODES][TEST_MAX_NODES];
  size_t resume_calls[TEST_MAX_NODES][TEST_MAX_NODES];
  size_t step_calls[TEST_MAX_NODES][TEST_MAX_NODES];
  salts_bt_node_status step_status[TEST_MAX_NODES][TEST_MAX_NODES];
  size_t join_calls[TEST_MAX_NODES];
  salts_bt_node_status joined_status[TEST_MAX_NODES];
  size_t finish_calls[TEST_MAX_NODES][TEST_MAX_NODES];
  size_t abort_calls[TEST_MAX_NODES][TEST_MAX_NODES];
} test_host_state;

static int test_tick_leaf(void *user, size_t node_index,
                          salts_bt_node_status *out_status) {
  test_host_state *state = (test_host_state *)user;
  size_t step;
  if (state == NULL || out_status == NULL || node_index >= TEST_MAX_NODES ||
      state->step_count[node_index] == 0u)
    return SALTS_EINVAL;

  ++state->tick_calls[node_index];
  if (state->leaf_error_once[node_index] != SALTS_OK) {
    int status = state->leaf_error_once[node_index];
    state->leaf_error_once[node_index] = SALTS_OK;
    *out_status = SALTS_BT_FAILURE;
    return status;
  }

  step = state->next_step[node_index];
  if (step >= state->step_count[node_index]) step = state->step_count[node_index] - 1u;
  *out_status = state->steps[node_index][step];
  if (state->next_step[node_index] < state->step_count[node_index])
    ++state->next_step[node_index];
  return SALTS_OK;
}

static void test_halt_leaf(void *user, size_t node_index) {
  test_host_state *state = (test_host_state *)user;
  if (state != NULL && node_index < TEST_MAX_NODES)
    ++state->halt_calls[node_index];
}

static int test_parallel_enter(void *user, size_t node_index,
                               size_t child_index, bool first_activation) {
  test_host_state *state = (test_host_state *)user;
  if (state == NULL || node_index >= TEST_MAX_NODES ||
      child_index >= TEST_MAX_NODES)
    return SALTS_EINVAL;
  ++state->enter_calls[node_index][child_index];
  if (first_activation) ++state->first_calls[node_index][child_index];
  else ++state->resume_calls[node_index][child_index];
  return SALTS_OK;
}

static int test_parallel_step(void *user, size_t node_index,
                              size_t child_index,
                              salts_bt_node_status child_status) {
  test_host_state *state = (test_host_state *)user;
  if (state == NULL || node_index >= TEST_MAX_NODES ||
      child_index >= TEST_MAX_NODES)
    return SALTS_EINVAL;
  ++state->step_calls[node_index][child_index];
  state->step_status[node_index][child_index] = child_status;
  return SALTS_OK;
}

static int test_parallel_join(void *user, size_t node_index,
                              salts_bt_node_status aggregate_status) {
  test_host_state *state = (test_host_state *)user;
  if (state == NULL || node_index >= TEST_MAX_NODES) return SALTS_EINVAL;
  ++state->join_calls[node_index];
  state->joined_status[node_index] = aggregate_status;
  return SALTS_OK;
}

static int test_parallel_finish(void *user, size_t node_index,
                                size_t child_index,
                                salts_bt_node_status child_status) {
  test_host_state *state = (test_host_state *)user;
  (void)child_status;
  if (state == NULL || node_index >= TEST_MAX_NODES ||
      child_index >= TEST_MAX_NODES)
    return SALTS_EINVAL;
  ++state->finish_calls[node_index][child_index];
  return SALTS_OK;
}

static void test_parallel_abort(void *user, size_t node_index,
                                size_t child_index) {
  test_host_state *state = (test_host_state *)user;
  if (state != NULL && node_index < TEST_MAX_NODES &&
      child_index < TEST_MAX_NODES)
    ++state->abort_calls[node_index][child_index];
}

static void set_steps(test_host_state *state, size_t node_index,
                      const salts_bt_node_status *steps, size_t count) {
  size_t i;
  state->step_count[node_index] = count;
  for (i = 0u; i < count; ++i) state->steps[node_index][i] = steps[i];
}

static void set_host(praktor_c11_bt_host *host, test_host_state *state) {
  memset(host, 0, sizeof(*host));
  host->tick_leaf = test_tick_leaf;
  host->halt_leaf = test_halt_leaf;
  host->parallel_enter = test_parallel_enter;
  host->parallel_step = test_parallel_step;
  host->parallel_join = test_parallel_join;
  host->parallel_finish = test_parallel_finish;
  host->parallel_abort = test_parallel_abort;
  host->user = state;
}

static void default_tree_config(salts_bt_tree_config *config) {
  salts_bt_tree_config_init(config);
  config->max_nodes = TEST_MAX_NODES;
  config->max_depth = 8u;
}

static int test_sequence_preserves_running_child_state(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[3] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_SEQUENCE, 0u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status first[] = {SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
  const salts_bt_node_status second[] = {SALTS_BT_SUCCESS};
  test_host_state state = {0};
  praktor_c11_bt_host host;
  salts_bt_node_status result = SALTS_BT_FAILURE;

  default_tree_config(&config);
  set_steps(&state, 1u, first, 2u);
  set_steps(&state, 2u, second, 1u);
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 3u, &host) == SALTS_OK);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.tick_calls[1] == 1u);
  CHECK(state.tick_calls[2] == 0u);

  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_SUCCESS);
  CHECK(state.tick_calls[1] == 2u);
  CHECK(state.tick_calls[2] == 1u);
  CHECK(state.halt_calls[1] == 0u);
  CHECK(praktor_c11_bt_tree_destroy(&tree) == SALTS_OK);
  CHECK(tree.impl == NULL);
  return 0;
}

static int test_running_condition_is_halted_on_cancel(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[3] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_WHILE_DO, 3u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status running[] = {SALTS_BT_RUNNING};
  const salts_bt_node_status success[] = {SALTS_BT_SUCCESS};
  test_host_state state = {0};
  praktor_c11_bt_host host;
  salts_bt_node_status result = SALTS_BT_FAILURE;

  default_tree_config(&config);
  set_steps(&state, 1u, running, 1u);
  set_steps(&state, 2u, success, 1u);
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 3u, &host) == SALTS_OK);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.tick_calls[1] == 1u);
  CHECK(state.tick_calls[2] == 0u);

  CHECK(praktor_c11_bt_tree_cancel(&tree) == SALTS_OK);
  CHECK(state.halt_calls[1] == 1u);
  CHECK(state.halt_calls[2] == 0u);
  CHECK(praktor_c11_bt_tree_cancel(&tree) == SALTS_OK);
  CHECK(state.halt_calls[1] == 1u);
  CHECK(praktor_c11_bt_tree_destroy(&tree) == SALTS_OK);
  CHECK(state.halt_calls[1] == 1u);
  return 0;
}

static int test_while_do_condition_child_yields_each_iteration(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[3] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_WHILE_DO, 3u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status conditions[] = {
      SALTS_BT_SUCCESS, SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
  const salts_bt_node_status body[] = {SALTS_BT_SUCCESS};
  test_host_state state = {0};
  praktor_c11_bt_host host;
  salts_bt_node_status result = SALTS_BT_FAILURE;

  default_tree_config(&config);
  set_steps(&state, 1u, conditions, 3u);
  set_steps(&state, 2u, body, 1u);
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 3u, &host) == SALTS_OK);

  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.tick_calls[1] == 1u);
  CHECK(state.tick_calls[2] == 1u);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.tick_calls[1] == 2u);
  CHECK(state.tick_calls[2] == 2u);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_SUCCESS);
  CHECK(state.tick_calls[1] == 3u);
  CHECK(state.tick_calls[2] == 2u);
  CHECK(praktor_c11_bt_tree_destroy(&tree) == SALTS_OK);
  return 0;
}

static int test_parallel_context_lifecycle_and_cancel(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[3] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_PARALLEL, 0u, 0u, false, true},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false},
      {0u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status success[] = {SALTS_BT_SUCCESS};
  const salts_bt_node_status running[] = {SALTS_BT_RUNNING};
  test_host_state state = {0};
  praktor_c11_bt_host host;
  salts_bt_node_status result = SALTS_BT_FAILURE;

  default_tree_config(&config);
  set_steps(&state, 1u, success, 1u);
  set_steps(&state, 2u, running, 1u);
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 3u, &host) == SALTS_OK);

  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.join_calls[0] == 1u);
  CHECK(state.joined_status[0] == SALTS_BT_RUNNING);
  CHECK(state.finish_calls[0][0] == 1u);
  CHECK(state.finish_calls[0][1] == 0u);

  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_RUNNING);
  CHECK(state.enter_calls[0][1] == 2u);
  CHECK(state.first_calls[0][1] == 1u);
  CHECK(state.resume_calls[0][1] == 1u);
  CHECK(state.finish_calls[0][0] == 2u);
  CHECK(state.join_calls[0] == 2u);

  CHECK(praktor_c11_bt_tree_cancel(&tree) == SALTS_OK);
  CHECK(state.halt_calls[2] == 1u);
  CHECK(state.abort_calls[0][1] == 1u);
  CHECK(state.abort_calls[0][0] == 0u);
  CHECK(praktor_c11_bt_tree_destroy(&tree) == SALTS_OK);
  CHECK(state.halt_calls[2] == 1u);
  return 0;
}

static int test_leaf_callback_error_is_preserved_and_resets_activation(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[1] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status success[] = {SALTS_BT_SUCCESS};
  test_host_state state = {0};
  praktor_c11_bt_host host;
  salts_bt_node_status result = SALTS_BT_FAILURE;

  default_tree_config(&config);
  set_steps(&state, 0u, success, 1u);
  state.leaf_error_once[0] = SALTS_ENOMEM;
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 1u, &host) == SALTS_OK);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_ENOMEM);
  CHECK(praktor_c11_bt_tree_tick(&tree, &result) == SALTS_OK);
  CHECK(result == SALTS_BT_SUCCESS);
  CHECK(state.tick_calls[0] == 2u);
  CHECK(praktor_c11_bt_tree_destroy(&tree) == SALTS_OK);
  return 0;
}

static int test_invalid_parent_order_fails_before_callbacks(void) {
  praktor_c11_bt_tree tree = {0};
  salts_bt_tree_config config;
  praktor_c11_bt_node_desc nodes[2] = {
      {PRAKTOR_C11_BT_NO_PARENT, SALTS_BT_NODE_SEQUENCE, 0u, 0u, false, false},
      {1u, SALTS_BT_NODE_LEAF, 0u, 0u, false, false}};
  const salts_bt_node_status success[] = {SALTS_BT_SUCCESS};
  test_host_state state = {0};
  praktor_c11_bt_host host;

  default_tree_config(&config);
  set_steps(&state, 1u, success, 1u);
  set_host(&host, &state);
  CHECK(praktor_c11_bt_tree_init(&tree, &config, nodes, 2u, &host) == SALTS_EINVAL);
  CHECK(tree.impl == NULL);
  CHECK(state.tick_calls[1] == 0u);
  return 0;
}

int main(void) {
  if (test_sequence_preserves_running_child_state() != 0) return 1;
  if (test_running_condition_is_halted_on_cancel() != 0) return 1;
  if (test_while_do_condition_child_yields_each_iteration() != 0) return 1;
  if (test_parallel_context_lifecycle_and_cancel() != 0) return 1;
  if (test_leaf_callback_error_is_preserved_and_resets_activation() != 0) return 1;
  if (test_invalid_parent_order_fails_before_callbacks() != 0) return 1;
  puts("Praktor C11 BehaviorTree adapter tests passed.");
  return 0;
}
