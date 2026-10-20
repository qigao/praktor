#include "tree_adapter.h"

#include <stdlib.h>
#include <string.h>

typedef struct praktor_c11_bt_impl praktor_c11_bt_impl;

typedef struct praktor_c11_bt_node {
  praktor_c11_bt_impl *owner;
  size_t index;
  salts_bt_node_id runtime_id;
  praktor_c11_bt_node_desc desc;
} praktor_c11_bt_node;

struct praktor_c11_bt_impl {
  salts_bt_tree runtime;
  praktor_c11_bt_node *nodes;
  size_t node_count;
  praktor_c11_bt_host host;
  int leaf_callback_error;
};

static praktor_c11_bt_impl *tree_impl(praktor_c11_bt_tree *tree) {
  return tree != NULL ? (praktor_c11_bt_impl *)tree->impl : NULL;
}

static const praktor_c11_bt_impl *tree_const_impl(const praktor_c11_bt_tree *tree) {
  return tree != NULL ? (const praktor_c11_bt_impl *)tree->impl : NULL;
}

static bool is_condition_kind(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_IF_THEN_ELSE ||
         kind == SALTS_BT_NODE_PRECONDITION ||
         kind == SALTS_BT_NODE_WHILE_DO;
}

static bool has_limit(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_RETRY ||
         kind == SALTS_BT_NODE_REPEAT ||
         kind == SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE ||
         kind == SALTS_BT_NODE_WHILE_DO;
}

static bool is_timed_kind(salts_bt_node_kind kind) {
  return kind == SALTS_BT_NODE_TIMEOUT || kind == SALTS_BT_NODE_DELAY;
}

static bool callback_contract_valid(const praktor_c11_bt_node_desc *desc,
                                    const praktor_c11_bt_host *host) {
  if (desc->condition_callback &&
      (!is_condition_kind(desc->kind) || host->condition == NULL))
    return false;
  if (desc->parallel_hooks) {
    if (desc->kind != SALTS_BT_NODE_PARALLEL ||
        host->parallel_enter == NULL || host->parallel_step == NULL ||
        host->parallel_join == NULL || host->parallel_finish == NULL)
      return false;
  }
  if (desc->kind == SALTS_BT_NODE_LEAF && host->tick_leaf == NULL)
    return false;
  if (desc->kind == SALTS_BT_NODE_SWITCH && host->select_child == NULL)
    return false;
  if (desc->kind == SALTS_BT_NODE_PIPELINE_SEQUENCE &&
      (host->pipeline_prepare == NULL || host->pipeline_complete == NULL))
    return false;
  if (desc->kind == SALTS_BT_NODE_ENTRY_UPDATED && host->entry_changed == NULL)
    return false;
  if (desc->kind == SALTS_BT_NODE_CONSUME_QUEUE &&
      (host->queue_begin == NULL || host->queue_finish == NULL))
    return false;
  if (!has_limit(desc->kind) && desc->limit != 0u)
    return false;
  if (!is_timed_kind(desc->kind) && desc->duration_ns != 0u)
    return false;
  if (desc->parallel_hooks && desc->kind != SALTS_BT_NODE_PARALLEL)
    return false;
  if (desc->condition_callback && !is_condition_kind(desc->kind))
    return false;
  if (desc->kind != SALTS_BT_NODE_WHILE_DO &&
      desc->kind != SALTS_BT_NODE_IF_THEN_ELSE &&
      desc->kind != SALTS_BT_NODE_PRECONDITION &&
      desc->condition_callback)
    return false;
  return true;
}

/*
 * Leaf callbacks in Salts return only a node status. Preserve a host callback's
 * actual error by provoking Salts' invalid-status path, which makes the runtime
 * halt/reset the whole activation, then return the saved error from tree_tick.
 */
static salts_bt_node_status adapter_leaf_tick(void *user) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  salts_bt_node_status status = (salts_bt_node_status)99;
  int result;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.tick_leaf == NULL)
    return (salts_bt_node_status)99;

  result = node->owner->host.tick_leaf(node->owner->host.user, node->index, &status);
  if (result != SALTS_OK) {
    node->owner->leaf_callback_error = result;
    return (salts_bt_node_status)99;
  }
  return status;
}

