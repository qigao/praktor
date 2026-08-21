#pragma once

#include "dag/task_executor.hpp"
#include "system/service_controller.hpp"

#include <memory>
#include <string>

namespace Praktor::Execution {

class ServiceExecutor final : public TaskExecutor {
public:
  ServiceExecutor();
  ServiceExecutor(Praktor::System::IProcessRunner& process_runner,
                  Praktor::System::ServiceProfileRegistry profiles);
  ~ServiceExecutor() override;

  ServiceExecutor(const ServiceExecutor&) = delete;
  ServiceExecutor& operator=(const ServiceExecutor&) = delete;
  ServiceExecutor(ServiceExecutor&&) noexcept;
  ServiceExecutor& operator=(ServiceExecutor&&) noexcept;

  TaskResult execute(const Task& task, WorkflowContext& context) override;
  std::string getTaskType() const override { return "service"; }

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::unique_ptr<TaskExecutor> createServiceExecutor();

}  // namespace Praktor::Execution
