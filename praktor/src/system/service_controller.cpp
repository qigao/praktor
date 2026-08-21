#include "system/service_controller.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Praktor::System {
namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;
using Praktor::Execution::TaskErrorCode;
using Praktor::Shell::ProcessSpec;
using Praktor::Shell::ProcessState;
using Praktor::Shell::ShellResult;

enum class ServiceState {
  Stopped = 1,
  StartPending = 2,
  StopPending = 3,
  Running = 4,
};

struct ProcessAttempt {
  std::optional<ShellResult> result;
  std::optional<TaskResult> failure;
};

struct QueryAttempt {
  std::optional<ServiceState> state;
  std::optional<TaskResult> failure;
};

std::string_view serviceStateName(ServiceState state) {
  switch (state) {
    case ServiceState::Stopped: return "stopped";
    case ServiceState::StartPending: return "start_pending";
    case ServiceState::StopPending: return "stop_pending";
    case ServiceState::Running: return "running";
  }
  throw std::invalid_argument("Unknown service state");
}

WorkflowValue failureDetails(const ServiceParams& params) {
  WorkflowValue details = WorkflowValue::object();
  details["name"] = params.name;
  details["profile"] = params.profile;
  switch (params.operation) {
    case SystemOperation::Status:
    case SystemOperation::Start:
    case SystemOperation::Stop:
    case SystemOperation::Restart:
      details["operation"] = std::string(systemOperationName(params.operation));
      break;
    default:
      details["operation"] = "unknown";
      break;
  }
  return details;
}

TaskResult fail(TaskErrorCode code,
                std::string phase,
                std::string message,
                const ServiceParams& params,
                std::optional<ServiceState> state = std::nullopt) {
  WorkflowValue details = failureDetails(params);
  if (state) {
    details["state"] = std::string(serviceStateName(*state));
  }
  return TaskResult::fail(code, std::move(phase), std::move(message),
                          std::move(details));
}

StrList renderArgs(const StrList& profile_args, const ServiceParams& params) {
  constexpr std::string_view arguments_token = "{arguments}";
  constexpr std::string_view service_name_token = "{service_name}";
  StrList rendered;
  for (const auto& token : profile_args) {
    if (token == arguments_token) {
      rendered.insert(rendered.end(), params.arguments.begin(), params.arguments.end());
      continue;
    }

    std::string entry = token;
    std::size_t position = 0;
    while ((position = entry.find(service_name_token, position)) != std::string::npos) {
      entry.replace(position, service_name_token.size(), params.name);
      position += params.name.size();
    }
    rendered.push_back(std::move(entry));
  }
  return rendered;
}

int remainingTimeoutMs(Clock::time_point deadline) {
  const auto remaining =
      std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()).count();
  return static_cast<int>(std::max<std::int64_t>(1, remaining));
}

ProcessAttempt runProcess(IProcessRunner& runner,
                          const ServiceCommandProfile& profile,
                          const StrList& profile_args,
                          const ServiceParams& params,
                          Clock::time_point deadline,
                          std::string phase) {
  if (Clock::now() >= deadline) {
    return {std::nullopt,
            fail(TaskErrorCode::Timeout, std::move(phase),
                 "service operation exceeded its deadline", params)};
  }

  ProcessSpec spec;
  spec.program = profile.program;
  spec.args = renderArgs(profile_args, params);
  spec.timeout_ms = remainingTimeoutMs(deadline);
  spec.stream_output = false;

  ShellResult result;
  try {
    result = runner.run(spec);
  } catch (const std::exception& error) {
    return {std::nullopt,
            fail(TaskErrorCode::ProcessSpawnFailed, std::move(phase),
                 std::string("failed to run service command: ") + error.what(),
                 params)};
  } catch (...) {
    return {std::nullopt,
            fail(TaskErrorCode::ProcessSpawnFailed, std::move(phase),
                 "failed to run service command", params)};
  }

  if (result.state == ProcessState::SpawnFailed) {
    return {std::nullopt,
            fail(TaskErrorCode::ProcessSpawnFailed, std::move(phase),
                 "failed to spawn service command", params)};
  }
  if (result.state == ProcessState::TimedOut) {
    return {std::nullopt,
            fail(TaskErrorCode::Timeout, std::move(phase),
                 "service command timed out", params)};
  }
  if (result.state == ProcessState::Cancelled) {
    return {std::nullopt,
            fail(TaskErrorCode::Cancelled, std::move(phase),
                 "service command was cancelled", params)};
  }
  if (!result.success()) {
    WorkflowValue details = failureDetails(params);
    details["process_state"] =
        std::string(Praktor::Shell::processStateName(result.state));
    details["exit_code"] = static_cast<std::int64_t>(result.exit_code);
    return {std::nullopt,
            TaskResult::fail(TaskErrorCode::ServiceStateFailed,
                             std::move(phase),
                             "service command failed",
                             std::move(details))};
  }
  return {std::move(result), std::nullopt};
}

