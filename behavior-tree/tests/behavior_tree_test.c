#include <salts/behavior_tree.h>
#include <salts/clock.h>
#include <salts/thread.h>

#include "tinytest.h"

typedef struct leaf_probe {
  const salts_bt_node_status *steps;
  size_t step_count;
  size_t calls;
} leaf_probe;

static salts_bt_node_status probe_leaf(void *user) {
  leaf_probe *probe = (leaf_probe *)user;
  salts_bt_node_status status;
  if (probe == NULL || probe->steps == NULL || probe->step_count == 0u)
    return SALTS_BT_FAILURE;
  status = probe->steps[probe->calls < probe->step_count
                            ? probe->calls
                            : probe->step_count - 1u];
  ++probe->calls;
  return status;
}

static void init_tree(salts_bt_tree *tree, size_t max_nodes, size_t max_depth) {
  salts_bt_tree_config config = {0};
  salts_bt_tree_config_init(&config);
  config.max_nodes = max_nodes;
  config.max_depth = max_depth;
  check_equal(salts_bt_tree_init(tree, &config), SALTS_OK);
}


typedef struct running_leaf_state {
  salts_bt_tree *tree;
  size_t tick_calls;
  size_t halt_calls;
  int reentrant_reset_status;
} running_leaf_state;

static salts_bt_node_status always_running_leaf(void *user) {
  running_leaf_state *state = (running_leaf_state *)user;
  ++state->tick_calls;
  return SALTS_BT_RUNNING;
}

static void halt_running_leaf(void *user) {
  running_leaf_state *state = (running_leaf_state *)user;
  ++state->halt_calls;
  state->reentrant_reset_status = salts_bt_tree_reset(state->tree);
}

typedef struct condition_probe {
  const salts_bt_node_status *steps;
  size_t step_count;
  size_t calls;
  int status;
} condition_probe;

static int probe_condition(void *user, salts_bt_node_status *out_condition_status) {
  condition_probe *probe = (condition_probe *)user;
  if (probe == NULL || out_condition_status == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_condition_status = probe->steps[probe->calls < probe->step_count
                                           ? probe->calls
                                           : probe->step_count - 1u];
  ++probe->calls;
  return probe->status;
}

typedef struct selector_probe {
  const size_t *steps;
  size_t step_count;
  size_t calls;
} selector_probe;

static int probe_selector(void *user, size_t *out_index) {
  selector_probe *probe = (selector_probe *)user;
  if (probe == NULL || out_index == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_index = probe->steps[probe->calls < probe->step_count
                                ? probe->calls
                                : probe->step_count - 1u];
  ++probe->calls;
  return SALTS_OK;
}


typedef struct parallel_probe {
  size_t enter_calls[4];
  size_t first_activations[4];
  size_t resumes[4];
  size_t step_calls[4];
  size_t join_calls;
  size_t finish_calls[4];
  size_t abort_calls[4];
  salts_bt_node_status step_status[4];
  salts_bt_node_status terminal_status[4];
  int enter_status[4];
  int step_return[4];
  int join_return;
  salts_bt_node_status joined_status;
  int finish_status[4];
} parallel_probe;

static int parallel_enter(void *user, size_t child_index,
                          bool first_activation) {
  parallel_probe *probe = (parallel_probe *)user;
  if (probe == NULL || child_index >= 4u) return SALTS_EINVAL;
  ++probe->enter_calls[child_index];
  if (first_activation) ++probe->first_activations[child_index];
  else ++probe->resumes[child_index];
  return probe->enter_status[child_index];
}

static int parallel_step(void *user, size_t child_index,
                         salts_bt_node_status child_status) {
  parallel_probe *probe = (parallel_probe *)user;
  if (probe == NULL || child_index >= 4u) return SALTS_EINVAL;
  ++probe->step_calls[child_index];
  probe->step_status[child_index] = child_status;
  return probe->step_return[child_index];
}

static int parallel_join(void *user,
                         salts_bt_node_status parallel_status) {
  parallel_probe *probe = (parallel_probe *)user;
  if (probe == NULL) return SALTS_EINVAL;
  ++probe->join_calls;
  probe->joined_status = parallel_status;
  return probe->join_return;
}

static int parallel_finish(void *user, size_t child_index,
                           salts_bt_node_status child_status) {
  parallel_probe *probe = (parallel_probe *)user;
  if (probe == NULL || child_index >= 4u) return SALTS_EINVAL;
  ++probe->finish_calls[child_index];
  probe->terminal_status[child_index] = child_status;
  return probe->finish_status[child_index];
}

static void parallel_abort(void *user, size_t child_index) {
  parallel_probe *probe = (parallel_probe *)user;
  if (probe != NULL && child_index < 4u) ++probe->abort_calls[child_index];
}

static salts_bt_node_status invalid_status_leaf(void *user) {
  (void)user;
  return (salts_bt_node_status)99;
}

typedef struct dynamic_parameter_probe {
  size_t calls;
  size_t limit;
  uint64_t duration_ns;
  int return_status;
} dynamic_parameter_probe;

static int resolve_dynamic_parameters(void *user, salts_bt_node_id node_id,
                                      salts_bt_node_parameters *out_params) {
  dynamic_parameter_probe *probe = (dynamic_parameter_probe *)user;
  (void)node_id;
  if (probe == NULL || out_params == NULL) return SALTS_EINVAL;
  ++probe->calls;
  if (probe->return_status != SALTS_OK) return probe->return_status;
  out_params->limit = probe->limit;
  out_params->duration_ns = probe->duration_ns;
  return SALTS_OK;
}

typedef struct pipeline_probe {
  size_t prepare_calls;
  size_t complete_calls;
  size_t last_prepared_child;
  size_t last_completed_child;
  salts_bt_node_status last_completed_status;
  size_t abort_calls;
  size_t last_aborted_child;
  int status;
} pipeline_probe;

static int pipeline_prepare(void *user, size_t child_index) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->prepare_calls;
  probe->last_prepared_child = child_index;
  return probe->status;
}

static int pipeline_complete(void *user, size_t child_index,
                             salts_bt_node_status child_status) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->complete_calls;
  probe->last_completed_child = child_index;
  probe->last_completed_status = child_status;
  return probe->status;
}

