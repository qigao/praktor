#include "system/managed_process.hpp"
#include "praktor/process_control.h"
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Praktor::System {
namespace {
static_assert(static_cast<int>(SystemOperation::Status) == PRAKTOR_PROCESS_STATUS);
static_assert(static_cast<int>(SystemOperation::Start) == PRAKTOR_PROCESS_START);
static_assert(static_cast<int>(SystemOperation::Stop) == PRAKTOR_PROCESS_STOP);
static_assert(static_cast<int>(SystemOperation::Restart) == PRAKTOR_PROCESS_RESTART);
static_assert(static_cast<int>(ManagedProcessError::None) == PRAKTOR_PROCESS_CONTROL_NONE);
static_assert(static_cast<int>(ManagedProcessError::InvalidParameters) == PRAKTOR_PROCESS_CONTROL_INVALID_PARAMETERS);
static_assert(static_cast<int>(ManagedProcessError::BackendFailure) == PRAKTOR_PROCESS_CONTROL_BACKEND_FAILURE);
static_assert(static_cast<int>(ManagedProcessError::Timeout) == PRAKTOR_PROCESS_CONTROL_TIMEOUT);
static_assert(static_cast<int>(ManagedProcessError::UnsupportedPlatform) == PRAKTOR_PROCESS_CONTROL_UNSUPPORTED_PLATFORM);
praktor_process_snapshot toC(const ManagedProcessSnapshot& s) {
  return {s.state == ManagedProcessState::Running,s.pid,s.session_id,s.instance_token,
          s.canonical_image_name.c_str()};
}
ManagedProcessSnapshot fromC(const praktor_process_snapshot& s) {
  return {s.running ? ManagedProcessState::Running : ManagedProcessState::NotRunning,
          s.pid,s.session_id,s.instance_token,s.canonical_image_name ? s.canonical_image_name : ""};
}
/* Only value conversion and exception containment. Query and start snapshots
 * occupy separate stable slots, so a new query cannot overwrite the identity
 * needed for startup compensation. No clock, polling or control state here. */
struct BackendBridge {
  IManagedProcessBackend& backend;
  const ManagedProcessParams& params;
  ManagedProcessResult<ManagedProcessSnapshot> queried, started;
  ManagedProcessCommandResult command;
  std::string failure_message;
  std::exception_ptr exception;
  BackendBridge(IManagedProcessBackend& b, const ManagedProcessParams& p) : backend(b), params(p) {}
  template<class Call> praktor_process_backend_result guard(Call call) noexcept {
    try { return call(); }
    catch (...) { exception = std::current_exception(); return {false,0,"C++ process backend exception"}; }
  }
  static praktor_process_backend_result query(void* user, const char*, praktor_process_snapshot* out) noexcept {
    auto& b = *static_cast<BackendBridge*>(user);
    return b.guard([&]() -> praktor_process_backend_result {
      auto result = b.backend.query(b.params.identity);
      if (!result.ok) {
        b.failure_message = std::move(result.message);
        return {false,result.native_error,b.failure_message.c_str()};
      }
      b.queried = std::move(result); *out = toC(b.queried.value);
      return {true,0,""};
    });
  }
  static praktor_process_backend_result start(void* user, const praktor_process_control_request*,
                                              praktor_process_snapshot* out) noexcept {
    auto& b = *static_cast<BackendBridge*>(user);
    return b.guard([&]() -> praktor_process_backend_result {
      auto result = b.backend.start(b.params);
      if (!result.ok) {
        b.failure_message = std::move(result.message);
        return {false,result.native_error,b.failure_message.c_str()};
      }
      b.started = std::move(result); *out = toC(b.started.value);
      return {true,0,""};
    });
  }
  static praktor_process_backend_result stop(void* user, const praktor_process_snapshot* snapshot) noexcept {
    auto& b = *static_cast<BackendBridge*>(user);
    return b.guard([&]() -> praktor_process_backend_result {
      b.command = b.backend.requestStop(fromC(*snapshot));
      return {b.command.ok,b.command.native_error,b.command.message.c_str()};
    });
  }
  static praktor_process_backend_result terminate(void* user, const praktor_process_snapshot* snapshot) noexcept {
    auto& b = *static_cast<BackendBridge*>(user);
    return b.guard([&]() -> praktor_process_backend_result {
      b.command = b.backend.terminate(fromC(*snapshot));
      return {b.command.ok,b.command.native_error,b.command.message.c_str()};
    });
  }
};
} // namespace
std::string_view managedProcessStateName(ManagedProcessState state) {
  switch (state) {
    case ManagedProcessState::NotRunning: return "not_running";
    case ManagedProcessState::Running: return "running";
  }
  throw std::invalid_argument("Unknown managed process state");
}
std::string_view managedProcessOperationName(SystemOperation operation) {
  switch (operation) {
    case SystemOperation::Status: return "status";
    case SystemOperation::Start: return "start";
    case SystemOperation::Stop: return "stop";
    case SystemOperation::Restart: return "restart";
  }
  throw std::invalid_argument("Unknown managed process operation");
}
ManagedProcessController::ManagedProcessController(IManagedProcessBackend& backend,
                                                   std::chrono::milliseconds poll_interval)
    : backend_(backend), poll_interval_(poll_interval) {
  if (poll_interval_.count() <= 0)
    throw std::invalid_argument("managed process poll interval must be positive");
}
ManagedProcessExecutionResult ManagedProcessController::execute(const ManagedProcessParams& params) {
  BackendBridge bridge(backend_,params);
  std::vector<const char*> args;
  for (const auto& arg : params.arguments) args.push_back(arg.c_str());
  const praktor_process_control_request request = {
      static_cast<praktor_process_operation>(params.operation),params.identity.image_name.c_str(),
      params.executable.c_str(),args.data(),args.size(),params.working_directory.c_str(),
      params.startup_timeout_ms,params.stop_timeout_ms,params.force_terminate};
  const praktor_process_control_host host = {
      &bridge,BackendBridge::query,BackendBridge::start,BackendBridge::stop,BackendBridge::terminate,
      nullptr,nullptr,static_cast<uint64_t>(poll_interval_.count())};
  praktor_process_control_result result{};
  if (praktor_process_control_execute(&request,&host,&result) != SALTS_OK)
    throw std::invalid_argument("invalid C process control host");
  if (bridge.exception) std::rethrow_exception(bridge.exception);
  return {result.ok,fromC(result.snapshot),result.changed,static_cast<ManagedProcessError>(result.error),
          result.native_error,result.phase,result.message,static_cast<int64_t>(result.duration_ms)};
}
} // namespace Praktor::System