static void adapter_leaf_halt(void *user) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node != NULL && node->owner != NULL &&
      node->owner->host.halt_leaf != NULL) {
    node->owner->host.halt_leaf(node->owner->host.user, node->index);
  }
}

static int adapter_condition(void *user, salts_bt_node_status *out_status) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL || out_status == NULL ||
      node->owner->host.condition == NULL)
    return SALTS_EINVAL;
  return node->owner->host.condition(node->owner->host.user, node->index,
                                     out_status);
}

static int adapter_select_child(void *user, size_t *out_child_index) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL || out_child_index == NULL ||
      node->owner->host.select_child == NULL)
    return SALTS_EINVAL;
  return node->owner->host.select_child(node->owner->host.user, node->index,
                                        out_child_index);
}

static int adapter_parallel_enter(void *user, size_t child_index,
                                  bool first_activation) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.parallel_enter == NULL)
    return SALTS_EINVAL;
  return node->owner->host.parallel_enter(node->owner->host.user, node->index,
                                          child_index, first_activation);
}

static int adapter_parallel_step(void *user, size_t child_index,
                                 salts_bt_node_status child_status) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.parallel_step == NULL)
    return SALTS_EINVAL;
  return node->owner->host.parallel_step(node->owner->host.user, node->index,
                                         child_index, child_status);
}

static int adapter_parallel_join(void *user,
                                 salts_bt_node_status aggregate_status) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.parallel_join == NULL)
    return SALTS_EINVAL;
  return node->owner->host.parallel_join(node->owner->host.user, node->index,
                                         aggregate_status);
}

static int adapter_parallel_finish(void *user, size_t child_index,
                                   salts_bt_node_status child_status) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.parallel_finish == NULL)
    return SALTS_EINVAL;
  return node->owner->host.parallel_finish(node->owner->host.user, node->index,
                                           child_index, child_status);
}

static void adapter_parallel_abort(void *user, size_t child_index) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node != NULL && node->owner != NULL &&
      node->owner->host.parallel_abort != NULL) {
    node->owner->host.parallel_abort(node->owner->host.user, node->index,
                                     child_index);
  }
}

static int adapter_pipeline_prepare(void *user, size_t child_index) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.pipeline_prepare == NULL)
    return SALTS_EINVAL;
  return node->owner->host.pipeline_prepare(node->owner->host.user, node->index,
                                            child_index);
}

static int adapter_pipeline_complete(void *user, size_t child_index,
                                     salts_bt_node_status child_status) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL ||
      node->owner->host.pipeline_complete == NULL)
    return SALTS_EINVAL;
  return node->owner->host.pipeline_complete(node->owner->host.user, node->index,
                                             child_index, child_status);
}

static void adapter_pipeline_abort(void *user, size_t child_index) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node != NULL && node->owner != NULL &&
      node->owner->host.pipeline_abort != NULL) {
    node->owner->host.pipeline_abort(node->owner->host.user, node->index,
                                     child_index);
  }
}

static int adapter_entry_changed(void *user, bool *out_changed) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL || out_changed == NULL ||
      node->owner->host.entry_changed == NULL)
    return SALTS_EINVAL;
  return node->owner->host.entry_changed(node->owner->host.user, node->index,
                                         out_changed);
}

static int adapter_queue_begin(void *user, bool *out_has_item) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL || out_has_item == NULL ||
      node->owner->host.queue_begin == NULL)
    return SALTS_EINVAL;
  return node->owner->host.queue_begin(node->owner->host.user, node->index,
                                       out_has_item);
}

static int adapter_queue_finish(void *user, bool *out_has_more) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node == NULL || node->owner == NULL || out_has_more == NULL ||
      node->owner->host.queue_finish == NULL)
    return SALTS_EINVAL;
  return node->owner->host.queue_finish(node->owner->host.user, node->index,
                                        out_has_more);
}

static void adapter_queue_abort(void *user) {
  praktor_c11_bt_node *node = (praktor_c11_bt_node *)user;
  if (node != NULL && node->owner != NULL &&
      node->owner->host.queue_abort != NULL) {
    node->owner->host.queue_abort(node->owner->host.user, node->index);
  }
}

