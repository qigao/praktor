#ifndef PRAKTOR_ACTIONS_C11_TREE_ADAPTER_H
#define PRAKTOR_ACTIONS_C11_TREE_ADAPTER_H

#include <salts/behavior_tree.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRAKTOR_C11_BT_NO_PARENT ((size_t)-1)

/* A zero-initialized owner for one compiled action-tree activation. */
typedef struct praktor_c11_bt_tree {
  void *impl;
} praktor_c11_bt_tree;

/*
 * Descriptors are supplied in pre-order: the root is index zero, and every
 * child appears after its parent. Sibling order in this array is execution
 * order. parent_index uses PRAKTOR_C11_BT_NO_PARENT only for the root.
 *
 * user data and action parameters do not live in this descriptor. The host
 * keeps its plan/payload storage alive until destroy and callbacks receive
 * the descriptor's zero-based index to access it.
 */
typedef struct praktor_c11_bt_node_desc {
  size_t parent_index;
  salts_bt_node_kind kind;
  size_t limit;
  uint64_t duration_ns;
  bool condition_callback;
  bool parallel_hooks;
  uint32_t dynamic_parameter_mask;
} praktor_c11_bt_node_desc;

/*
 * Host callbacks stay on the one thread which calls tick/reset/cancel/destroy.
 * The adapter owns Salts tree state; the host owns its plan, action payloads,
 * workflow values and any external leases/resources. A leaf returning RUNNING
 * must be resumable from a later tick. halt_leaf must synchronously initiate
 * cancellation/cleanup and must not release state still referenced by an
 * asynchronous completion. Shell/process leaves should delegate process
 * ownership to Salts::Process, not create another process owner.
 */
/* Optional host-owned value store callback results. */
typedef enum praktor_c11_bt_value_result {
  PRAKTOR_C11_BT_VALUE_OK = 0,
  PRAKTOR_C11_BT_VALUE_NOT_FOUND = 1,
  PRAKTOR_C11_BT_VALUE_BUFFER_TOO_SMALL = 2,
  PRAKTOR_C11_BT_VALUE_ERROR = 3
} praktor_c11_bt_value_result;

/* Borrowed typed leaf payload. Strings and arrays remain valid for the
 * callback duration; a host must copy anything retained across ticks. */
typedef enum praktor_c11_leaf_value_kind {
  PRAKTOR_C11_LEAF_STRING = 0,
  PRAKTOR_C11_LEAF_NUMBER = 1,
  PRAKTOR_C11_LEAF_BOOLEAN = 2
} praktor_c11_leaf_value_kind;
typedef struct praktor_c11_leaf_parameter {
  const char *name;
  praktor_c11_leaf_value_kind kind;
  const char *text;
  double number;
  bool boolean;
} praktor_c11_leaf_parameter;
typedef struct praktor_c11_leaf_request {
  size_t node_index;
  const char *action;
  const praktor_c11_leaf_parameter *parameters;
  size_t parameter_count;
  /* Root-scoped helpers must decline this request. A branch-aware host routes
   * it to its own branch state using the stable node index and compiled plan. */
  bool in_parallel_branch;
  /* Ephemeral identity for one Session, never dereferenced or serialized by a
   * host. It disambiguates equal node indices in different live Sessions. */
  const void *session_owner;
} praktor_c11_leaf_request;

