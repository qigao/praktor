#pragma once

#include "execution/execution_control.hpp"
#include "praktor/shell/shell_executor.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>

namespace Praktor::Execution {

inline int clampProcessTimeoutMs(
    int configured_timeout_ms,
    const std::shared_ptr<ExecutionControl>& control) {
  if (!control) {
    return configured_timeout_ms;
  }

  const auto remaining = control->remainingDeadline();
  if (!remaining) {
    return configured_timeout_ms;
  }

  if (*remaining <= ExecutionControl::Clock::duration::zero()) {
    return 1;
  }

  auto remaining_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(*remaining).count();
  if (remaining_ms <= 0) {
    remaining_ms = 1;
  }
  remaining_ms = std::min<std::int64_t>(
      remaining_ms, std::numeric_limits<int>::max());
  return std::min(configured_timeout_ms, static_cast<int>(remaining_ms));
}

inline Praktor::Shell::ShellResult waitForManagedProcess(
    Praktor::Shell::ManagedProcess& process,
    const std::shared_ptr<ExecutionControl>& control) {
  if (!control) {
    return process.wait();
  }

  constexpr auto kControlPollInterval = std::chrono::milliseconds(10);
  for (;;) {
    if (control->stopRequested()) {
      (void)process.cancel();
      return process.wait();
    }
    if (process.waitFor(kControlPollInterval)) {
      return process.wait();
    }
  }
}

} // namespace Praktor::Execution