std::optional<ServiceState> parseServiceState(std::string_view output) {
  constexpr std::string_view state_label = "STATE";
  std::size_t line_start = 0;
  while (line_start <= output.size()) {
    const std::size_t line_end = output.find('\n', line_start);
    std::string_view line = output.substr(
        line_start, line_end == std::string_view::npos ? output.size() - line_start
                                                       : line_end - line_start);
    const std::size_t first = line.find_first_not_of(" \t\r");
    if (first != std::string_view::npos) {
      line.remove_prefix(first);
    }
    if (line.starts_with(state_label) &&
        (line.size() == state_label.size() ||
         line[state_label.size()] == ':' ||
         line[state_label.size()] == ' ' ||
         line[state_label.size()] == '\t')) {
      const std::size_t colon = line.find(':', state_label.size());
      if (colon == std::string_view::npos) {
        return std::nullopt;
      }
      line.remove_prefix(colon + 1);
      const std::size_t numeric_start = line.find_first_not_of(" \t");
      if (numeric_start == std::string_view::npos) {
        return std::nullopt;
      }
      line.remove_prefix(numeric_start);

      int numeric_state = 0;
      const char* begin = line.data();
      const char* end = begin + line.size();
      const auto parsed = std::from_chars(begin, end, numeric_state);
      if (parsed.ec != std::errc{} || parsed.ptr == begin) {
        return std::nullopt;
      }
      if (parsed.ptr != end &&
          std::isspace(static_cast<unsigned char>(*parsed.ptr)) == 0) {
        return std::nullopt;
      }
      switch (numeric_state) {
        case 1: return ServiceState::Stopped;
        case 2: return ServiceState::StartPending;
        case 3: return ServiceState::StopPending;
        case 4: return ServiceState::Running;
        default: return std::nullopt;
      }
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 1;
  }
  return std::nullopt;
}

QueryAttempt queryState(IProcessRunner& runner,
                        const ServiceCommandProfile& profile,
                        const ServiceParams& params,
                        Clock::time_point deadline,
                        std::string phase) {
  ProcessAttempt attempt = runProcess(runner, profile, profile.status_args,
                                      params, deadline, phase);
  if (attempt.failure) {
    return {std::nullopt, std::move(attempt.failure)};
  }

  const auto state = parseServiceState(attempt.result->stdout_output);
  if (!state) {
    return {std::nullopt,
            fail(TaskErrorCode::ServiceStateFailed, std::move(phase),
                 "service status did not contain a supported numeric STATE field",
                 params)};
  }
  return {state, std::nullopt};
}

}  // namespace

ServiceProfileRegistry::ServiceProfileRegistry() {
  ServiceCommandProfile windows_scm;
  windows_scm.name = "windows_scm";
  windows_scm.program = "sc.exe";
  registerProfile(std::move(windows_scm));
}

void ServiceProfileRegistry::registerProfile(ServiceCommandProfile profile) {
  if (profile.name.empty()) {
    throw std::invalid_argument("service profile name cannot be empty");
  }
  if (profile.program.empty()) {
    throw std::invalid_argument("service profile program cannot be empty");
  }
  const std::string name = profile.name;
  if (!profiles_.emplace(name, std::move(profile)).second) {
    throw std::invalid_argument("duplicate service profile: " + name);
  }
}

