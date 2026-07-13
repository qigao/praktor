#include "praktor/shell/process_executor.hpp"

#include "managed_process_internal.hpp"

#include <stdexcept>
#include <thread>
#include <utility>

namespace Praktor::Shell {

namespace {

void validateSpec(const ProcessSpec& spec) {
  if (spec.program.empty()) {
    throw std::runtime_error("Process program cannot be empty");
  }
  if (spec.timeout_ms <= 0) {
    throw std::runtime_error("Process timeout must be positive");
  }
  if (spec.cancel_grace_ms < 0) {
    throw std::runtime_error("Process cancel grace period cannot be negative");
  }
  if (spec.max_output_bytes == 0) {
    throw std::runtime_error("Process output limit must be positive");
  }
}

} // namespace

const char* processStateName(ProcessState state) noexcept {
  switch (state) {
    case ProcessState::Starting: return "starting";
    case ProcessState::Running: return "running";
    case ProcessState::Cancelling: return "cancelling";
    case ProcessState::Exited: return "exited";
    case ProcessState::Signaled: return "signaled";
    case ProcessState::TimedOut: return "timed_out";
    case ProcessState::Cancelled: return "cancelled";
    case ProcessState::SpawnFailed: return "spawn_failed";
    case ProcessState::WaitFailed: return "wait_failed";
    case ProcessState::OutputLimitExceeded: return "output_limit_exceeded";
  }
  return "unknown";
}

bool detail::ProcessControl::isTerminal(ProcessState state) noexcept {
  return state == ProcessState::Exited || state == ProcessState::Signaled ||
         state == ProcessState::TimedOut || state == ProcessState::Cancelled ||
         state == ProcessState::SpawnFailed || state == ProcessState::WaitFailed ||
         state == ProcessState::OutputLimitExceeded;
}

bool detail::ProcessControl::publishRunning(int pid, std::function<void()> native_cancel) {
  std::lock_guard<std::mutex> lock(mutex_);
  pid_ = pid;
  native_cancel_ = std::move(native_cancel);
  state_ = cancel_requested_ ? ProcessState::Cancelling : ProcessState::Running;
  if (cancel_requested_) {
    native_cancel_();
  }
  changed_.notify_all();
  return !cancel_requested_;
}

void detail::ProcessControl::releaseNative() {
  std::lock_guard<std::mutex> lock(mutex_);
  native_cancel_ = {};
}

bool detail::ProcessControl::probeCompletion(const std::function<bool()>& probe) {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool completed = probe();
  if (completed) {
    native_cancel_ = {};
  }
  return completed;
}

bool detail::ProcessControl::requestCancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (isTerminal(state_) || cancel_requested_ ||
      (state_ != ProcessState::Starting && !native_cancel_)) {
    return false;
  }
  cancel_requested_ = true;
  state_ = ProcessState::Cancelling;
  if (native_cancel_) {
    native_cancel_();
  }
  changed_.notify_all();
  return true;
}

bool detail::ProcessControl::cancelRequested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancel_requested_;
}

int detail::ProcessControl::pid() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pid_;
}

ProcessState detail::ProcessControl::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool detail::ProcessControl::waitFor(std::chrono::milliseconds timeout) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return changed_.wait_for(lock, timeout, [this]() { return isTerminal(state_); });
}

ShellResult detail::ProcessControl::wait() const {
  std::unique_lock<std::mutex> lock(mutex_);
  changed_.wait(lock, [this]() { return isTerminal(state_); });
  return *result_;
}

std::optional<ShellResult> detail::ProcessControl::result() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return result_;
}

void detail::ProcessControl::finish(PlatformProcessResult completed) {
  std::lock_guard<std::mutex> lock(mutex_);
  native_cancel_ = {};
  if (cancel_requested_ && completed.state != ProcessState::TimedOut &&
      completed.state != ProcessState::OutputLimitExceeded &&
      completed.state != ProcessState::SpawnFailed) {
    completed.state = ProcessState::Cancelled;
    completed.result.exit_code = -1;
  }
  completed.result.pid = pid_;
  completed.result.state = completed.state;
  state_ = completed.state;
  result_ = std::move(completed.result);
  changed_.notify_all();
}

struct ManagedProcess::Impl {
  explicit Impl(ProcessSpec process_spec, ShellExecutor::StreamCallback callback,
                std::string command_line)
      : spec(std::move(process_spec)), stream_callback(std::move(callback)),
        command_line_override(std::move(command_line)), worker([this]() {
          ShellExecutor::setStreamCallback(std::move(stream_callback));
          detail::PlatformProcessResult completed;
          try {
#if defined(_WIN32)
            completed = detail::executeManagedWindows(spec, control, command_line_override);
#else
            completed = detail::executeManagedPosix(spec, control);
#endif
          } catch (const std::exception& error) {
            completed.result.exit_code = -1;
            completed.result.stderr_output = error.what();
            completed.state = ProcessState::WaitFailed;
          } catch (...) {
            completed.result.exit_code = -1;
            completed.result.stderr_output = "Unknown process management failure";
            completed.state = ProcessState::WaitFailed;
          }
          control.finish(std::move(completed));
        }) {}

  ~Impl() noexcept {
    control.requestCancel();
    if (worker.joinable()) {
      worker.join();
    }
  }

  ProcessSpec spec;
  ShellExecutor::StreamCallback stream_callback;
  std::string command_line_override;
  detail::ProcessControl control;
  std::thread worker;
};

ManagedProcess::ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ManagedProcess::~ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(ManagedProcess&& other) noexcept = default;
ManagedProcess& ManagedProcess::operator=(ManagedProcess&& other) noexcept = default;

bool ManagedProcess::valid() const noexcept { return static_cast<bool>(impl_); }
int ManagedProcess::pid() const noexcept { return impl_ ? impl_->control.pid() : -1; }
ProcessState ManagedProcess::state() const noexcept {
  return impl_ ? impl_->control.state() : ProcessState::SpawnFailed;
}
bool ManagedProcess::isRunning() const noexcept {
  const ProcessState current = state();
  return current == ProcessState::Starting || current == ProcessState::Running ||
         current == ProcessState::Cancelling;
}
bool ManagedProcess::waitFor(std::chrono::milliseconds timeout) const {
  return impl_ && impl_->control.waitFor(timeout);
}
ShellResult ManagedProcess::wait() const {
  if (!impl_) {
    throw std::logic_error("Cannot wait on an empty managed process");
  }
  return impl_->control.wait();
}
bool ManagedProcess::cancel() { return impl_ && impl_->control.requestCancel(); }
std::optional<ShellResult> ManagedProcess::result() const {
  return impl_ ? impl_->control.result() : std::nullopt;
}

ManagedProcess ProcessExecutor::start(const ProcessSpec& spec) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(
      spec, ShellExecutor::getStreamCallback(), std::string{}));
}

ManagedProcess ProcessExecutor::startShell(const ProcessSpec& spec,
                                           std::string windows_command_line) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(
      spec, ShellExecutor::getStreamCallback(), std::move(windows_command_line)));
}

ShellResult ProcessExecutor::execute(const ProcessSpec& spec) {
  return start(spec).wait();
}

} // namespace Praktor::Shell
