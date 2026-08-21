#pragma once

#include "yml/task_types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Praktor::System {

enum class ManagedProcessState { NotRunning, Running };

struct ManagedProcessSnapshot {
  ManagedProcessState state{ManagedProcessState::NotRunning};
  std::uint32_t pid{0};
};

template <typename T>
struct ManagedProcessResult {
  bool ok{false};
  T value{};
  int native_error{0};
  std::string message;
};

struct ManagedProcessCommandResult {
  bool ok{false};
  int native_error{0};
  std::string message;
};

class IManagedProcessBackend {
public:
  virtual ~IManagedProcessBackend() = default;
  virtual ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity& identity) = 0;
  virtual ManagedProcessResult<std::uint32_t> start(
      const ManagedProcessParams& params) = 0;
  virtual ManagedProcessCommandResult requestStop(
      const ManagedProcessSnapshot& snapshot) = 0;
  virtual ManagedProcessCommandResult terminate(
      const ManagedProcessSnapshot& snapshot) = 0;
};

enum class ManagedProcessError {
  None,
  InvalidParameters,
  BackendFailure,
  Timeout,
  UnsupportedPlatform,
};

struct ManagedProcessExecutionResult {
  bool ok{false};
  ManagedProcessSnapshot snapshot{};
  bool changed{false};
  ManagedProcessError error{ManagedProcessError::None};
  int native_error{0};
  std::string phase;
  std::string message;
  std::int64_t duration_ms{0};
};

inline constexpr std::string_view kManagedProcessUnsupportedPlatform =
    "unsupported_platform";

std::string_view managedProcessStateName(ManagedProcessState state);
std::string_view managedProcessOperationName(SystemOperation operation);

class ManagedProcessController {
public:
  explicit ManagedProcessController(
      IManagedProcessBackend& backend,
      std::chrono::milliseconds poll_interval = std::chrono::milliseconds(50));

  ManagedProcessExecutionResult execute(const ManagedProcessParams& params);

private:
  IManagedProcessBackend& backend_;
  std::chrono::milliseconds poll_interval_;
};

std::unique_ptr<IManagedProcessBackend> createManagedProcessBackend();

}  // namespace Praktor::System
