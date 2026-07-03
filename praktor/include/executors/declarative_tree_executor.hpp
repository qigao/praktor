#pragma once

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"
#include "core/executor.hpp"

#include <unordered_map>

namespace Praktor::Execution
{

class DeclarativeTreeExecutor : public TaskExecutor
{
public:
  explicit DeclarativeTreeExecutor(std::unordered_map<std::string, std::string> base_environment = {});
  ~DeclarativeTreeExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "action_orchestration"; }

private:
  // Convert Praktor OrchNode to actions::Node with variable substitution and context bridge
  actions::Node convertNode(const OrchNode& node, const WorkflowContext& context, const std::string& taskName);

  // Context bridge methods for {ctx.*} syntax
  bool hasContextRef(const std::string& param) const;
  std::string extractContextPath(const std::string& param) const;
  std::string resolveContextRef(const std::string& param, const WorkflowContext& ctx) const;

  // Security controls
  bool isAllowedContextRoot(const std::string& root) const;
  bool isSensitivePath(const std::string& path) const;
  std::string sanitizeForShell(const std::string& value) const;
  void logContextAccess(const std::string& path, const std::string& value) const;

  // Bridge Praktor context to actions blackboard
  void contextToBlackboard(const WorkflowContext& context, actions::Blackboard& bb);

  // Extract outputs from blackboard to context
  void blackboardToContext(const actions::Blackboard& bb, WorkflowContext& context, const std::string& taskName);

  // Parse string value to appropriate type (string, number, bool)
  actions::Value parseValue(const std::string& str) const;

  std::unordered_map<std::string, std::string> base_environment_;
};

std::unique_ptr<TaskExecutor> createDeclarativeTreeExecutor(
    std::unordered_map<std::string, std::string> base_environment = {});

}  // namespace Praktor::Execution
