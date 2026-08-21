#include "executors/service_executor.hpp"

#include "praktor/shell/process_executor.hpp"
#include "util/variable_substitution.hpp"

#include <utility>

namespace Praktor::Execution {
namespace {

class ProcessExecutorRunner final : public Praktor::System::IProcessRunner {
public:
  Praktor::Shell::ShellResult run(
      const Praktor::Shell::ProcessSpec& spec) override {
    return Praktor::Shell::ProcessExecutor::execute(spec);
  }
};

ServiceParams substituteServiceParams(const ServiceParams& params,
                                      const WorkflowContext& context) {
  ServiceParams substituted = params;
  substituted.name = substituteVariables(params.name, context);
  substituted.profile = substituteVariables(params.profile, context);
  for (auto& argument : substituted.arguments) {
    argument = substituteVariables(argument, context);
  }
  return substituted;
}

}  // namespace

struct ServiceExecutor::Impl {
  Impl()
      : owned_runner(std::make_unique<ProcessExecutorRunner>()),
        owned_profiles(
            std::make_unique<Praktor::System::ServiceProfileRegistry>()),
        controller(std::make_unique<Praktor::System::ServiceController>(
            *owned_runner, *owned_profiles)) {}

  Impl(Praktor::System::IProcessRunner& process_runner,
       const Praktor::System::ServiceProfileRegistry& profiles)
      : controller(std::make_unique<Praktor::System::ServiceController>(
            process_runner, profiles)) {}

  std::unique_ptr<Praktor::System::IProcessRunner> owned_runner;
  std::unique_ptr<Praktor::System::ServiceProfileRegistry> owned_profiles;
  std::unique_ptr<Praktor::System::ServiceController> controller;
};

ServiceExecutor::ServiceExecutor() : impl_(std::make_unique<Impl>()) {}

ServiceExecutor::ServiceExecutor(
    Praktor::System::IProcessRunner& process_runner,
    const Praktor::System::ServiceProfileRegistry& profiles)
    : impl_(std::make_unique<Impl>(process_runner, profiles)) {}

ServiceExecutor::~ServiceExecutor() = default;
ServiceExecutor::ServiceExecutor(ServiceExecutor&&) noexcept = default;
ServiceExecutor& ServiceExecutor::operator=(ServiceExecutor&&) noexcept = default;

TaskResult ServiceExecutor::execute(const Task& task, WorkflowContext& context) {
  const ServiceParams params = substituteServiceParams(
      std::get<ServiceParams>(task.specifics), context);
  Praktor::System::ServiceExecutionResult result =
      impl_->controller->execute(params);

  context.setCurrentTaskOutput("name", params.name);
  context.setCurrentTaskOutput(
      "operation",
      std::string(Praktor::System::systemOperationName(params.operation)));
  context.setCurrentTaskOutput("state", result.state);
  context.setCurrentTaskOutput("changed", result.changed);
  context.setCurrentTaskOutput("duration_ms", result.duration_ms);
  return std::move(result.task_result);
}

std::unique_ptr<TaskExecutor> createServiceExecutor() {
  return std::make_unique<ServiceExecutor>();
}

}  // namespace Praktor::Execution
