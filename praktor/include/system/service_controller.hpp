#pragma once

#include "dag/task_executor.hpp"
#include "praktor/shell/process_executor.hpp"
#include "yml/task_types.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Praktor::System {

class IProcessRunner {
public:
  virtual ~IProcessRunner() = default;
  virtual Praktor::Shell::ShellResult run(
      const Praktor::Shell::ProcessSpec& spec) = 0;
};

struct ServiceCommandProfile {
  std::string name;
  std::string program;
  StrList status_args{"query", "{service_name}"};
  StrList start_args{"start", "{service_name}", "{arguments}"};
  StrList stop_args{"stop", "{service_name}", "{arguments}"};
};

class ServiceProfileRegistry {
public:
  ServiceProfileRegistry();

  void registerProfile(ServiceCommandProfile profile);
  const ServiceCommandProfile* findProfile(std::string_view name) const noexcept;

private:
  std::map<std::string, ServiceCommandProfile, std::less<>> profiles_;
};

struct ServiceExecutionResult {
  TaskResult task_result;
  std::string state;
  bool changed = false;
  std::int64_t duration_ms = 0;
};

std::string_view systemOperationName(SystemOperation operation);

class ServiceController {
public:
  ServiceController(IProcessRunner& process_runner,
                    const ServiceProfileRegistry& profiles) noexcept;

  ServiceExecutionResult execute(const ServiceParams& params);

private:
  IProcessRunner& process_runner_;
  const ServiceProfileRegistry& profiles_;
};

}  // namespace Praktor::System
