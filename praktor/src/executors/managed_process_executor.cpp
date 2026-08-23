#include "executors/managed_process_executor.hpp"

#include "util/logging.hpp"
#include "util/variable_substitution.hpp"

#include <string_view>
#include <utility>

namespace Praktor::Execution {
namespace {

ManagedProcessParams substituteManagedProcessParams(
    const ManagedProcessParams& params,
    const WorkflowContext& context) {
  ManagedProcessParams substituted = params;
  substituted.executable = substituteVariables(params.executable, context);
  substituted.working_directory =
      substituteVariables(params.working_directory, context);
  substituted.identity.image_name =
      substituteVariables(params.identity.image_name, context);
  for (auto& argument : substituted.arguments) {
    argument = substituteVariables(argument, context);
  }
  return substituted;
}

std::string_view operationNameOrUnknown(SystemOperation operation) {
  switch (operation) {
    case SystemOperation::Status: return "status";
    case SystemOperation::Start: return "start";
    case SystemOperation::Stop: return "stop";
    case SystemOperation::Restart: return "restart";
  }
  return "unknown";
}

TaskErrorCode taskErrorCode(
    const Praktor::System::ManagedProcessExecutionResult& result) {
  switch (result.error) {
    case Praktor::System::ManagedProcessError::None:
      return TaskErrorCode::None;
    case Praktor::System::ManagedProcessError::InvalidParameters:
      return TaskErrorCode::SchemaInvalid;
    case Praktor::System::ManagedProcessError::Timeout:
      return TaskErrorCode::Timeout;
    case Praktor::System::ManagedProcessError::UnsupportedPlatform:
      return TaskErrorCode::UnsupportedPlatform;
    case Praktor::System::ManagedProcessError::BackendFailure:
      return result.phase == "start" ? TaskErrorCode::ProcessSpawnFailed
                                     : TaskErrorCode::ServiceStateFailed;
  }
  return TaskErrorCode::ServiceStateFailed;
}

WorkflowValue failureDetails(
    const ManagedProcessParams& params,
    const Praktor::System::ManagedProcessExecutionResult& result) {
  WorkflowValue details = WorkflowValue::object();
  details["image_name"] = params.identity.image_name;
  details["operation"] = std::string(operationNameOrUnknown(params.operation));
  details["state"] =
      std::string(Praktor::System::managedProcessStateName(result.snapshot.state));
  details["pid"] = static_cast<std::int64_t>(result.snapshot.pid);
  return details;
}

}  // namespace

struct ManagedProcessExecutor::Impl {
  Impl()
      : owned_backend(Praktor::System::createManagedProcessBackend()),
        controller(std::make_unique<Praktor::System::ManagedProcessController>(
            *owned_backend)) {}

  Impl(Praktor::System::IManagedProcessBackend& backend,
       std::chrono::milliseconds poll_interval)
      : controller(std::make_unique<Praktor::System::ManagedProcessController>(
            backend, poll_interval)) {}

  std::unique_ptr<Praktor::System::IManagedProcessBackend> owned_backend;
  std::unique_ptr<Praktor::System::ManagedProcessController> controller;
};

ManagedProcessExecutor::ManagedProcessExecutor()
    : impl_(std::make_unique<Impl>()) {}

ManagedProcessExecutor::ManagedProcessExecutor(
    Praktor::System::IManagedProcessBackend& backend,
    std::chrono::milliseconds poll_interval)
    : impl_(std::make_unique<Impl>(backend, poll_interval)) {}

ManagedProcessExecutor::~ManagedProcessExecutor() = default;
ManagedProcessExecutor::ManagedProcessExecutor(ManagedProcessExecutor&&) noexcept = default;
ManagedProcessExecutor& ManagedProcessExecutor::operator=(
    ManagedProcessExecutor&&) noexcept = default;

TaskResult ManagedProcessExecutor::execute(const Task& task,
                                           WorkflowContext& context) {
  const ManagedProcessParams params = substituteManagedProcessParams(
      std::get<ManagedProcessParams>(task.specifics), context);
  Praktor::System::ManagedProcessExecutionResult result =
      impl_->controller->execute(params);

  context.setCurrentTaskOutput("image_name", params.identity.image_name);
  context.setCurrentTaskOutput(
      "operation", std::string(operationNameOrUnknown(params.operation)));
  context.setCurrentTaskOutput(
      "state",
      std::string(Praktor::System::managedProcessStateName(result.snapshot.state)));
  context.setCurrentTaskOutput("pid", static_cast<std::int64_t>(result.snapshot.pid));
  context.setCurrentTaskOutput("changed", result.changed);
  context.setCurrentTaskOutput("duration_ms", result.duration_ms);

  if (result.ok) {
    return TaskResult(true);
  }

  if (result.native_error != 0) {
    TLOG_ERRORF(
        "Managed process operation failed: operation={}, image_name={}, phase={}, "
        "native_error={}, reason={}. Verify the executable identity and process permissions.",
        operationNameOrUnknown(params.operation), params.identity.image_name,
        result.phase, result.native_error, result.message);
  }
  return TaskResult::fail(taskErrorCode(result), std::move(result.phase),
                          std::move(result.message),
                          failureDetails(params, result));
}

std::unique_ptr<TaskExecutor> createManagedProcessExecutor() {
  return std::make_unique<ManagedProcessExecutor>();
}

}  // namespace Praktor::Execution
