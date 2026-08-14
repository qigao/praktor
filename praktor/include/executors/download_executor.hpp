#pragma once

#include "dag/task_executor.hpp"

#include <memory>

namespace Praktor::Execution {

class DownloadExecutor final : public TaskExecutor {
public:
  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "download"; }
};

std::unique_ptr<TaskExecutor> createDownloadExecutor();

}  // namespace Praktor::Execution
