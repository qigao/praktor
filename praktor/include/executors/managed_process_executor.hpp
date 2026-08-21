#pragma once

#include "dag/task_executor.hpp"
#include "system/managed_process.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace Praktor::Execution {

class ManagedProcessExecutor final : public TaskExecutor {
public:
  ManagedProcessExecutor();
  explicit ManagedProcessExecutor(
      Praktor::System::IManagedProcessBackend& backend,
      std::chrono::milliseconds poll_interval = std::chrono::milliseconds(50));
  ~ManagedProcessExecutor() override;

  ManagedProcessExecutor(const ManagedProcessExecutor&) = delete;
  ManagedProcessExecutor& operator=(const ManagedProcessExecutor&) = delete;
  ManagedProcessExecutor(ManagedProcessExecutor&&) noexcept;
  ManagedProcessExecutor& operator=(ManagedProcessExecutor&&) noexcept;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "managed_process"; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<TaskExecutor> createManagedProcessExecutor();

}  // namespace Praktor::Execution
