#ifndef PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP
#define PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP

#include "tree_plan.hpp"
#include <salts/process_retainer.h>

namespace actions {
class Blackboard;
class Executor;
class ExecutionContext;
}

#include <functional>
#include <memory>
#include <string>

namespace actions::c11 {

using SubTreeResolver =
    std::function<const actions::Tree*(const std::string& tree_name)>;

/*
 * An opt-in execution bridge for the compiled C11 tree.
 *
 * Ordinary registered action leaves are executed through actions::Executor
 * against the caller-owned Blackboard and ExecutionContext. The session owns
 * a stable copy of the AST because TreePlan's source_nodes refer into it.
 *
 * Shell runs through Salts::Process by default: the session drives bounded
 * asynchronous stdin/stdout/stderr operations, retains each borrowed buffer
 * until its terminal completion, and closes/drains the process owner before
 * releasing callback state. The session never falls back to the legacy
 * ShellExecutor path. A custom leaf hook may override Shell; SALTS_ENOTSUP on
 * an inactive Shell selects the built-in Salts::Process path, but a live host
 * or Executor activation can never switch owners midway through a tick.
 * A registered Executor leaf that returns RUNNING requires a halt_leaf hook.
 *
 * PipelineSequence, EntryUpdated and ConsumeQueue have built-in
 * Blackboard-backed hooks (ConsumeQueue uses the shared C JSON parser).
 * Parallel currently requires host-supplied enter/step/join/finish/abort hooks,
 * because its branch-local Blackboard/ExecutionContext isolation and commit
 * policy are still host-defined. These lifecycle hooks do not themselves route
 * registered Executor leaves to branch-local state. An unclaimed leaf
 * under Parallel fails closed instead of falling back to the shared
 * Blackboard/ExecutionContext. The host must dispatch every such leaf via
 * tick_leaf and manage branch state in its callback/user-data contract.
 * Custom PipelineSequence and ConsumeQueue hook
 * groups must provide their prepare/begin, completion/finish and abort callbacks
 * together so cancellation has an explicit cleanup path.
 *
 * Decorator numeric parameters expressed as exact Blackboard substitutions
 * (for example, Retry.num_attempts="{attempts}", Timeout.timeout_ms="{timeout}",
 * or Delay.delay_ms="{delay}") are resolved from the live Blackboard before
 * each visit/tick rather than frozen at session construction. Every visit
 * revalidates the value against the node's numeric and configured iteration /
 * duration bounds. A missing key, non-integral limit, negative duration, or
 * out-of-budget limit fails the tick; the underlying Salts runtime then halts
 * active descendants and runs the normal host-resource cleanup path.
 *
 * The tree and all callbacks are single-driver and synchronous. The caller
 * must keep Executor, Blackboard, ExecutionContext, hook user data and any
 * external resources alive until this session is destroyed.
 *
 * Salts::Process Shell requires a caller-owned, fixed-capacity
 * cflow_process_retainer. A slot is reserved before spawning. If shutdown
 * cannot quiesce, the Process plus its stable completion_user and borrowed
 * buffers are handed to that owner before the Session is released. The
 * ServiceHost/Workflow owner must keep driving the retainer until settled.
 * Without one, built-in Shell admission fails closed; no legacy fallback.
 * Permanent driver faults occupy a bounded retainer slot rather than
 * permitting unsafe destruction or invented terminal callbacks.
 */
class TreeExecutorSession {
public:
  TreeExecutorSession(const actions::Tree& tree,
                      actions::Executor& executor,
                      actions::Blackboard& blackboard,
                      actions::ExecutionContext& execution_context,
                      const salts_bt_tree_config& limits,
                      const praktor_c11_bt_host* host_hooks = nullptr,
                       cflow_process_retainer* recovery_owner = nullptr,
                       uint32_t shell_close_budget_ms = 3000u);

  /*
   * Explicit SubTree registry injection. The resolver is called only during
   * construction; returned trees must remain valid until that expansion
   * finishes. Literal selectors only: dynamic topology selectors fail closed.
   */
  TreeExecutorSession(const actions::Tree& tree,
                      actions::Executor& executor,
                      actions::Blackboard& blackboard,
                      actions::ExecutionContext& execution_context,
                      const salts_bt_tree_config& limits,
                      const SubTreeResolver& subtree_resolver,
                      const praktor_c11_bt_host* host_hooks = nullptr,
                      cflow_process_retainer* recovery_owner = nullptr,
                      uint32_t shell_close_budget_ms = 3000u);
  ~TreeExecutorSession();

  TreeExecutorSession(const TreeExecutorSession&) = delete;
  TreeExecutorSession& operator=(const TreeExecutorSession&) = delete;
  TreeExecutorSession(TreeExecutorSession&&) = delete;
  TreeExecutorSession& operator=(TreeExecutorSession&&) = delete;

  int tick(salts_bt_node_status* out_status);
  int reset();
  int cancel();
  int validate() const;

  const actions::Node* sourceNode(size_t node_index) const noexcept;
  const TreePlan& plan() const noexcept;
  const std::string& lastError() const noexcept;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

} // namespace actions::c11

#endif /* PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP */
