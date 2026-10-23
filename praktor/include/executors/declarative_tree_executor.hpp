#pragma once

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"
#include <functional>
#include <unordered_map>

// Only forward declarations are needed at this public API boundary.
// The legacy Executor implementation belongs in the .cpp, not consumers.
namespace actions {
struct Node;
class Blackboard;
}

struct cflow_process_retainer;

namespace Praktor::Execution
{

// Explicitly selected per executor instance. Legacy remains the default for all
// workflow-scheduler call sites; C11 is only available in qualified builds.
enum class ActionTreeBackend { Legacy, C11 };

class DeclarativeTreeExecutor : public TaskExecutor
{
public:
  explicit DeclarativeTreeExecutor(
      std::unordered_map<std::string, std::string> base_environment = {},
      ActionTreeBackend backend = ActionTreeBackend::Legacy,
      cflow_process_retainer* recovery_owner = nullptr);
  ~DeclarativeTreeExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "action_orchestration"; }

private:
  // Convert Praktor OrchNode to actions::Node with variable substitution and context bridge
  actions::Node convertNode(const OrchNode& node, const WorkflowContext& context, const std::string& taskName);

  // Context bridge methods for {ctx.*} syntax
  bool hasContextRef(const std::string& param) const;
  std::string extractContextPath(const std::string& param) const;
  std::string resolveContextRef(
      const std::string& param,
      const WorkflowContext& ctx,
      const std::function<std::string(const std::string&)>& transform_value = {}) const;

  // Security controls
  bool isAllowedContextRoot(const std::string& root) const;
  bool isSensitivePath(const std::string& path) const;
  std::string sanitizeForShell(const std::string& value) const;
  void logContextAccess(const std::string& path, const std::string& value) const;

  // Bridge Praktor context to actions blackboard
  void contextToBlackboard(const WorkflowContext& context, actions::Blackboard& bb);

  // Extract outputs from blackboard to context
  void blackboardToContext(const actions::Blackboard& bb, WorkflowContext& context, const std::string& taskName);

  std::unordered_map<std::string, std::string> base_environment_;
  ActionTreeBackend backend_ = ActionTreeBackend::Legacy;
  cflow_process_retainer* recovery_owner_ = nullptr;
};

std::unique_ptr<TaskExecutor> createDeclarativeTreeExecutor(
    std::unordered_map<std::string, std::string> base_environment = {},
    ActionTreeBackend backend = ActionTreeBackend::Legacy,
    cflow_process_retainer* recovery_owner = nullptr);

}  // namespace Praktor::Execution