static void adapter_dispose_partial(praktor_c11_bt_impl *impl) {
  if (impl == NULL) return;
  if (impl->runtime.impl != NULL) (void)salts_bt_tree_destroy(&impl->runtime);
  free(impl->nodes);
  free(impl);
}

static int adapter_add_node(praktor_c11_bt_impl *impl, size_t index) {
  praktor_c11_bt_node *node = &impl->nodes[index];
  const praktor_c11_bt_node_desc *desc = &node->desc;
  salts_bt_node_id parent_id =
      desc->parent_index == PRAKTOR_C11_BT_NO_PARENT
          ? SALTS_BT_INVALID_NODE_ID
          : impl->nodes[desc->parent_index].runtime_id;
  int status;

  if (desc->kind == SALTS_BT_NODE_LEAF) {
    const salts_bt_leaf_ops ops = {adapter_leaf_tick, adapter_leaf_halt};
    return salts_bt_tree_add_leaf(&impl->runtime, parent_id, &ops, node,
                                  &node->runtime_id);
  }
  if (is_timed_kind(desc->kind)) {
    return salts_bt_tree_add_timed_node(&impl->runtime, parent_id, desc->kind,
                                        desc->duration_ns, &node->runtime_id);
  }
  if (is_condition_kind(desc->kind) && desc->condition_callback) {
    return salts_bt_tree_add_condition_node(
        &impl->runtime, parent_id, desc->kind, adapter_condition, node,
        desc->limit, &node->runtime_id);
  }
  if (desc->kind == SALTS_BT_NODE_SWITCH) {
    return salts_bt_tree_add_switch_node(&impl->runtime, parent_id,
                                         adapter_select_child, node,
                                         &node->runtime_id);
  }
  if (desc->kind == SALTS_BT_NODE_PARALLEL && desc->parallel_hooks) {
    return salts_bt_tree_add_parallel_node(
        &impl->runtime, parent_id, adapter_parallel_enter, adapter_parallel_step,
        adapter_parallel_join, adapter_parallel_finish, adapter_parallel_abort,
        node, &node->runtime_id);
  }
  if (desc->kind == SALTS_BT_NODE_PIPELINE_SEQUENCE) {
    return salts_bt_tree_add_pipeline_node(
        &impl->runtime, parent_id, adapter_pipeline_prepare,
        adapter_pipeline_complete, adapter_pipeline_abort, node,
        &node->runtime_id);
  }
  if (desc->kind == SALTS_BT_NODE_ENTRY_UPDATED) {
    return salts_bt_tree_add_entry_updated_node(
        &impl->runtime, parent_id, adapter_entry_changed, node,
        &node->runtime_id);
  }
  if (desc->kind == SALTS_BT_NODE_CONSUME_QUEUE) {
    return salts_bt_tree_add_consume_queue_node(
        &impl->runtime, parent_id, adapter_queue_begin, adapter_queue_finish,
        adapter_queue_abort, node, &node->runtime_id);
  }
  if (has_limit(desc->kind)) {
    status = salts_bt_tree_add_node_with_limit(
        &impl->runtime, parent_id, desc->kind, NULL, NULL, desc->limit,
        &node->runtime_id);
  } else {
    status = salts_bt_tree_add_node(&impl->runtime, parent_id, desc->kind,
                                    NULL, NULL, &node->runtime_id);
  }
  return status;
}

