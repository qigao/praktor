#ifndef SALTS_BEHAVIOR_TREE_H
#define SALTS_BEHAVIOR_TREE_H

#include <salts.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A zero-initialized owning handle. It must not be copied while initialized. */
typedef struct salts_bt_tree {
  void *impl;
} salts_bt_tree;

typedef uint32_t salts_bt_node_id;

#define SALTS_BT_INVALID_NODE_ID ((salts_bt_node_id)0u)
#define SALTS_BT_MAX_DEPTH 256u
#define SALTS_BT_MAX_CONTROL_ITERATIONS 4096u

typedef enum salts_bt_node_kind {
  SALTS_BT_NODE_LEAF = 0,
  SALTS_BT_NODE_SEQUENCE = 1,
  SALTS_BT_NODE_FALLBACK = 2,
  SALTS_BT_NODE_INVERTER = 3,
  SALTS_BT_NODE_FORCE_SUCCESS = 4,
  SALTS_BT_NODE_FORCE_FAILURE = 5,
  SALTS_BT_NODE_RETRY = 6,
  SALTS_BT_NODE_REPEAT = 7,
  SALTS_BT_NODE_TIMEOUT = 8,
  SALTS_BT_NODE_DELAY = 9,
  SALTS_BT_NODE_PARALLEL = 10,
  SALTS_BT_NODE_REACTIVE_SEQUENCE = 11,
  SALTS_BT_NODE_RUN_ONCE = 12,
  SALTS_BT_NODE_IF_THEN_ELSE = 13,
  SALTS_BT_NODE_PRECONDITION = 14,
  SALTS_BT_NODE_WHILE_DO = 15,
  SALTS_BT_NODE_SWITCH = 16,
  SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE = 17,
  SALTS_BT_NODE_PIPELINE_SEQUENCE = 18,
  SALTS_BT_NODE_ENTRY_UPDATED = 19,
  SALTS_BT_NODE_CONSUME_QUEUE = 20,
  SALTS_BT_NODE_REACTIVE_FALLBACK = 21
} salts_bt_node_kind;

typedef enum salts_bt_node_status {
  SALTS_BT_SUCCESS = 0,
  SALTS_BT_FAILURE = 1,
  SALTS_BT_RUNNING = 2
} salts_bt_node_status;

/*
 * max_nodes and max_depth are hard construction limits. max_depth includes
 * the root. max_depth must be in [1, SALTS_BT_MAX_DEPTH].
 */
typedef struct salts_bt_tree_config {
  size_t max_nodes;
  size_t max_depth;
  size_t max_control_iterations;
} salts_bt_tree_config;

/*
 * All callback user contexts are borrowed: the tree never frees them, and they
 * must remain valid until salts_bt_tree_destroy() returns SALTS_OK. Callbacks
 * run synchronously on the tree driver thread. They must not recursively tick,
 * mutate, reset, cancel, or destroy the same tree; reentrant operations are
 * rejected while a tree operation is active (with SALTS_EBUSY where applicable).
 *
 * Leaf handlers return shared/behavior-tree status only, not a Salts error code.
 * Represent an operational leaf failure as SALTS_BT_FAILURE and retain any
 * detailed error in caller-owned state. An invalid leaf status returns
 * SALTS_EINVAL and aborts the current tick with normal lifecycle cleanup.
 */
typedef salts_bt_node_status (*salts_bt_leaf_fn)(void *user);
typedef void (*salts_bt_leaf_halt_fn)(void *user);

/* Predicate callbacks return a Salts status code; SALTS_OK means the output
 * is one of SUCCESS/FAILURE/RUNNING. RUNNING means condition evaluation is
 * pending; the guarded branch is not ticked and a previously active branch is
 * halted. They run synchronously on the tree driver thread. */
typedef int (*salts_bt_condition_fn)(void *user,
                                     salts_bt_node_status *out_condition_status);
/* Selector callbacks return a Salts status code and a zero-based child index. */
typedef int (*salts_bt_selector_fn)(void *user, size_t *out_child_index);
typedef int (*salts_bt_pipeline_prepare_fn)(void *user, size_t child_index);
typedef int (*salts_bt_pipeline_complete_fn)(void *user, size_t child_index,
                                             salts_bt_node_status child_status);
typedef void (*salts_bt_pipeline_abort_fn)(void *user, size_t child_index);
typedef int (*salts_bt_entry_changed_fn)(void *user, bool *out_changed);
typedef int (*salts_bt_queue_begin_fn)(void *user, bool *out_has_item);
typedef int (*salts_bt_queue_finish_fn)(void *user, bool *out_has_more);
typedef void (*salts_bt_queue_abort_fn)(void *user);

