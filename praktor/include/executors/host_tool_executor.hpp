#pragma once

#include "dag/task_executor.hpp"

#include <memory>

namespace Praktor::Execution {

class HostToolTaskExecutor final : public TaskExecutor {
public:
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "host_tool"; }
};

std::unique_ptr<TaskExecutor> createHostToolTaskExecutor();

} // namespace Praktor::Execution
