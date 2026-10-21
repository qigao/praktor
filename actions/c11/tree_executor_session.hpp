#ifndef PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP
#define PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP

#include "tree_plan.hpp"
#include "core/executor.hpp"

#include <memory>
#include <string>

namespace actions::c11 {

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
 * registered Executor leaves to branch-local state: such leaves still execute
 * against the session's single Blackboard/ExecutionContext. A host that needs
 * isolation must explicitly dispatch the relevant leaves through tick_leaf and
 * manage branch state in its own callback/user-data contract; otherwise branch
 * isolation is not provided. Custom PipelineSequence and ConsumeQueue hook
 * groups must provide their prepare/begin, completion/finish and abort callbacks
 * together so cancellation has an explicit cleanup path.
 *
 * The tree and all callbacks are single-driver and synchronous. The caller
 * must keep Executor, Blackboard, ExecutionContext, hook user data and any
 * external resources alive until this session is destroyed.
 */
class TreeExecutorSession {
public:
  TreeExecutorSession(const actions::Tree& tree,
                      actions::Executor& executor,
                      actions::Blackboard& blackboard,
                      actions::ExecutionContext& execution_context,
                      const salts_bt_tree_config& limits,
                      const praktor_c11_bt_host* host_hooks = nullptr);
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
  std::unique_ptr<Impl> impl_;
};

} // namespace actions::c11

#endif /* PRAKTOR_ACTIONS_C11_TREE_EXECUTOR_SESSION_HPP */
