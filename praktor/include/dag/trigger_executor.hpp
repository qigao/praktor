#pragma once

#include "workflow_context.hpp"
#include "yml/task.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Praktor {
namespace Execution {

// Maximum number of nested trigger hops allowed in a single trigger chain.
// Trigger execution is synchronous recursion; a workflow whose triggers
// re-enter each other (e.g. A on_complete -> B, B on_complete -> A) would
// otherwise recurse without bound until the stack overflows. A terminating
// chain (each task triggers the next) is far below this bound.
inline constexpr size_t kMaxTriggerChainDepth = 64;

using ExecutionCallback = std::function<bool(const Task &, size_t depth)>;

/**
 * @class TriggerExecutor
 * @brief Executes event-driven triggers (on_success, on_failure, on_complete)
 */
class TriggerExecutor {
public:
  /**
   * @brief Configure the maximum nested trigger-chain depth
   *
   * Trigger execution is synchronous recursion; the guard turns runaway
   * circular trigger chains into a controlled workflow failure instead of a
   * stack overflow. Keep this at or below the default unless the workflow
   * genuinely needs very deep trigger chains.
   */
  void setMaxTriggerDepth(size_t depth) { max_trigger_depth_ = depth; }

  size_t maxTriggerDepth() const { return max_trigger_depth_; }

  /**
   * @brief Execute triggers for a task based on its completion status
   * @param task The task that completed
   * @param success Whether the task succeeded
   * @param context The workflow context
   * @param all_tasks All tasks in the workflow (for trigger task lookup)
   * @param callback Function to execute a task
   */
  bool executeTriggers(const Task &task, bool success, WorkflowContext &context,
                       const std::vector<Task> &all_tasks, ExecutionCallback callback,
                       size_t depth = 0);

private:
  bool executeTriggerAction(const TriggerAction &action, WorkflowContext &context,
                            const std::vector<Task> &all_tasks, ExecutionCallback callback,
                            std::unordered_set<std::string> &visited_triggers, size_t depth);

  size_t max_trigger_depth_ = kMaxTriggerChainDepth;
};

} // namespace Execution
} // namespace Praktor