const ServiceCommandProfile* ServiceProfileRegistry::findProfile(
    std::string_view name) const noexcept {
  const auto found = profiles_.find(name);
  return found == profiles_.end() ? nullptr : &found->second;
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

ServiceController::ServiceController(IProcessRunner& process_runner,
                                     const ServiceProfileRegistry& profiles) noexcept
    : process_runner_(process_runner), profiles_(profiles) {}

ServiceExecutionResult ServiceController::execute(const ServiceParams& params) {
  const auto started_at = Clock::now();
  ServiceExecutionResult execution;
  auto finish = [&](TaskResult task_result) {
    execution.task_result = std::move(task_result);
    execution.duration_ms = std::chrono::duration_cast<Milliseconds>(
                                Clock::now() - started_at)
                                .count();
    return execution;
  };

  const bool supported_operation =
      params.operation == SystemOperation::Status ||
      params.operation == SystemOperation::Start ||
      params.operation == SystemOperation::Stop ||
      params.operation == SystemOperation::Restart;
  if (!supported_operation || params.name.empty() || params.profile.empty() ||
      params.timeout_ms <= 0 ||
      params.poll_interval_ms <= 0) {
    return finish(fail(TaskErrorCode::SchemaInvalid, "validate",
                       "service parameters are invalid", params));
  }

  const ServiceCommandProfile* profile = profiles_.findProfile(params.profile);
  if (profile == nullptr) {
    return finish(fail(TaskErrorCode::ServiceStateFailed, "profile",
                       "unknown service command profile: " + params.profile,
                       params));
  }

  const auto deadline = started_at + Milliseconds(params.timeout_ms);
  QueryAttempt initial =
      queryState(process_runner_, *profile, params, deadline, "query");
  if (initial.failure) {
    return finish(std::move(*initial.failure));
  }
  ServiceState current = *initial.state;
  execution.state = std::string(serviceStateName(current));

  auto pollFor = [&](ServiceState target) -> std::optional<TaskResult> {
    for (;;) {
      const auto now = Clock::now();
      if (now >= deadline) {
        return fail(TaskErrorCode::Timeout, "poll",
                    "service did not reach " +
                        std::string(serviceStateName(target)) +
                        " before the deadline",
                    params, current);
      }
      const auto remaining =
          std::chrono::duration_cast<Milliseconds>(deadline - now);
      std::this_thread::sleep_for(
          std::min(Milliseconds(params.poll_interval_ms), remaining));

      QueryAttempt query =
          queryState(process_runner_, *profile, params, deadline, "poll");
      if (query.failure) {
        return std::move(query.failure);
      }
      current = *query.state;
      execution.state = std::string(serviceStateName(current));
      if (current == target) {
        return std::nullopt;
      }
    }
  };

  auto mutate = [&](const StrList& command, std::string phase)
      -> std::optional<TaskResult> {
    ProcessAttempt attempt = runProcess(process_runner_, *profile, command,
                                        params, deadline, std::move(phase));
    if (attempt.failure) {
      return std::move(attempt.failure);
    }
    execution.changed = true;
    return std::nullopt;
  };

  auto start = [&]() -> std::optional<TaskResult> {
    if (current == ServiceState::Running) {
      return std::nullopt;
    }
    if (current == ServiceState::StopPending) {
      return fail(TaskErrorCode::ServiceStateFailed, "transition",
                  "cannot start a service while it is stopping", params,
                  current);
    }
    if (current == ServiceState::Stopped) {
      if (auto failure = mutate(profile->start_args, "start")) {
        return failure;
      }
    }
    return pollFor(ServiceState::Running);
  };

  auto stop = [&]() -> std::optional<TaskResult> {
    if (current == ServiceState::Stopped) {
      return std::nullopt;
    }
    if (current == ServiceState::StartPending) {
      return fail(TaskErrorCode::ServiceStateFailed, "transition",
                  "cannot stop a service while it is starting", params,
                  current);
    }
    if (current == ServiceState::Running) {
      if (auto failure = mutate(profile->stop_args, "stop")) {
        return failure;
      }
    }
    return pollFor(ServiceState::Stopped);
  };

  std::optional<TaskResult> failure;
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
      if (current == ServiceState::StartPending) {
        failure = pollFor(ServiceState::Running);
      }
      if (!failure) {
        failure = stop();
      }
      if (!failure) {
        failure = start();
      }
      break;
    default:
      failure = fail(TaskErrorCode::SchemaInvalid, "validate",
                     "unsupported service operation", params, current);
      break;
  }

  if (failure) {
    return finish(std::move(*failure));
  }
  return finish(TaskResult(true));
}

}  // namespace Praktor::System
