#pragma once

#include "praktor/shell/process_executor.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>

namespace Praktor::Shell::detail {

struct PlatformProcessResult {
  ShellResult result;
  ProcessState state = ProcessState::WaitFailed;
};

class ProcessControl {
public:
  bool publishRunning(int pid, std::function<void()> native_cancel);
  void releaseNative();
  bool probeCompletion(const std::function<bool()>& probe);
  bool requestCancel();
  bool cancelRequested() const;
  int pid() const;
  ProcessState state() const;
  bool waitFor(std::chrono::milliseconds timeout) const;
  ShellResult wait() const;
  std::optional<ShellResult> result() const;
  void finish(PlatformProcessResult completed);

private:
  static bool isTerminal(ProcessState state) noexcept;

  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  ProcessState state_ = ProcessState::Starting;
  int pid_ = -1;
  bool cancel_requested_ = false;
  std::function<void()> native_cancel_;
  std::optional<ShellResult> result_;
};

PlatformProcessResult executeManagedWindows(const ProcessSpec& spec, ProcessControl& control,
                                            const std::string& command_line_override);
PlatformProcessResult executeManagedPosix(const ProcessSpec& spec, ProcessControl& control);

} // namespace Praktor::Shell::detail