int praktor_c11_bt_tree_init(praktor_c11_bt_tree *tree,
                             const salts_bt_tree_config *tree_config,
                             const praktor_c11_bt_node_desc *nodes,
                             size_t node_count,
                             const praktor_c11_bt_host *host) {
  praktor_c11_bt_impl *impl;
  size_t index;
  int status;

  if (tree == NULL || tree->impl != NULL || tree_config == NULL ||
      nodes == NULL || node_count == 0u || host == NULL ||
      node_count > UINT32_MAX ||
      node_count > SIZE_MAX / sizeof(praktor_c11_bt_node))
    return SALTS_EINVAL;
  if (tree_config->max_nodes == 0u ||
      tree_config->max_nodes > UINT32_MAX ||
      tree_config->max_depth == 0u ||
      tree_config->max_depth > SALTS_BT_MAX_DEPTH ||
      tree_config->max_control_iterations == 0u ||
      tree_config->max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS)
    return SALTS_EINVAL;
  if (tree_config->max_nodes < node_count) return SALTS_ENOBUFS;
  if (nodes[0].parent_index != PRAKTOR_C11_BT_NO_PARENT) return SALTS_EINVAL;

  /* Reject malformed descriptor ordering and callback contracts before build. */
  for (index = 0u; index < node_count; ++index) {
    if (index != 0u &&
        (nodes[index].parent_index == PRAKTOR_C11_BT_NO_PARENT ||
         nodes[index].parent_index >= index))
      return SALTS_EINVAL;
    if (!callback_contract_valid(&nodes[index], host)) return SALTS_EINVAL;
  }

  impl = (praktor_c11_bt_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->nodes = (praktor_c11_bt_node *)calloc(node_count, sizeof(*impl->nodes));
  if (impl->nodes == NULL) {
    free(impl);
    return SALTS_ENOMEM;
  }
  impl->node_count = node_count;
  impl->host = *host;
  impl->leaf_callback_error = SALTS_OK;

  for (index = 0u; index < node_count; ++index) {
    impl->nodes[index].owner = impl;
    impl->nodes[index].index = index;
    impl->nodes[index].desc = nodes[index];
  }

  status = salts_bt_tree_init(&impl->runtime, tree_config);
  if (status != SALTS_OK) {
    adapter_dispose_partial(impl);
    return status;
  }

  for (index = 0u; index < node_count; ++index) {
    status = adapter_add_node(impl, index);
    if (status != SALTS_OK) {
      adapter_dispose_partial(impl);
      return status;
    }
  }
  status = salts_bt_tree_validate(&impl->runtime);
  if (status != SALTS_OK) {
    adapter_dispose_partial(impl);
    return status;
  }

  tree->impl = impl;
  return SALTS_OK;
}

int praktor_c11_bt_tree_tick(praktor_c11_bt_tree *tree,
                             salts_bt_node_status *out_status) {
  praktor_c11_bt_impl *impl = tree_impl(tree);
  int status;
  if (impl == NULL || out_status == NULL) return SALTS_EINVAL;
  impl->leaf_callback_error = SALTS_OK;
  status = salts_bt_tree_tick(&impl->runtime, out_status);
  if (impl->leaf_callback_error != SALTS_OK) {
    int callback_error = impl->leaf_callback_error;
    impl->leaf_callback_error = SALTS_OK;
    return callback_error;
  }
  return status;
}

int praktor_c11_bt_tree_reset(praktor_c11_bt_tree *tree) {
  praktor_c11_bt_impl *impl = tree_impl(tree);
  if (impl == NULL) return SALTS_EINVAL;
  return salts_bt_tree_reset(&impl->runtime);
}

int praktor_c11_bt_tree_cancel(praktor_c11_bt_tree *tree) {
  praktor_c11_bt_impl *impl = tree_impl(tree);
  if (impl == NULL) return SALTS_EINVAL;
  return salts_bt_tree_cancel(&impl->runtime);
}

int praktor_c11_bt_tree_validate(const praktor_c11_bt_tree *tree) {
  const praktor_c11_bt_impl *impl = tree_const_impl(tree);
  if (impl == NULL) return SALTS_EINVAL;
  return salts_bt_tree_validate(&impl->runtime);
}

int praktor_c11_bt_tree_destroy(praktor_c11_bt_tree *tree) {
  praktor_c11_bt_impl *impl;
  int status;
  if (tree == NULL) return SALTS_EINVAL;
  impl = tree_impl(tree);
  if (impl == NULL) return SALTS_OK;

  status = salts_bt_tree_destroy(&impl->runtime);
  if (status != SALTS_OK) return status;

  free(impl->nodes);
  free(impl);
  tree->impl = NULL;
  return SALTS_OK;
}
