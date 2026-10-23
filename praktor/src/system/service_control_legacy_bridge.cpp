#include "system/service_controller.hpp"
#include "praktor/service_control.h"
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Praktor::System {
namespace {
using Praktor::Execution::TaskErrorCode;
static_assert(static_cast<int>(SystemOperation::Restart) == PRAKTOR_PROCESS_RESTART);
static_assert(static_cast<int>(Praktor::Shell::ProcessState::Exited) == PRAKTOR_PROCESS_EXITED);
static_assert(static_cast<int>(Praktor::Shell::ProcessState::OutputLimitExceeded) == PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED);
struct Owner {
  praktor_service_control control{};
  Owner() {
    const praktor_service_config config{4096,1024*1024,16*1024*1024};
    if (praktor_service_control_init(&control,&config) != SALTS_OK)
      throw std::runtime_error("C service controller initialization failed");
  }
  ~Owner() { praktor_service_control_destroy(&control); }
  Owner(const Owner&) = delete;
  Owner& operator=(const Owner&) = delete;
};
struct RunnerBridge {
  IProcessRunner& runner;
  Praktor::Shell::ShellResult result;
  char diagnostic[1024]{};
  static int run(void *user, const praktor_service_command *command, praktor_service_response *out) noexcept {
    auto& b = *static_cast<RunnerBridge *>(user);
    try {
      Praktor::Shell::ProcessSpec spec;
      spec.program = command->program;
      for (size_t i = 0; i < command->arguments.count; ++i) spec.args.emplace_back(command->arguments.values[i]);
      spec.timeout_ms = static_cast<int>(command->timeout_ms);
      spec.max_output_bytes = command->max_output_bytes; spec.stream_output = false;
      b.result = b.runner.run(spec);
      out->state = static_cast<praktor_process_state>(b.result.state);
      out->exit_code = b.result.exit_code;
      out->output = b.result.stdout_output.data(); out->output_size = b.result.stdout_output.size();
      return SALTS_OK;
    } catch (const std::exception& error) {
      std::snprintf(b.diagnostic,sizeof(b.diagnostic),"failed to run service command: %.980s",error.what());
      out->failure_message = b.diagnostic;
    } catch (...) { out->failure_message = "failed to run service command"; }
    return SALTS_EIO;
  }
};
std::vector<const char*> args(const StrList& values) {
  std::vector<const char*> result;
  for (const auto& value : values) result.push_back(value.c_str());
  return result;
}
bool native(const std::string& value) { return value.find('\0') == std::string::npos; }
bool native(const StrList& values) {
  for (const auto& value : values) if (!native(value)) return false;
  return true;
}
TaskErrorCode errorCode(praktor_service_error error) {
  switch (error) {
    case PRAKTOR_SERVICE_INVALID: return TaskErrorCode::SchemaInvalid;
    case PRAKTOR_SERVICE_SPAWN_FAILED: return TaskErrorCode::ProcessSpawnFailed;
    case PRAKTOR_SERVICE_TIMEOUT: return TaskErrorCode::Timeout;
    case PRAKTOR_SERVICE_CANCELLED: return TaskErrorCode::Cancelled;
    default: return TaskErrorCode::ServiceStateFailed;
  }
}
WorkflowValue details(const ServiceParams& params) {
  auto value = WorkflowValue::object(); value["name"] = params.name; value["profile"] = params.profile;
  value["operation"] = static_cast<unsigned>(params.operation) <= static_cast<unsigned>(SystemOperation::Restart)
      ? std::string(systemOperationName(params.operation)) : "unknown";
  return value;
}
}
ServiceProfileRegistry::ServiceProfileRegistry() {
  const auto *native_profile = praktor_service_windows_scm_profile();
  ServiceCommandProfile profile; profile.name = "windows_scm"; profile.program = native_profile->program;
  const auto convert = [](praktor_service_args values) {
    StrList result; for (size_t i = 0; i < values.count; ++i) result.emplace_back(values.values[i]); return result;
  };
  profile.status_args = convert(native_profile->status); profile.start_args = convert(native_profile->start);
  profile.stop_args = convert(native_profile->stop); registerProfile(std::move(profile));
}
void ServiceProfileRegistry::registerProfile(ServiceCommandProfile profile) {
  if (profile.name.empty()) throw std::invalid_argument("service profile name cannot be empty");
  if (profile.program.empty()) throw std::invalid_argument("service profile program cannot be empty");
  const auto name = profile.name;
  if (!profiles_.emplace(name,std::move(profile)).second) throw std::invalid_argument("duplicate service profile: "+name);
}
const ServiceCommandProfile *ServiceProfileRegistry::findProfile(std::string_view name) const noexcept {
  const auto found = profiles_.find(name); return found == profiles_.end() ? nullptr : &found->second;
}
std::string_view systemOperationName(SystemOperation operation) {
  switch (operation) {
    case SystemOperation::Status: return "status";
    case SystemOperation::Start: return "start";
    case SystemOperation::Stop: return "stop";
    case SystemOperation::Restart: return "restart";
  }
  throw std::invalid_argument("Unknown system operation");
}
ServiceController::ServiceController(IProcessRunner& runner, ServiceProfileRegistry profiles)
    : process_runner_(runner), profiles_(std::move(profiles)) {}
ServiceExecutionResult ServiceController::execute(const ServiceParams& params) {
  const auto *profile = profiles_.findProfile(params.profile);
  if (!native(params.name) || !native(params.profile) || !native(params.arguments) ||
      (profile && (!native(profile->program) || !native(profile->status_args) ||
                    !native(profile->start_args) || !native(profile->stop_args)))) {
    return {TaskResult::fail(TaskErrorCode::SchemaInvalid,"validate","service metadata contains NUL",details(params)),"",false,0};
  }
  Owner owner; RunnerBridge runner{process_runner_,{}, {}};
  const auto arguments = args(params.arguments);
  const auto status = profile ? args(profile->status_args) : std::vector<const char*>{};
  const auto start = profile ? args(profile->start_args) : std::vector<const char*>{};
  const auto stop = profile ? args(profile->stop_args) : std::vector<const char*>{};
  const praktor_service_profile cprofile{profile ? profile->program.c_str() : nullptr,
      {status.data(),status.size()},{start.data(),start.size()},{stop.data(),stop.size()}};
  const praktor_service_request request{static_cast<praktor_process_operation>(params.operation),
      params.name.c_str(),params.profile.c_str(),profile ? &cprofile : nullptr,
      {arguments.data(),arguments.size()},params.timeout_ms,params.poll_interval_ms};
  const praktor_service_host host{&runner,RunnerBridge::run,nullptr,nullptr}; praktor_service_result result{};
  if (praktor_service_control_execute(&owner.control,&request,&host,&result) != SALTS_OK)
    throw std::runtime_error("invalid C service control call");
  TaskResult task(result.ok);
  if (!result.ok) {
    auto value = details(params);
    if (result.include_state) value["state"] = praktor_service_state_name(result.state);
    if (result.include_process) {
      value["process_state"] = Praktor::Shell::processStateName(static_cast<Praktor::Shell::ProcessState>(result.process_state));
      value["exit_code"] = static_cast<int64_t>(result.exit_code);
    }
    task = TaskResult::fail(errorCode(result.error),result.phase,result.message,std::move(value));
  }
  return {std::move(task),praktor_service_state_name(result.state),result.changed,static_cast<int64_t>(result.duration_ms)};
}
} // namespace Praktor::System
