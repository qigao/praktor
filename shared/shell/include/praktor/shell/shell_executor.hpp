#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace actions {
class ShellExecutor;
}

namespace Praktor::Shell {

enum class ProcessState {
  Starting,
  Running,
  Cancelling,
  Exited,
  Signaled,
  TimedOut,
  Cancelled,
  SpawnFailed,
  WaitFailed,
  OutputLimitExceeded
};

const char* processStateName(ProcessState state) noexcept;

struct ShellResult {
  int exit_code = 0;
  std::string stdout_output;
  std::string stderr_output;
  int pid = -1;
  int termination_signal = 0;
  bool output_streamed_live = false;
  ProcessState state = ProcessState::Exited;

  bool success() const { return state == ProcessState::Exited && exit_code == 0; }
};

class ManagedProcess {
public:
  /** Empty handle. Operations other than observers and move assignment are invalid. */
  ManagedProcess() noexcept;
  ~ManagedProcess() noexcept;
  ManagedProcess(ManagedProcess&& other) noexcept;
  ManagedProcess& operator=(ManagedProcess&& other) noexcept;
  ManagedProcess(const ManagedProcess&) = delete;
  ManagedProcess& operator=(const ManagedProcess&) = delete;

  /** Returns whether this handle owns a process session. */
  bool valid() const noexcept;
  /** Returns the root process ID after spawn, or -1 while unavailable. */
  int pid() const noexcept;
  /** Returns the current lifecycle state. Safe to call concurrently. */
  ProcessState state() const noexcept;
  /** Returns true while starting, running, or cancelling. */
  bool isRunning() const noexcept;
  /** Waits up to timeout for a terminal state; does not cancel on timeout. */
  bool waitFor(std::chrono::milliseconds timeout) const;
  /** Waits for and returns the terminal result. Throws for an empty handle. */
  ShellResult wait() const;
  /** Requests process-tree cancellation. Returns false if no request was accepted. */
  bool cancel();
  /** Returns the terminal result when available without blocking. */
  std::optional<ShellResult> result() const;

private:
  struct Impl;
  explicit ManagedProcess(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;

  friend class ProcessExecutor;
  friend class ShellExecutor;
};

class ShellExecutor {
public:
  using StreamCallback = std::function<void(const std::string&)>;

  static ShellResult execute(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      int timeout_ms = 30000,
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true);

  /** Starts a managed shell command. The returned handle owns process-tree cleanup. */
  static ManagedProcess start(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      int timeout_ms = 30000,
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true);

  static void executeAsync(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true);

  static bool killProcess(int pid);

  static void setStreamCallback(StreamCallback callback);
  static void emitStreamLine(const std::string& line);

private:
  friend class ::actions::ShellExecutor;
  friend class ProcessExecutor;

  static StreamCallback getStreamCallback();

  static bool killWindowsProcess(int pid);
  static bool killLinuxProcess(int pid);
  static bool killBsdProcess(int pid);
};

} // namespace Praktor::Shell