/*
 * Optional per-tick values for control nodes whose parameter may be read from
 * a live host context (for example, a Praktor Blackboard). A resolver updates
 * only the fields named by the mask registered on its node. The runtime calls
 * it before any node/descendant side effects on every visit/tick of that node.
 */
#define SALTS_BT_DYNAMIC_PARAMETER_LIMIT ((uint32_t)0x01u)
#define SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS ((uint32_t)0x02u)
typedef struct salts_bt_node_parameters {
  size_t limit;
  uint64_t duration_ns;
} salts_bt_node_parameters;
typedef int (*salts_bt_node_parameters_fn)(void *user,
                                           salts_bt_node_id node_id,
                                           salts_bt_node_parameters *out_params);

/* Parallel host-context hooks run on the single tree driver thread.
 * Enter selects/prepares branch-local context before every active branch tick;
 * first_activation is true once per activation. Step runs after each branch tick,
 * including RUNNING, so the host can stage per-branch progress. Join runs once
 * after all branches have been ticked and aggregate status is known; it commits
 * staged writes before the parent resumes. Finish runs once per newly terminal
 * branch after Join succeeds. Abort runs after descendants are halted during
 * reset/cancel or error. Join should commit atomically if it can fail. */
typedef int (*salts_bt_parallel_enter_fn)(void *user, size_t child_index,
                                          bool first_activation);
typedef int (*salts_bt_parallel_step_fn)(void *user, size_t child_index,
                                         salts_bt_node_status child_status);
/* Called once after all branches tick; commits staged outputs before the parent resumes. */
typedef int (*salts_bt_parallel_join_fn)(void *user,
                                         salts_bt_node_status parallel_status);
typedef int (*salts_bt_parallel_finish_fn)(void *user, size_t child_index,
                                           salts_bt_node_status child_status);
typedef void (*salts_bt_parallel_abort_fn)(void *user, size_t child_index);

typedef struct salts_bt_leaf_ops {
  salts_bt_leaf_fn tick;
  /* Optional: synchronously stop work while this leaf is RUNNING. */
  salts_bt_leaf_halt_fn halt;
} salts_bt_leaf_ops;

/* Set a useful bounded default before changing individual limits. */
void salts_bt_tree_config_init(salts_bt_tree_config *config);

/*
 * Initialize a zero-initialized tree. The implementation allocates a fixed
 * table for max_nodes valid node IDs plus the reserved invalid-ID slot during
 * initialization and does not grow it later. Returns SALTS_OK, SALTS_EINVAL
 * for an invalid contract or SALTS_ENOMEM.
 */
int salts_bt_tree_init(salts_bt_tree *tree,
                       const salts_bt_tree_config *config);

/*
 * Add a node. parent_id == SALTS_BT_INVALID_NODE_ID declares the single root;
 * otherwise the node is appended to its parent's child list. Parents must be
 * created before children. Sequence/Fallback/Parallel/ReactiveSequence/
 * ReactiveFallback/Switch accept multiple children; simple decorators enforce
 * single-child arity. IfThenElse accepts callback + 1-2 branches or an in-tree
 * condition + 1-2 branches. Precondition/WhileDo accept callback + one body or
 * an in-tree condition + body. Leaves cannot have children. Leaves require a
 * handler. Switch, PipelineSequence, EntryUpdated, and ConsumeQueue require
 * their typed add_* factories because their callback configuration cannot be
 * attached later; the generic add functions reject those node kinds.
 * IDs are stable for the lifetime of the tree and are not reused after reset.
 *
 * Capacity exhaustion returns SALTS_ENOBUFS. Invalid topology/contracts return
 * SALTS_EINVAL. A tree cannot be mutated while it is ticking (SALTS_EBUSY).
 */
int salts_bt_tree_add_node(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           salts_bt_node_kind kind,
                           salts_bt_leaf_fn handler,
                           void *user,
                           salts_bt_node_id *out_node_id);

/*
 * Parameterized single-child nodes use limit: RETRY means maximum attempts
 * including the first attempt (must be positive); REPEAT means completed child
 * cycles (zero is a valid immediate SUCCESS). KeepRunningUntilFailure counts
 * successful child ticks and returns FAILURE when the budget is exhausted.
 * The value is bounded by the tree configuration. Other node kinds require
 * limit == 0.
 */
int salts_bt_tree_add_node_with_limit(salts_bt_tree *tree,
                                      salts_bt_node_id parent_id,
                                      salts_bt_node_kind kind,
                                      salts_bt_leaf_fn handler,
                                      void *user,
                                      size_t limit,
                                      salts_bt_node_id *out_node_id);

/*
 * Add a leaf with an optional halt callback. When reset/destroy aborts a leaf
 * whose last status was RUNNING, halt is called at most once before the user
 * can release its borrowed state. Callbacks execute synchronously on the
 * single driver thread and must not mutate or destroy this tree.
 */
