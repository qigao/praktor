#pragma once

#include "dag/task_executor.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace Praktor::Execution {

class ProgramExecutor : public TaskExecutor {
public:
  explicit ProgramExecutor(std::unordered_map<std::string, std::string> base_environment = {});
  ~ProgramExecutor() override = default;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "program"; }

private:
  std::unordered_map<std::string, std::string> base_environment_;
};

std::unique_ptr<TaskExecutor> createProgramExecutor(
    std::unordered_map<std::string, std::string> base_environment = {});

} // namespace Praktor::Execution