typedef struct praktor_c11_bt_host {
  int (*tick_leaf)(void *user, size_t node_index,
                   salts_bt_node_status *out_status);
  void (*halt_leaf)(void *user, size_t node_index);

  /* For callback-predicate forms of IfThenElse, Precondition and WhileDo. */
  int (*condition)(void *user, size_t node_index,
                   salts_bt_node_status *out_status);
  int (*select_child)(void *user, size_t node_index,
                      size_t *out_child_index);


  /* All five are required together when a node enables parallel_hooks.
   * parallel_abort must synchronously clean up branch-local resources when
   * reset/cancel/error interrupts an activation or a later hook fails. */
  int (*parallel_enter)(void *user, size_t node_index, size_t child_index,
                        bool first_activation);
  int (*parallel_step)(void *user, size_t node_index, size_t child_index,
                       salts_bt_node_status child_status);
  int (*parallel_join)(void *user, size_t node_index,
                       salts_bt_node_status aggregate_status);
  int (*parallel_finish)(void *user, size_t node_index, size_t child_index,
                         salts_bt_node_status child_status);
  void (*parallel_abort)(void *user, size_t node_index, size_t child_index);

  int (*pipeline_prepare)(void *user, size_t node_index, size_t child_index);
  int (*pipeline_complete)(void *user, size_t node_index, size_t child_index,
                           salts_bt_node_status child_status);
  void (*pipeline_abort)(void *user, size_t node_index, size_t child_index);

  int (*entry_changed)(void *user, size_t node_index, bool *out_changed);

  int (*queue_begin)(void *user, size_t node_index, bool *out_has_item);
  int (*queue_finish)(void *user, size_t node_index, bool *out_has_more);
  void (*queue_abort)(void *user, size_t node_index);

  void *user;

  /*
   * Optional host-owned value store. Supply all three callbacks together and
   * a non-NULL value_user. value_get with NULL/zero output is a size query:
   * return BUFFER_TOO_SMALL and write required bytes (excluding NUL), or
   * NOT_FOUND. Normal reads require capacity > required bytes and NUL-terminate.
   */
  int (*value_get)(void *value_user, const char *key, char *out_value,
                   size_t out_capacity, size_t *out_size);
  int (*value_set)(void *value_user, const char *key, const char *value);
  int (*value_contains)(void *value_user, const char *key, int *out_contains);
  void *value_user;

  /* Resolve Blackboard-backed decorator values on each node tick. */
  int (*resolve_dynamic_parameters)(void *user, size_t node_index,
                                    salts_bt_node_parameters *out_params);
  /* Session AST bridge: use either tick_leaf or this typed dispatcher.
   * RUNNING uses the same dispatcher until halt/terminal completion.
   * Supply exactly one halt callback: indexed halt_leaf or halt_typed_leaf.
   * Stateful typed hosts should use halt_typed_leaf for scoped cancellation. */
  int (*tick_typed_leaf)(void *user, const praktor_c11_leaf_request *request,
                         salts_bt_node_status *out_status);
  void (*halt_typed_leaf)(void *user, const praktor_c11_leaf_request *request);
} praktor_c11_bt_host;

/*
 * Copy a validated, parent-first node plan into a Salts C11 tree. The adapter
 * copies descriptors and callbacks, but host.user and its payloads are
 * borrowed until destroy. The output handle must be zero-initialized.
 *
 * This is deliberately only a tree-runtime bridge: it does not parse YAML,
 * own a Praktor WorkflowContext/blackboard, spawn threads, or own processes.
 * Invalid topology and missing callback contracts fail before any leaf runs.
 */
int praktor_c11_bt_tree_init(praktor_c11_bt_tree *tree,
                             const salts_bt_tree_config *tree_config,
                             const praktor_c11_bt_node_desc *nodes,
                             size_t node_count,
                             const praktor_c11_bt_host *host);

/* One cooperative root tick. The output status is valid only on SALTS_OK. */
int praktor_c11_bt_tree_tick(praktor_c11_bt_tree *tree,
                             salts_bt_node_status *out_status);

/* Clear all execution state while retaining topology; halts RUNNING leaves. */
int praktor_c11_bt_tree_reset(praktor_c11_bt_tree *tree);

/* Cancel the current activation and clear state; the tree remains reusable. */
int praktor_c11_bt_tree_cancel(praktor_c11_bt_tree *tree);

/* Validate the fully compiled tree before starting or resuming it. */
int praktor_c11_bt_tree_validate(const praktor_c11_bt_tree *tree);

/* Halt active descendants, run adapter abort hooks, and release tree-owned memory. */
int praktor_c11_bt_tree_destroy(praktor_c11_bt_tree *tree);

#ifdef __cplusplus
}
#endif

#endif /* PRAKTOR_ACTIONS_C11_TREE_ADAPTER_H */