static void pipeline_abort(void *user, size_t child_index) {
  pipeline_probe *probe = (pipeline_probe *)user;
  ++probe->abort_calls;
  probe->last_aborted_child = child_index;
}

typedef struct entry_changed_probe {
  const bool *steps;
  size_t step_count;
  size_t calls;
  int status;
} entry_changed_probe;

static int entry_changed(void *user, bool *out_changed) {
  entry_changed_probe *probe = (entry_changed_probe *)user;
  if (probe == NULL || out_changed == NULL ||
      probe->steps == NULL || probe->step_count == 0u)
    return SALTS_EINVAL;
  *out_changed = probe->steps[probe->calls < probe->step_count
                                  ? probe->calls
                                  : probe->step_count - 1u];
  ++probe->calls;
  return probe->status;
}

typedef struct queue_probe {
  size_t remaining;
  size_t begin_calls;
  size_t finish_calls;
  size_t abort_calls;
} queue_probe;

static int queue_begin(void *user, bool *out_has_item) {
  queue_probe *probe = (queue_probe *)user;
  if (probe == NULL || out_has_item == NULL) return SALTS_EINVAL;
  ++probe->begin_calls;
  *out_has_item = probe->remaining > 0u;
  if (*out_has_item) --probe->remaining;
  return SALTS_OK;
}

static int queue_finish(void *user, bool *out_has_more) {
  queue_probe *probe = (queue_probe *)user;
  if (probe == NULL || out_has_more == NULL) return SALTS_EINVAL;
  ++probe->finish_calls;
  *out_has_more = probe->remaining > 0u;
  return SALTS_OK;
}

static void queue_abort(void *user) {
  queue_probe *probe = (queue_probe *)user;
  ++probe->abort_calls;
}

