#include "system/managed_process.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Praktor::System {
namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;

ManagedProcessError backendError(std::string_view message) {
  return message == kManagedProcessUnsupportedPlatform
             ? ManagedProcessError::UnsupportedPlatform
             : ManagedProcessError::BackendFailure;
}

bool isSupportedOperation(SystemOperation operation) {
  return operation == SystemOperation::Status ||
         operation == SystemOperation::Start ||
         operation == SystemOperation::Stop ||
         operation == SystemOperation::Restart;
}

}  // namespace

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

ManagedProcessController::ManagedProcessController(
    IManagedProcessBackend& backend,
    std::chrono::milliseconds poll_interval)
    : backend_(backend), poll_interval_(poll_interval) {
  if (poll_interval_ <= Milliseconds::zero()) {
    throw std::invalid_argument("managed process poll interval must be positive");
  }
}

ManagedProcessExecutionResult ManagedProcessController::execute(
    const ManagedProcessParams& params) {
  const auto started_at = Clock::now();
  ManagedProcessExecutionResult execution;

  auto finish = [&](bool ok) {
    execution.ok = ok;
    execution.duration_ms =
        std::chrono::duration_cast<Milliseconds>(Clock::now() - started_at)
            .count();
    return execution;
  };

  auto fail = [&](ManagedProcessError error,
                  std::string phase,
                  std::string message,
                  int native_error = 0) {
    execution.error = error;
    execution.native_error = native_error;
    execution.phase = std::move(phase);
    execution.message = std::move(message);
    return finish(false);
  };

  if (!isSupportedOperation(params.operation) ||
      params.identity.image_name.empty() || params.startup_timeout_ms <= 0 ||
      params.stop_timeout_ms <= 0 ||
      ((params.operation == SystemOperation::Start ||
        params.operation == SystemOperation::Restart) &&
       params.executable.empty())) {
    return fail(ManagedProcessError::InvalidParameters, "validate",
                "managed process parameters are invalid");
  }

  auto query = [&](std::string_view phase) -> std::optional<ManagedProcessExecutionResult> {
    auto result = backend_.query(params.identity);
    if (!result.ok) {
      const ManagedProcessError error = backendError(result.message);
      return fail(error, std::string(phase),
                  std::move(result.message), result.native_error);
    }
    execution.snapshot = result.value;
    return std::nullopt;
  };

  if (auto failure = query("query")) {
    return *failure;
  }

  auto pollFor = [&](ManagedProcessState target,
                     Clock::time_point deadline,
                     std::string phase)
      -> std::optional<ManagedProcessExecutionResult> {
    for (;;) {
      if (Clock::now() >= deadline) {
        return fail(ManagedProcessError::Timeout, std::move(phase),
                    "managed process did not reach " +
                        std::string(managedProcessStateName(target)) +
                        " before the deadline");
      }
      if (auto failure = query(phase)) {
        return failure;
      }
      if (Clock::now() >= deadline) {
        return fail(ManagedProcessError::Timeout, std::move(phase),
                    "managed process did not reach " +
                        std::string(managedProcessStateName(target)) +
                        " before the deadline");
      }
      if (execution.snapshot.state == target) {
        return std::nullopt;
      }
      const auto now = Clock::now();
      std::this_thread::sleep_for(std::min(
          poll_interval_,
          std::chrono::duration_cast<Milliseconds>(deadline - now)));
    }
  };

  auto start = [&]() -> std::optional<ManagedProcessExecutionResult> {
    if (execution.snapshot.state == ManagedProcessState::Running) {
      return std::nullopt;
    }
    const auto startup_deadline =
        Clock::now() + Milliseconds(params.startup_timeout_ms);
    auto result = backend_.start(params);
    if (!result.ok) {
      const ManagedProcessError error = backendError(result.message);
      return fail(error, "start",
                  std::move(result.message), result.native_error);
    }
    const ManagedProcessSnapshot started = result.value;
    execution.snapshot = started;
    execution.changed = true;

    for (;;) {
      if (auto failure = query("start_poll")) {
        return failure;
      }
      const auto now = Clock::now();
      if (execution.snapshot.state == ManagedProcessState::Running &&
          now < startup_deadline) {
        return std::nullopt;
      }
      if (now >= startup_deadline) {
        break;
      }
      std::this_thread::sleep_for(std::min(
          poll_interval_,
          std::chrono::duration_cast<Milliseconds>(startup_deadline - now)));
    }

    if (auto failure = query("start_timeout_query")) {
      execution.snapshot = started;
      return failure;
    }
    if (execution.snapshot.state == ManagedProcessState::NotRunning) {
      return fail(ManagedProcessError::Timeout, "start_poll",
                  "managed process startup exceeded the deadline");
    }

    auto terminated = backend_.terminate(started);
    if (!terminated.ok) {
      const ManagedProcessError error = backendError(terminated.message);
      return fail(error, "start_timeout_terminate",
                  std::move(terminated.message), terminated.native_error);
    }

    if (auto cleanup_failure = pollFor(
            ManagedProcessState::NotRunning,
            Clock::now() + Milliseconds(params.stop_timeout_ms),
            "start_timeout_cleanup_poll")) {
      return cleanup_failure;
    }
    return fail(ManagedProcessError::Timeout, "start_poll",
                "managed process startup exceeded the deadline");
  };

  auto stop = [&]() -> std::optional<ManagedProcessExecutionResult> {
    if (execution.snapshot.state == ManagedProcessState::NotRunning) {
      return std::nullopt;
    }

    auto stop_result = backend_.requestStop(execution.snapshot);
    if (!stop_result.ok) {
      const ManagedProcessError error = backendError(stop_result.message);
      return fail(error, "request_stop",
                  std::move(stop_result.message), stop_result.native_error);
    }
    execution.changed = true;

    const auto graceful_deadline =
        Clock::now() + Milliseconds(params.stop_timeout_ms);
    if (auto failure = pollFor(ManagedProcessState::NotRunning,
                               graceful_deadline, "stop_poll")) {
      if (failure->error != ManagedProcessError::Timeout ||
          !params.force_terminate) {
        return failure;
      }
      execution.error = ManagedProcessError::None;
      execution.native_error = 0;
      execution.phase.clear();
      execution.message.clear();
    } else {
      return std::nullopt;
    }

    if (auto failure = query("force_query")) {
      return failure;
    }
    if (execution.snapshot.state == ManagedProcessState::NotRunning) {
      return std::nullopt;
    }

    const ManagedProcessSnapshot confirmed = execution.snapshot;
    auto terminate_result = backend_.terminate(confirmed);
    if (!terminate_result.ok) {
      const ManagedProcessError error = backendError(terminate_result.message);
      return fail(error, "terminate",
                  std::move(terminate_result.message),
                  terminate_result.native_error);
    }
    return pollFor(ManagedProcessState::NotRunning,
                   Clock::now() + Milliseconds(params.stop_timeout_ms),
                   "terminate_poll");
  };

  std::optional<ManagedProcessExecutionResult> failure;
  switch (params.operation) {
    case SystemOperation::Status:
      break;
    case SystemOperation::Start:
      failure = start();
      break;
    case SystemOperation::Stop:
      failure = stop();
      break;
    case SystemOperation::Restart:
      failure = stop();
      if (!failure) {
        failure = query("restart_query");
      }
      if (!failure) {
        failure = start();
      }
      break;
    default:
      return fail(ManagedProcessError::InvalidParameters, "validate",
                  "unsupported managed process operation");
  }

  if (failure) {
    return *failure;
  }
  return finish(true);
}

}  // namespace Praktor::System