int salts_bt_tree_add_leaf(salts_bt_tree *tree,
                           salts_bt_node_id parent_id,
                           const salts_bt_leaf_ops *ops,
                           void *user,
                           salts_bt_node_id *out_node_id);

/*
 * Add a Timeout or Delay decorator with a monotonic duration in nanoseconds.
 * Timeout returns FAILURE and halts a still-running descendant once the
 * deadline is reached. Delay returns RUNNING until its delay elapses, then
 * starts ticking its child. A zero duration is immediate for Delay and an
 * immediate timeout for Timeout.
 */
int salts_bt_tree_add_timed_node(salts_bt_tree *tree,
                                 salts_bt_node_id parent_id,
                                 salts_bt_node_kind kind,
                                 uint64_t duration_ns,
                                 salts_bt_node_id *out_node_id);

/*
 * Attach a per-tick parameter resolver to an existing node. LIMIT is valid for
 * Retry, Repeat, WhileDo and KeepRunningUntilFailure; DURATION_NS is valid for
 * Timeout and Delay. These flags are mutually exclusive for current node kinds.
 * The callback is called on the tree driver thread before the node is executed.
 * It may return SALTS_OK or a Salts error; callback errors abort the tick and
 * run the normal descendant/host-resource cleanup. Dynamic limit zero means
 * Retry/KeepRunningUntilFailure -> FAILURE, Repeat -> SUCCESS, WhileDo ->
 * SUCCESS without evaluating its condition/body. Values above the tree's hard
 * control-iteration limit are rejected before child side effects. Attaching,
 * replacing, or removing a resolver is an activation boundary: the affected
 * subtree is reset before the new configuration is installed, running leaves
 * are halted synchronously, and persistent caches in that subtree are cleared.
 * Set resolver to NULL with mask 0 and user NULL to restore constructor-time
 * parameters and detach a previously attached resolver.
 */
int salts_bt_tree_set_node_parameters_resolver(
    salts_bt_tree *tree, salts_bt_node_id node_id, uint32_t parameter_mask,
    salts_bt_node_parameters_fn resolver, void *user);

/*
 * Add an IfThenElse, Precondition, or WhileDo node with a status predicate.
 * In callback form, IfThenElse has one or two branch children and
 * Precondition/WhileDo have one body child. The callback may return RUNNING.
 *
 * Alternatively, create the node with salts_bt_tree_add_node(_with_limit)
 * without a predicate, and make its first child the condition tree:
 * IfThenElse takes condition + then + optional else; Precondition and WhileDo
 * take condition + body. All children then share the same tree lifecycle.
 *
 * WhileDo limit is the maximum number of successful body iterations per
 * activation and must be positive and bounded by max_control_iterations.
 * WhileDo is cooperative: each tick evaluates the condition and ticks at most
 * one body iteration. After a successful body iteration it returns RUNNING
 * unless the limit is reached; a false condition returns SUCCESS and a failed
 * body returns FAILURE. The activation counter survives RUNNING ticks and is
 * cleared by terminal completion or explicit reset/cancel. This bounds work per
 * driver tick; it differs from the legacy Praktor loop, which may run multiple
 * successful iterations in one call.
 */
int salts_bt_tree_add_condition_node(salts_bt_tree *tree,
                                     salts_bt_node_id parent_id,
                                     salts_bt_node_kind kind,
                                     salts_bt_condition_fn condition,
                                     void *user,
                                     size_t limit,
                                     salts_bt_node_id *out_node_id);

/*
 * Add a Switch node. The selector chooses a zero-based child index on every
 * tick. Out-of-range selections fail. When the selected child changes during
 * RUNNING, the prior child is reset/halted before the new case is ticked.
 */
int salts_bt_tree_add_switch_node(salts_bt_tree *tree,
                                  salts_bt_node_id parent_id,
                                  salts_bt_selector_fn selector,
                                  void *user,
                                  salts_bt_node_id *out_node_id);

/*
 * Add a host-context Parallel. enter selects/prepares the branch-local context
 * before each child tick; first_activation is true on its first tick after
 * construction/reset. finish commits/merges or otherwise closes a terminal
 * branch. If reset/cancel or another branch error interrupts an active branch,
 * optional abort runs after the descendant is halted/reset. The caller owns
 * branch snapshots, isolation and merge policy; this runtime stores no
 * blackboard values and launches no worker threads.
 *
 * Use salts_bt_tree_add_node(..., SALTS_BT_NODE_PARALLEL, ...) for an unhooked
 * Parallel whose leaves already manage their own isolated contexts.
 */
int salts_bt_tree_add_parallel_node(salts_bt_tree *tree,
                                    salts_bt_node_id parent_id,
                                    salts_bt_parallel_enter_fn enter,
                                    salts_bt_parallel_step_fn step,
                                    salts_bt_parallel_join_fn join,
                                    salts_bt_parallel_finish_fn finish,
                                    salts_bt_parallel_abort_fn abort,
                                    void *user,
                                    salts_bt_node_id *out_node_id);