spec("Salts C11 Behavior Tree") {
  it("resumes a Sequence at its RUNNING child and resets after terminal success") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status first_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 2u, 0u};
    leaf_probe second_probe = {second_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_true(root != first);
    check_true(first != second);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(first_probe.calls, 1u);
    check_equal(second_probe.calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 2u);
    check_equal(second_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 3u);
    check_equal(second_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_true(tree.impl == NULL);
  }

  it("advances a Fallback after FAILURE and resumes the RUNNING alternative") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status fail_steps[] = {SALTS_BT_FAILURE};
    const salts_bt_node_status next_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe fail_probe = {fail_steps, 1u, 0u};
    leaf_probe next_probe = {next_steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FALLBACK, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &fail_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &next_probe, &second), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(fail_probe.calls, 1u);
    check_equal(next_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(fail_probe.calls, 1u);
    check_equal(next_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("enforces node capacity and rejects invalid topology") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id extra = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    size_t node_count = 0u;

    init_tree(&tree, 2u, 2u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &extra), SALTS_ENOBUFS);
    check_equal(extra, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_add_node(&tree, child, SALTS_BT_NODE_SEQUENCE,
                                      NULL, NULL, &extra), SALTS_EINVAL);
    check_equal(salts_bt_tree_get_node_count(&tree, &node_count), SALTS_OK);
    check_equal(node_count, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("rejects missing roots and invalid configuration") {
    salts_bt_tree tree = {0};
    salts_bt_tree_config config = {0};
    salts_bt_node_status result = SALTS_BT_FAILURE;
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;

    salts_bt_tree_config_init(&config);
    config.max_depth = 0u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_EINVAL);
    check_equal(salts_bt_tree_init(&tree, NULL), SALTS_EINVAL);

    salts_bt_tree_config_init(&config);
    config.max_nodes = 4u;
    config.max_depth = 4u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(salts_bt_tree_get_root(&tree, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps terminal child states through Inverter") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps a terminal child failure to SUCCESS through ForceSuccess") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_SUCCESS, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("maps a terminal child success to FAILURE through ForceFailure") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_FAILURE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("requires exactly one child for single-child decorators") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id extra = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &extra), SALTS_EINVAL);
    check_equal(extra, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }
  it("preserves RUNNING through status decorators") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_FORCE_SUCCESS, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("validates the whole tree before any leaf side effect") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id incomplete = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_INVERTER,
                                      NULL, NULL, &incomplete), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_EINVAL);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("accepts a complete tree through explicit validation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_INVERTER, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("retries terminal failures up to the configured attempt bound") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_FAILURE, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("repeats a bounded number of terminal child cycles across RUNNING ticks") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("rejects unbounded or over-budget Retry and Repeat limits") {
    salts_bt_tree tree = {0};
    salts_bt_tree_config config = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;

    salts_bt_tree_config_init(&config);
    config.max_nodes = 4u;
    config.max_depth = 4u;
    config.max_control_iterations = 1u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_OK);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 0u, &root), SALTS_EINVAL);
    check_equal(root, SALTS_BT_INVALID_NODE_ID);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 2u, &root), SALTS_EINVAL);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("returns FAILURE when Retry exhausts all attempts") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("treats a zero-cycle Repeat as immediate SUCCESS after structural validation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 0u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("halts a RUNNING leaf once on reset and again only after it restarts") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_leaf(&tree, SALTS_BT_INVALID_NODE_ID,
                                      &ops, &state, &root), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 2u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);
    check_true(tree.impl == NULL);
  }

  it("halts a RUNNING child when Timeout expires") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_timed_node(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_TIMEOUT,
                    UINT64_C(50000000), &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &state, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    cmeta_sleep_ms(60u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(state.halt_calls, 1u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
  }

  it("delays child execution without blocking the driver") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_timed_node(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_DELAY,
                    UINT64_C(30000000), &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 0u);
    cmeta_sleep_ms(40u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("reactivates terminal Parallel branches while another branch runs") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status first_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 2u, 0u};
    leaf_probe second_probe = {second_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PARALLEL, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(first_probe.calls, 1u);
    check_equal(second_probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(first_probe.calls, 2u);
    check_equal(second_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("keeps RUNNING Parallel branches active when another branch fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id active = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id failed = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status fail_steps[] = {SALTS_BT_FAILURE};
    leaf_probe fail_probe = {fail_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PARALLEL, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &state, &active), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &fail_probe, &failed), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    check_equal(state.halt_calls, 0u);
    check_equal(fail_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
  }

  it("rechecks ReactiveSequence conditions and halts later active children") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id work = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    leaf_probe condition_probe = {condition_steps, 2u, 0u};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state work_state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_REACTIVE_SEQUENCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &work_state, &work), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 1u);
    check_equal(work_state.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(condition_probe.calls, 2u);
    check_equal(work_state.halt_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(work_state.halt_calls, 1u);
  }

  it("caches RunOnce's terminal result until reset") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_RUN_ONCE, NULL, NULL, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }


  it("switches IfThenElse branches and halts a displaced RUNNING branch") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_node = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_node = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status conditions[] = {SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    condition_probe condition = {conditions, 2u, 0u, SALTS_OK};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status else_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe else_probe = {else_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_IF_THEN_ELSE,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running, &then_node),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_node), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(running.halt_calls, 1u);
    check_equal(else_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running.halt_calls, 1u);
  }

  it("enforces Precondition before running its child and yields from WhileDo") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status precondition_steps[] = {SALTS_BT_FAILURE, SALTS_BT_SUCCESS};
    condition_probe precondition = {precondition_steps, 2u, 0u, SALTS_OK};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe child_probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_PRECONDITION,
                                                probe_condition, &precondition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &child_probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(child_probe.calls, 0u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(child_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);

    {
      salts_bt_tree while_tree = {0};
      salts_bt_node_id while_root = SALTS_BT_INVALID_NODE_ID;
      salts_bt_node_id while_child = SALTS_BT_INVALID_NODE_ID;
      const salts_bt_node_status while_conditions[] = {SALTS_BT_SUCCESS, SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
      condition_probe while_condition = {while_conditions, 3u, 0u, SALTS_OK};
      const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
      leaf_probe while_probe = {success_steps, 1u, 0u};
      init_tree(&while_tree, 8u, 8u);
      check_equal(salts_bt_tree_add_condition_node(&while_tree, SALTS_BT_INVALID_NODE_ID,
                                                  SALTS_BT_NODE_WHILE_DO,
                                                  probe_condition, &while_condition, 4u,
                                                  &while_root), SALTS_OK);
      check_equal(salts_bt_tree_add_node(&while_tree, while_root, SALTS_BT_NODE_LEAF,
                                        probe_leaf, &while_probe, &while_child), SALTS_OK);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_RUNNING);
      check_equal(while_probe.calls, 1u);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_RUNNING);
      check_equal(while_probe.calls, 2u);
      check_equal(salts_bt_tree_tick(&while_tree, &result), SALTS_OK);
      check_equal(result, SALTS_BT_SUCCESS);
      check_equal(while_probe.calls, 2u);
      check_equal(salts_bt_tree_destroy(&while_tree), SALTS_OK);
    }
  }

  it("routes Switch to the selected case and halts the previous running case") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    const size_t selections[] = {0u, 1u};
    selector_probe selector = {selections, 2u, 0u};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe success_probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_switch_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                              probe_selector, &selector, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running, &first),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &success_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(running.halt_calls, 1u);
    check_equal(success_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("keeps running after successful child ticks and fails on its bounded budget") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_SUCCESS, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 3u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID,
                    SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE, NULL, NULL,
                    2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("propagates predicate errors before executing any branch side effects") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status conditions[] = {SALTS_BT_SUCCESS};
    condition_probe condition = {conditions, 1u, 0u, SALTS_EINVAL};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe child_probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_PRECONDITION,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &child_probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(condition.calls, 1u);
    check_equal(child_probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("fails Switch cleanly for an out-of-range case index") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    const size_t selections[] = {3u};
    selector_probe selector = {selections, 1u, 0u};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_switch_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                              probe_selector, &selector, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("PipelineSequence runs preparation/completion once per child across RUNNING") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    pipeline_probe pipeline = {0u, 0u, 0u, 0u, SALTS_BT_FAILURE, 0u, 0u, SALTS_OK};
    const salts_bt_node_status first_steps[] = {SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 1u, 0u};
    leaf_probe second_probe = {second_steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_pipeline_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                pipeline_prepare, pipeline_complete,
                                                pipeline_abort, &pipeline, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(pipeline.prepare_calls, 2u);
    check_equal(pipeline.complete_calls, 1u);
    check_equal(pipeline.last_completed_child, 0u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(pipeline.prepare_calls, 2u);
    check_equal(pipeline.complete_calls, 2u);
    check_equal(pipeline.last_completed_child, 1u);
    check_equal(pipeline.last_completed_status, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("EntryUpdated continues live work, caches terminal results, and reruns on change") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const bool change_steps[] = {false, false, false, true};
    entry_changed_probe changed = {change_steps, 4u, 0u, SALTS_OK};
    const salts_bt_node_status child_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    leaf_probe probe = {child_steps, 3u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_entry_updated_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     entry_changed, &changed, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(probe.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("ConsumeQueue advances one item per successful child iteration") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {2u, 0u, 0u, 0u};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 1u);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(queue.begin_calls, 2u);
    check_equal(queue.finish_calls, 2u);
    check_equal(probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("aborts an active PipelineSequence context after halting the child on reset") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    pipeline_probe pipeline = {0u, 0u, 0u, 0u, SALTS_BT_FAILURE, 0u, 0u, SALTS_OK};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_pipeline_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                pipeline_prepare, pipeline_complete,
                                                pipeline_abort, &pipeline, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(pipeline.prepare_calls, 1u);
    check_equal(pipeline.abort_calls, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(leaf.halt_calls, 1u);
    check_equal(pipeline.abort_calls, 1u);
    check_equal(pipeline.last_aborted_child, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(pipeline.abort_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("aborts a live ConsumeQueue item exactly once when reset interrupts its child") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {2u, 0u, 0u, 0u};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 0u);
    check_equal(queue.abort_calls, 0u);
    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(leaf.halt_calls, 1u);
    check_equal(queue.abort_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(queue.abort_calls, 1u);
  }



  it("returns FAILURE for an empty ConsumeQueue without ticking its child") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    queue_probe queue = {0u, 0u, 0u, 0u};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_consume_queue_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                     queue_begin, queue_finish,
                                                     queue_abort, &queue, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(queue.begin_calls, 1u);
    check_equal(queue.finish_calls, 0u);
    check_equal(queue.abort_calls, 0u);
    check_equal(probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("cancels active work and allows a fresh activation without stale callbacks") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;
    size_t node_count = 0u;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_leaf(&tree, SALTS_BT_INVALID_NODE_ID,
                                      &ops, &state, &root), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 1u);
    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
    check_equal(salts_bt_tree_get_node_count(&tree, &node_count), SALTS_OK);
    check_equal(node_count, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(state.tick_calls, 2u);
    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(state.halt_calls, 2u);
    check_equal(state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 2u);
  }



  it("preserves per-branch activation until terminal finish and merges via host hooks") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status first_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status second_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe first_probe = {first_steps, 2u, 0u};
    leaf_probe second_probe = {second_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step, parallel_join, parallel_finish,
                                               parallel_abort, &context, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &first_probe, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &second_probe, &second), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(context.enter_calls[0], 1u);
    check_equal(context.first_activations[0], 1u);
    check_equal(context.resumes[0], 0u);
    check_equal(context.finish_calls[0], 0u);
    check_equal(context.enter_calls[1], 1u);
    check_equal(context.first_activations[1], 1u);
    check_equal(context.finish_calls[1], 1u);
    check_equal(context.terminal_status[1], SALTS_BT_SUCCESS);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(context.enter_calls[0], 2u);
    check_equal(context.first_activations[0], 1u);
    check_equal(context.resumes[0], 1u);
    check_equal(context.finish_calls[0], 1u);
    check_equal(context.terminal_status[0], SALTS_BT_SUCCESS);
    check_equal(context.enter_calls[1], 2u);
    check_equal(context.first_activations[1], 2u);
    check_equal(context.finish_calls[1], 2u);
    check_equal(context.abort_calls[0], 0u);
    check_equal(context.abort_calls[1], 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("aborts prepared Parallel branches after halting them on cancellation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id first = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id second = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state first_leaf = {&tree, 0u, 0u, SALTS_OK};
    running_leaf_state second_leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step, parallel_join, parallel_finish,
                                               parallel_abort, &context, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &first_leaf, &first), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &second_leaf, &second), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(first_leaf.halt_calls, 1u);
    check_equal(second_leaf.halt_calls, 1u);
    check_equal(context.abort_calls[0], 1u);
    check_equal(context.abort_calls[1], 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(context.abort_calls[0], 1u);
    check_equal(context.abort_calls[1], 1u);
  }

  it("cleans remaining running branches when a leaf returns an invalid status") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id running = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id invalid = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PARALLEL, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &ops, &state, &running), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      invalid_status_leaf, NULL, &invalid), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(state.tick_calls, 1u);
    check_equal(state.halt_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(state.halt_calls, 1u);
  }



  it("aborts prepared Parallel contexts and halts prior branches when enter fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id active = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id not_started = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_leaf_ops active_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state active_leaf = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe never_started = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    context.enter_status[1] = SALTS_EINVAL;
    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step, parallel_join, parallel_finish,
                                               parallel_abort, &context, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &active_ops,
                                      &active_leaf, &active), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &never_started, &not_started), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(context.enter_calls[0], 1u);
    check_equal(context.enter_calls[1], 1u);
    check_equal(active_leaf.tick_calls, 1u);
    check_equal(active_leaf.halt_calls, 1u);
    check_equal(never_started.calls, 0u);
    check_equal(context.abort_calls[0], 1u);
    check_equal(context.abort_calls[1], 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(context.abort_calls[0], 1u);
    check_equal(context.abort_calls[1], 1u);
  }

  it("aborts the Parallel branch context when terminal finish fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe leaf = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    context.finish_status[0] = SALTS_EINVAL;
    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step, parallel_join, parallel_finish,
                                               parallel_abort, &context, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(context.enter_calls[0], 1u);
    check_equal(context.finish_calls[0], 1u);
    check_equal(context.terminal_status[0], SALTS_BT_SUCCESS);
    check_equal(context.abort_calls[0], 1u);
    check_equal(leaf.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(context.abort_calls[0], 1u);
  }



  it("finishes a Parallel branch after a nested Repeat resets its own control state") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id repeat = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id leaf = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status steps[] = {SALTS_BT_FAILURE, SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step, parallel_join, parallel_finish,
                                               parallel_abort, &context, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node_with_limit(&tree, root, SALTS_BT_NODE_REPEAT,
                                                  NULL, NULL, 2u, &repeat), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, repeat, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &leaf), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 2u);
    check_equal(context.enter_calls[0], 1u);
    check_equal(context.finish_calls[0], 1u);
    check_equal(context.terminal_status[0], SALTS_BT_SUCCESS);
    check_equal(context.abort_calls[0], 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("preserves RunOnce completion across ordinary parent Sequence resets") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id once = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id once_leaf = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id fail_leaf = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    const salts_bt_node_status failure_steps[] = {SALTS_BT_FAILURE};
    leaf_probe once_probe = {success_steps, 1u, 0u};
    leaf_probe failure_probe = {failure_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_RUN_ONCE,
                                      NULL, NULL, &once), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, once, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &once_probe, &once_leaf), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &failure_probe, &fail_leaf), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(once_probe.calls, 1u);
    check_equal(failure_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(once_probe.calls, 1u);
    check_equal(failure_probe.calls, 2u);

    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(once_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("preserves EntryUpdated terminal cache across parent resets but reruns after explicit reset") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id updated = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id updated_leaf = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id fail_leaf = SALTS_BT_INVALID_NODE_ID;
    const bool unchanged_steps[] = {false, false, false, false};
    entry_changed_probe changed = {unchanged_steps, 4u, 0u, SALTS_OK};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    const salts_bt_node_status failure_steps[] = {SALTS_BT_FAILURE};
    leaf_probe updated_probe = {success_steps, 1u, 0u};
    leaf_probe failure_probe = {failure_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_entry_updated_node(&tree, root, entry_changed,
                                                     &changed, &updated), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, updated, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &updated_probe, &updated_leaf), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &failure_probe, &fail_leaf), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(updated_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(updated_probe.calls, 1u);

    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(updated_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("restarts an EntryUpdated child that was RUNNING when explicitly cancelled") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id updated = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id updated_leaf = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id blocking_leaf = SALTS_BT_INVALID_NODE_ID;
    const bool unchanged_steps[] = {false, false, false};
    entry_changed_probe changed = {unchanged_steps, 3u, 0u, SALTS_OK};
    const salts_bt_node_status running_then_success[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status failure_steps[] = {SALTS_BT_FAILURE};
    leaf_probe updated_probe = {running_then_success, 2u, 0u};
    leaf_probe blocker = {failure_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_entry_updated_node(&tree, root, entry_changed,
                                                     &changed, &updated), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, updated, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &updated_probe, &updated_leaf), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &blocker, &blocking_leaf), SALTS_OK);

    /* The first tick is RUNNING inside EntryUpdated, so Sequence never reaches blocker. */
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(updated_probe.calls, 1u);
    /* Explicit cancellation clears cached state and halts RUNNING work. */
    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(updated_probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    /* EntryUpdated restarts and succeeds; Sequence then reaches its failing blocker. */
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(updated_probe.calls, 2u);
    check_equal(blocker.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("rechecks ReactiveFallback priority and halts a lower-priority RUNNING branch") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id priority = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id fallback = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status priority_steps[] = {
        SALTS_BT_FAILURE, SALTS_BT_SUCCESS};
    leaf_probe priority_probe = {priority_steps, 2u, 0u};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state fallback_probe = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_REACTIVE_FALLBACK,
                                      NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &priority_probe, &priority), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops,
                                      &fallback_probe, &fallback), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(priority_probe.calls, 1u);
    check_equal(fallback_probe.tick_calls, 1u);
    check_equal(fallback_probe.halt_calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(priority_probe.calls, 2u);
    check_equal(fallback_probe.tick_calls, 1u);
    check_equal(fallback_probe.halt_calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(fallback_probe.halt_calls, 1u);
  }



  it("clears persistent caches after a tick callback error before retrying") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id once = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id once_leaf = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id flaky_leaf = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    const salts_bt_node_status invalid_then_success[] = {
        (salts_bt_node_status)99, SALTS_BT_SUCCESS};
    leaf_probe once_probe = {success_steps, 1u, 0u};
    leaf_probe flaky_probe = {invalid_then_success, 2u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_RUN_ONCE,
                                      NULL, NULL, &once), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, once, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &once_probe, &once_leaf), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &flaky_probe, &flaky_leaf), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(once_probe.calls, 1u);
    check_equal(flaky_probe.calls, 1u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(once_probe.calls, 2u);
    check_equal(flaky_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("does not tick IfThenElse branches while its condition is RUNNING") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_node = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_node = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_RUNNING, SALTS_BT_FAILURE};
    condition_probe condition = {condition_steps, 3u, 0u, SALTS_OK};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state then_probe = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status else_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe else_probe = {else_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_IF_THEN_ELSE,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &then_probe, &then_node),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_node), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(then_probe.tick_calls, 1u);
    check_equal(then_probe.halt_calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(then_probe.tick_calls, 1u);
    check_equal(then_probe.halt_calls, 1u);
    check_equal(else_probe.calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(then_probe.tick_calls, 1u);
    check_equal(else_probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(then_probe.halt_calls, 1u);
  }

  it("pauses WhileDo body execution while its condition node is RUNNING") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    condition_probe condition = {condition_steps, 3u, 0u, SALTS_OK};
    const salts_bt_node_status child_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe child_probe = {child_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_WHILE_DO,
                                                probe_condition, &condition, 2u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &child_probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(child_probe.calls, 1u);
    check_equal(condition.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(child_probe.calls, 1u);
    check_equal(condition.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(child_probe.calls, 2u);
    check_equal(condition.calls, 3u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("ticks an in-tree IfThenElse condition before touching either branch") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_branch = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_branch = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_RUNNING, SALTS_BT_SUCCESS};
    const salts_bt_node_status branch_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe condition_probe = {condition_steps, 2u, 0u};
    leaf_probe then_probe = {branch_steps, 1u, 0u};
    leaf_probe else_probe = {branch_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_IF_THEN_ELSE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &then_probe, &then_branch), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_branch), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 1u);
    check_equal(then_probe.calls, 0u);
    check_equal(else_probe.calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(condition_probe.calls, 2u);
    check_equal(then_probe.calls, 1u);
    check_equal(else_probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("supports a condition child for Precondition") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id body = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {SALTS_BT_FAILURE};
    const salts_bt_node_status body_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe condition_probe = {condition_steps, 1u, 0u};
    leaf_probe body_probe = {body_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_PRECONDITION, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &body_probe, &body), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(condition_probe.calls, 1u);
    check_equal(body_probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("supports a condition child for WhileDo and respects the iteration bound") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id body = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    const salts_bt_node_status body_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe condition_probe = {condition_steps, 3u, 0u};
    leaf_probe body_probe = {body_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(&tree, SALTS_BT_INVALID_NODE_ID,
                                                  SALTS_BT_NODE_WHILE_DO, NULL, NULL,
                                                  4u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &body_probe, &body), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 1u);
    check_equal(body_probe.calls, 1u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 2u);
    check_equal(body_probe.calls, 2u);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(condition_probe.calls, 3u);
    check_equal(body_probe.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("halts a RUNNING in-tree IfThenElse condition on cancellation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_branch = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_branch = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state condition_state = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status branch_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe then_probe = {branch_steps, 1u, 0u};
    leaf_probe else_probe = {branch_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_IF_THEN_ELSE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops,
                                      &condition_state, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &then_probe, &then_branch), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_branch), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_state.tick_calls, 1u);
    check_equal(then_probe.calls, 0u);
    check_equal(else_probe.calls, 0u);

    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(condition_state.halt_calls, 1u);
    check_equal(condition_state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(condition_state.halt_calls, 1u);
  }

  it("halts a RUNNING in-tree WhileDo condition on cancellation") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id body = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state condition_state = {&tree, 0u, 0u, SALTS_OK};
    const salts_bt_node_status body_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe body_probe = {body_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(&tree, SALTS_BT_INVALID_NODE_ID,
                                                  SALTS_BT_NODE_WHILE_DO, NULL, NULL,
                                                  4u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops,
                                      &condition_state, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &body_probe, &body), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_state.tick_calls, 1u);
    check_equal(condition_state.halt_calls, 0u);
    check_equal(body_probe.calls, 0u);

    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(condition_state.halt_calls, 1u);
    check_equal(condition_state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(condition_state.halt_calls, 1u);
  }

  it("halts a RUNNING WhileDo body when its in-tree condition becomes FAILURE") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id condition = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id body = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status condition_steps[] = {
        SALTS_BT_SUCCESS, SALTS_BT_FAILURE};
    leaf_probe condition_probe = {condition_steps, 2u, 0u};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state body_state = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(&tree, SALTS_BT_INVALID_NODE_ID,
                                                  SALTS_BT_NODE_WHILE_DO, NULL, NULL,
                                                  4u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &condition_probe, &condition), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &body_state, &body), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(condition_probe.calls, 1u);
    check_equal(body_state.tick_calls, 1u);
    check_equal(body_state.halt_calls, 0u);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(condition_probe.calls, 2u);
    check_equal(body_state.tick_calls, 1u);
    check_equal(body_state.halt_calls, 1u);
    check_equal(body_state.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(body_state.halt_calls, 1u);
  }

  it("gives RUNNING precedence over FAILURE and invokes branch step on each tick") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id failing = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id running = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status failure_steps[] = {SALTS_BT_FAILURE};
    leaf_probe failure_leaf = {failure_steps, 1u, 0u};
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running_leaf = {&tree, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step,
                                               parallel_join, parallel_finish, parallel_abort,
                                               &context, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &failure_leaf, &failing), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running_leaf, &running), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(failure_leaf.calls, 1u);
    check_equal(running_leaf.tick_calls, 1u);
    check_equal(running_leaf.halt_calls, 0u);
    check_equal(context.step_calls[0], 1u);
    check_equal(context.step_status[0], SALTS_BT_FAILURE);
    check_equal(context.finish_calls[0], 1u);
    check_equal(context.step_calls[1], 1u);
    check_equal(context.step_status[1], SALTS_BT_RUNNING);
    check_equal(context.finish_calls[1], 0u);
    check_equal(context.join_calls, 1u);
    check_equal(context.joined_status, SALTS_BT_RUNNING);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    /* A terminal branch is finished and reactivated while a sibling runs. */
    check_equal(failure_leaf.calls, 2u);
    check_equal(running_leaf.tick_calls, 2u);
    check_equal(context.enter_calls[0], 2u);
    check_equal(context.first_activations[0], 2u);
    check_equal(context.step_calls[0], 2u);
    check_equal(context.step_status[0], SALTS_BT_FAILURE);
    check_equal(context.finish_calls[0], 2u);
    check_equal(context.step_calls[1], 2u);
    check_equal(context.step_status[1], SALTS_BT_RUNNING);
    check_equal(context.join_calls, 2u);
    check_equal(context.joined_status, SALTS_BT_RUNNING);
    check_equal(salts_bt_tree_cancel(&tree), SALTS_OK);
    check_equal(running_leaf.halt_calls, 1u);
    check_equal(context.abort_calls[0], 0u);
    check_equal(context.abort_calls[1], 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running_leaf.halt_calls, 1u);
  }

  it("aborts Parallel branch context when per-tick step/merge fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe leaf = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    context.step_return[0] = SALTS_EINVAL;
    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step,
                                               parallel_join, parallel_finish, parallel_abort,
                                               &context, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(leaf.calls, 1u);
    check_equal(context.step_calls[0], 1u);
    check_equal(context.finish_calls[0], 0u);
    check_equal(context.abort_calls[0], 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(context.abort_calls[0], 1u);
  }

  it("aborts all prepared Parallel branches when the join/merge callback fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    parallel_probe context = {0};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe leaf = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    context.join_return = SALTS_EINVAL;
    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_parallel_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                               parallel_enter, parallel_step,
                                               parallel_join, parallel_finish,
                                               parallel_abort, &context, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(leaf.calls, 1u);
    check_equal(context.step_calls[0], 1u);
    check_equal(context.join_calls, 1u);
    check_equal(context.joined_status, SALTS_BT_SUCCESS);
    check_equal(context.finish_calls[0], 0u);
    check_equal(context.abort_calls[0], 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(context.abort_calls[0], 1u);
  }

  it("treats max_depth and max_nodes as independent bounds") {
    salts_bt_tree tree = {0};
    salts_bt_tree_config config = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    salts_bt_tree_config_init(&config);
    config.max_nodes = 2u;
    config.max_depth = 8u;
    check_equal(salts_bt_tree_init(&tree, &config), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &child), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }



  it("rejects invalid condition statuses without ticking either IfThenElse branch") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id then_node = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id else_node = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status invalid_steps[] = {(salts_bt_node_status)99};
    condition_probe condition = {invalid_steps, 1u, 0u, SALTS_OK};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe then_probe = {success_steps, 1u, 0u};
    leaf_probe else_probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_SUCCESS;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_condition_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                                SALTS_BT_NODE_IF_THEN_ELSE,
                                                probe_condition, &condition, 0u, &root),
                SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &then_probe, &then_node), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &else_probe, &else_node), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EINVAL);
    check_equal(condition.calls, 1u);
    check_equal(then_probe.calls, 0u);
    check_equal(else_probe.calls, 0u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("rejects callback-only node kinds through the generic constructor") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id node = 42u;
    salts_bt_node_id leaf = SALTS_BT_INVALID_NODE_ID;
    size_t count = 0u;
    size_t i;
    const salts_bt_node_kind callback_only_kinds[] = {
        SALTS_BT_NODE_SWITCH,
        SALTS_BT_NODE_PIPELINE_SEQUENCE,
        SALTS_BT_NODE_ENTRY_UPDATED,
        SALTS_BT_NODE_CONSUME_QUEUE};
    const salts_bt_node_status success_steps[] = {SALTS_BT_SUCCESS};
    leaf_probe probe = {success_steps, 1u, 0u};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 8u, 8u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL,
                                      &root), SALTS_OK);

    for (i = 0u; i < sizeof(callback_only_kinds) / sizeof(callback_only_kinds[0]);
         ++i) {
      node = 42u;
      check_equal(salts_bt_tree_add_node_with_limit(&tree, root,
                                                    callback_only_kinds[i],
                                                    NULL, NULL, 0u, &node),
                  SALTS_EINVAL);
      check_equal(node, SALTS_BT_INVALID_NODE_ID);
      check_equal(salts_bt_tree_get_node_count(&tree, &count), SALTS_OK);
      check_equal(count, 1u);
    }

    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &probe, &leaf), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(probe.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }


  it("re-resolves Retry limits on each tick before ticking the child") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_RUNNING, SALTS_BT_FAILURE};
    leaf_probe leaf = {steps, 2u, 0u};
    dynamic_parameter_probe dynamic = {0u, 2u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &dynamic), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(leaf.calls, 1u);
    check_equal(dynamic.calls, 1u);

    /* Shrinking the live budget to one makes the next failed attempt terminal. */
    dynamic.limit = 1u;
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(leaf.calls, 2u);
    check_equal(dynamic.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("re-resolves a Timeout deadline and halts a running child once") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    dynamic_parameter_probe dynamic = {0u, 0u, UINT64_MAX, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_timed_node(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_TIMEOUT,
                    UINT64_MAX, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops,
                                      &running, &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS,
                    resolve_dynamic_parameters, &dynamic), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(running.halt_calls, 0u);
    check_equal(dynamic.calls, 1u);

    dynamic.duration_ns = 0u;
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(running.tick_calls, 1u);
    check_equal(running.halt_calls, 1u);
    check_equal(running.reentrant_reset_status, SALTS_EBUSY);
    check_equal(dynamic.calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running.halt_calls, 1u);
  }

  it("rejects invalid dynamic parameter resolver contracts") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    dynamic_parameter_probe dynamic = {0u, 1u, 0u, SALTS_OK};

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node(&tree, SALTS_BT_INVALID_NODE_ID,
                                      SALTS_BT_NODE_SEQUENCE, NULL, NULL,
                                      &root), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &dynamic), SALTS_EINVAL);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    NULL, &dynamic), SALTS_EINVAL);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, 0u, resolve_dynamic_parameters, &dynamic),
                SALTS_EINVAL);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT |
                                     SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS,
                    resolve_dynamic_parameters, &dynamic), SALTS_EINVAL);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("honors a dynamic zero Retry budget without ticking its child") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe leaf = {steps, 1u, 0u};
    dynamic_parameter_probe dynamic = {0u, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &dynamic), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(leaf.calls, 0u);
    check_equal(dynamic.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("restores constructor limits when detaching a dynamic resolver") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_node_status steps[] = {SALTS_BT_SUCCESS};
    leaf_probe leaf = {steps, 1u, 0u};
    dynamic_parameter_probe dynamic = {0u, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_RUNNING;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_node(&tree, root, SALTS_BT_NODE_LEAF,
                                      probe_leaf, &leaf, &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &dynamic), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_FAILURE);
    check_equal(leaf.calls, 0u);
    check_equal(dynamic.calls, 1u);

    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, 0u, NULL, NULL), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(leaf.calls, 1u);
    check_equal(dynamic.calls, 1u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
  }

  it("reconfigures a dynamic resolver as a clean activation boundary") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    dynamic_parameter_probe first = {0u, 2u, 0u, SALTS_OK};
    dynamic_parameter_probe second = {0u, 0u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_REPEAT,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running,
                                      &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &first), SALTS_OK);

    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(running.halt_calls, 0u);
    check_equal(first.calls, 1u);

    /* Replacing the resolver must halt the active child before switching users. */
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &second), SALTS_OK);
    check_equal(running.halt_calls, 1u);
    check_equal(running.reentrant_reset_status, SALTS_EBUSY);
    check_equal(second.calls, 0u);

    /* The replacement takes effect on the next visit and zero Repeat succeeds. */
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_SUCCESS);
    check_equal(running.tick_calls, 1u);
    check_equal(second.calls, 1u);

    /* Detaching restores the constructor limit (2) and stops calling the resolver. */
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, 0u, NULL, NULL), SALTS_OK);
    check_equal(salts_bt_tree_validate(&tree), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 2u);
    check_equal(second.calls, 1u);

    check_equal(salts_bt_tree_reset(&tree), SALTS_OK);
    check_equal(running.halt_calls, 2u);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running.halt_calls, 2u);
  }

  it("cleans up active leaves when a dynamic parameter resolver fails") {
    salts_bt_tree tree = {0};
    salts_bt_node_id root = SALTS_BT_INVALID_NODE_ID;
    salts_bt_node_id child = SALTS_BT_INVALID_NODE_ID;
    const salts_bt_leaf_ops running_ops = {always_running_leaf, halt_running_leaf};
    running_leaf_state running = {&tree, 0u, 0u, SALTS_OK};
    dynamic_parameter_probe dynamic = {0u, 2u, 0u, SALTS_OK};
    salts_bt_node_status result = SALTS_BT_FAILURE;

    init_tree(&tree, 4u, 4u);
    check_equal(salts_bt_tree_add_node_with_limit(
                    &tree, SALTS_BT_INVALID_NODE_ID, SALTS_BT_NODE_RETRY,
                    NULL, NULL, 2u, &root), SALTS_OK);
    check_equal(salts_bt_tree_add_leaf(&tree, root, &running_ops, &running,
                                      &child), SALTS_OK);
    check_equal(salts_bt_tree_set_node_parameters_resolver(
                    &tree, root, SALTS_BT_DYNAMIC_PARAMETER_LIMIT,
                    resolve_dynamic_parameters, &dynamic), SALTS_OK);
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_OK);
    check_equal(result, SALTS_BT_RUNNING);
    check_equal(running.tick_calls, 1u);
    check_equal(running.halt_calls, 0u);

    dynamic.return_status = SALTS_EIO;
    check_equal(salts_bt_tree_tick(&tree, &result), SALTS_EIO);
    check_equal(running.tick_calls, 1u);
    check_equal(running.halt_calls, 1u);
    check_equal(running.reentrant_reset_status, SALTS_EBUSY);
    check_equal(salts_bt_tree_destroy(&tree), SALTS_OK);
    check_equal(running.halt_calls, 1u);
  }


}
