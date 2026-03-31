#ifndef __DECLARATIVE_TREE_EXECUTOR_HPP__
#define __DECLARATIVE_TREE_EXECUTOR_HPP__

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
  std::string getTaskType() const override { return "behavior_tree"; }

private:
  // Convert Praktor BtdslNode to btdsl::Node with variable substitution and context bridge
  btdsl::Node convertNode(const BtdslNode& node, const WorkflowContext& context, const std::string& taskName);

  // Preprocess node parameters: expand Mustache templates {{ variable }}
  void preprocessNodeParams(btdsl::Node& node, const WorkflowContext& context);

  // Context bridge methods for {ctx.*} syntax
  bool hasContextRef(const std::string& param) const;
  std::string extractContextPath(const std::string& param) const;
  std::string resolveContextRef(const std::string& param, const WorkflowContext& ctx) const;

  // Security controls
  bool isAllowedContextRoot(const std::string& root) const;
  bool isSensitivePath(const std::string& path) const;
  std::string sanitizeForShell(const std::string& value) const;
  void logContextAccess(const std::string& path, const std::string& value) const;

  // Bridge Praktor context to BTDSL blackboard
  void contextToBlackboard(const WorkflowContext& context, btdsl::Blackboard& bb);

  // Extract outputs from blackboard to context
  void blackboardToContext(const btdsl::Blackboard& bb, WorkflowContext& context, const std::string& taskName);

  // Parse string value to appropriate type (string, number, bool)
  btdsl::Value parseValue(const std::string& str) const;

  std::unordered_map<std::string, std::string> base_environment_;
};

std::unique_ptr<TaskExecutor> createDeclarativeTreeExecutor(
    std::unordered_map<std::string, std::string> base_environment = {});

}  // namespace Praktor::Execution

#endif  // __DECLARATIVE_TREE_EXECUTOR_HPP__