/*
 * PipelineSequence invokes prepare before first ticking each child and complete
 * when that child becomes terminal. SUCCESS advances to the next child;
 * FAILURE terminates; RUNNING resumes the same child next tick without another
 * prepare call. Empty pipelines succeed. If the tree is reset/destroyed while
 * a child is active, optional abort runs after the child is halted/reset. Abort
 * is also called if prepare/child/complete fails while its context is active.
 * This lets a host clean its own context without coupling this library to it.
 */
int salts_bt_tree_add_pipeline_node(salts_bt_tree *tree,
                                    salts_bt_node_id parent_id,
                                    salts_bt_pipeline_prepare_fn prepare,
                                    salts_bt_pipeline_complete_fn complete,
                                    salts_bt_pipeline_abort_fn abort,
                                    void *user,
                                    salts_bt_node_id *out_node_id);

/*
 * EntryUpdated asks changed on each tick. The first tick always executes the
 * child. A changed result resets/halts the old child and reruns it. If unchanged,
 * an active RUNNING child continues; otherwise its last terminal result is
 * cached until the next change. The callback owns and updates its comparison
 * snapshot.
 */
int salts_bt_tree_add_entry_updated_node(salts_bt_tree *tree,
                                         salts_bt_node_id parent_id,
                                         salts_bt_entry_changed_fn changed,
                                         void *user,
                                         salts_bt_node_id *out_node_id);

/*
 * ConsumeQueue calls begin before each child iteration. begin returns has_item
 * and prepares the current item in host-owned context. On child FAILURE the
 * node fails; on RUNNING it resumes the same child; on SUCCESS, finish reports
 * whether another item exists. If more items exist, the node yields RUNNING and
 * begins the next iteration on the next tick; an empty/exhausted queue fails,
 * matching Praktor's current ConsumeQueue contract.
 */
int salts_bt_tree_add_consume_queue_node(salts_bt_tree *tree,
                                         salts_bt_node_id parent_id,
                                         salts_bt_queue_begin_fn begin,
                                         salts_bt_queue_finish_fn finish,
                                         salts_bt_queue_abort_fn abort,
                                         void *user,
                                         salts_bt_node_id *out_node_id);

/*
 * Tick the root once. Sequence stops at FAILURE or RUNNING and resumes the
 * active child on the next tick. Fallback advances after FAILURE and stops at
 * SUCCESS or RUNNING. An empty Sequence succeeds; an empty Fallback fails.
 *
 * Terminal control-flow outcomes reset that subtree, so a later re-entry starts
 * from the first child. This initial runtime is single-driver and synchronous;
 * RUNNING is cooperative re-tick, not hidden background work. A tree is not
 * internally synchronized; all operations on one tree must be serialized on
 * a single driver thread.
 *
 * On API error, the function returns a Salts status code. On success it stores
 * one of SALTS_BT_SUCCESS, SALTS_BT_FAILURE, SALTS_BT_RUNNING in out_status.
 */
int salts_bt_tree_tick(salts_bt_tree *tree,
                       salts_bt_node_status *out_status);

/* Validate complete topology before any leaf is allowed to run. */
int salts_bt_tree_validate(const salts_bt_tree *tree);

/*
 * Clear all per-node execution state without changing topology. Unlike internal
 * terminal-subtree resets, explicit reset also clears RunOnce and EntryUpdated
 * cached state. RUNNING descendants are halted and host adapter abort hooks run.
 */
int salts_bt_tree_reset(salts_bt_tree *tree);

/*
 * Cancel the current activation: synchronously halt RUNNING descendants, abort
 * any prepared host adapter context, and clear all per-node and cached state
 * while preserving topology. The tree may be ticked again after cancellation. This call is
 * serialized with all other tree operations and returns SALTS_EBUSY if invoked
 * reentrantly from a callback.
 */
int salts_bt_tree_cancel(salts_bt_tree *tree);

/* Copy the current root ID / node count. Returns SALTS_EINVAL if no root exists. */
int salts_bt_tree_get_root(const salts_bt_tree *tree,
                           salts_bt_node_id *out_root);
int salts_bt_tree_get_node_count(const salts_bt_tree *tree,
                                 size_t *out_count);

/*
 * Destroy an initialized tree. It returns SALTS_EBUSY if called while any
 * tree operation or synchronous callback is active; otherwise it halts and
 * cleans active descendants/adapter contexts, frees only tree-owned memory,
 * and clears the handle. User pointers remain caller-owned.
 */
int salts_bt_tree_destroy(salts_bt_tree *tree);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_BEHAVIOR_TREE_H */
