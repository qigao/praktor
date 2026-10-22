#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "praktor/shell/process_executor.hpp"

#include "praktor/process_driver.h"
#include <salts/error_codes.h>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>


namespace Praktor::Shell {
namespace {
void validateSpec(const ProcessSpec& spec) {
  if (spec.program.empty() || spec.program.find('\0') != std::string::npos)
    throw std::runtime_error("Process program must be a non-empty native string");
  if (spec.timeout_ms <= 0 || spec.max_output_bytes == 0)
    throw std::runtime_error("Process timeout and output limit must be positive");
  if (spec.working_dir.find('\0') != std::string::npos)
    throw std::runtime_error("Process working directory contains NUL");
  for (const auto& arg : spec.args)
    if (arg.find('\0') != std::string::npos)
      throw std::runtime_error("Process argument contains NUL");
  for (const auto& entry : spec.env)
    if (entry.first.empty() || entry.first.find('=') != std::string::npos ||
        entry.first.find('\0') != std::string::npos ||
        entry.second.find('\0') != std::string::npos)
      throw std::runtime_error("Invalid process environment entry");
}
ProcessState terminalState(cmeta_process_state_t state) noexcept {
  switch (state) {
    case SALTS_PROCESS_EXITED: return ProcessState::Exited;
    case SALTS_PROCESS_SIGNALED: return ProcessState::Signaled;
    case SALTS_PROCESS_TIMED_OUT: return ProcessState::TimedOut;
    case SALTS_PROCESS_TERMINATED: return ProcessState::Cancelled;
    case SALTS_PROCESS_OUTPUT_LIMIT_EXCEEDED: return ProcessState::OutputLimitExceeded;
    default: return ProcessState::WaitFailed;
  }
}
}  // namespace

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

// Explicit C++ compatibility surface: synchronized observers and a joined
// scheduling thread only. Process buffers, I/O, capture and teardown live in C.
struct ManagedProcess::Impl {
  ProcessSpec spec;
  bool shell_command;
  ShellExecutor::StreamCallback callback;
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  ProcessState observed = ProcessState::Starting;
  bool cancel_requested = false;
  std::optional<ShellResult> completed;
  std::thread driver;

  Impl(ProcessSpec source, bool shell, ShellExecutor::StreamCallback stream)
      : spec(std::move(source)), shell_command(shell), callback(std::move(stream)),
        driver([this] { run(); }) {}
  ~Impl() noexcept { requestCancel(); if (driver.joinable()) driver.join(); }
  bool requestCancel() {
    std::lock_guard<std::mutex> lock(mutex);
    if (completed || cancel_requested) return false;
    cancel_requested = true; observed = ProcessState::Cancelling; return true;
  }
  static int cancelled(void* user) noexcept {
    auto& self = *static_cast<Impl*>(user);
    std::lock_guard<std::mutex> lock(self.mutex);
    return self.cancel_requested ? 1 : 0;
  }
  static int emit(void* user, const char* line, size_t size) noexcept {
    auto& self = *static_cast<Impl*>(user);
    try { if (self.callback && size != 0u) self.callback(std::string(line, size)); }
    catch (...) { return SALTS_EIO; }
    return SALTS_OK;
  }
  void run() noexcept {
    ShellResult output;
    output.exit_code = -1; output.output_streamed_live = spec.stream_output;
    praktor_process_driver process{};
    bool spawned = false;
    try {
      std::vector<const char*> args;
      for (const auto& arg : spec.args) args.push_back(arg.c_str());
      args.push_back(nullptr);
      std::vector<std::string> env_storage;
      for (const auto& entry : spec.env) env_storage.push_back(entry.first + "=" + entry.second);
      std::vector<const char*> env;
      for (const auto& entry : env_storage) env.push_back(entry.c_str());
      env.push_back(nullptr);
      cmeta_process_options_t options{}; cmeta_process_options_init(&options);
      options.program = spec.program.c_str(); options.args = shell_command ? nullptr : args.data();
      options.env = spec.env.empty() ? nullptr : env.data();
      options.cwd = spec.working_dir.empty() ? nullptr : spec.working_dir.c_str();
      options.flags = shell_command ? SALTS_PROCESS_SHELL_COMMAND : 0;
      options.timeout_ms = static_cast<uint64_t>(spec.timeout_ms);
      const int start = praktor_process_driver_start(&process, &options,
          spec.input.data(), spec.input.size(), spec.max_output_bytes, nullptr,
          spec.stream_output ? &emit : nullptr, this);
      spawned = start == SALTS_OK;
      if (spawned) {
        { std::lock_guard<std::mutex> lock(mutex);
          if (!cancel_requested) observed = ProcessState::Running; }
        (void)praktor_process_driver_run(&process, &cancelled, this);
      }
      if (process.impl != nullptr) {
        (void)praktor_process_driver_drain(&process);
        praktor_process_view view{};
        (void)praktor_process_driver_view(&process, &view);
        output.stdout_output.assign(view.stdout_data, view.stdout_size);
        output.stderr_output.assign(view.stderr_data, view.stderr_size);
        output.pid = view.result.pid; output.termination_signal = view.result.term_signal;
        output.exit_code = view.result.exit_code;
        output.state = !spawned ? ProcessState::SpawnFailed : view.output_limited
            ? ProcessState::OutputLimitExceeded : view.failed ? ProcessState::WaitFailed
            : terminalState(view.result.state);
        if (!spawned) output.stderr_output = view.error;
      } else {
        output.state = ProcessState::SpawnFailed;
        output.stderr_output = "Salts::Process admission failed (status " + std::to_string(start) + ")";
      }
      if (spawned && cancelled(this) && output.state != ProcessState::TimedOut &&
          output.state != ProcessState::WaitFailed && output.state != ProcessState::OutputLimitExceeded)
        output.state = ProcessState::Cancelled;
      const char* diagnostic = nullptr;
      if (output.state == ProcessState::TimedOut) diagnostic = "Command timed out";
      else if (output.state == ProcessState::Cancelled) diagnostic = "Command cancelled";
      else if (output.state == ProcessState::OutputLimitExceeded)
        diagnostic = "Captured process output exceeded the configured limit";
      else if (output.state == ProcessState::WaitFailed) diagnostic = "Salts::Process I/O or cleanup failed";
      if (diagnostic) {
        output.exit_code = -1;
        if (!output.stderr_output.empty()) output.stderr_output += '\n';
        output.stderr_output += diagnostic;
      }
    } catch (...) { output.state = ProcessState::WaitFailed; output.exit_code = -1; }
    // Even a C++ allocation/callback failure cannot drop live C leases.
    if (process.impl != nullptr) {
      (void)praktor_process_driver_drain(&process);
      if (praktor_process_driver_release(&process) != SALTS_OK) std::terminate();
    }
    { std::lock_guard<std::mutex> lock(mutex);
      observed = output.state; completed = std::move(output); }
    changed.notify_all();
  }
};

ManagedProcess::ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ManagedProcess::~ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(ManagedProcess&& other) noexcept = default;
ManagedProcess& ManagedProcess::operator=(ManagedProcess&& other) noexcept = default;
bool ManagedProcess::valid() const noexcept { return static_cast<bool>(impl_); }
ProcessState ManagedProcess::state() const noexcept {
  if (!impl_) return ProcessState::SpawnFailed;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->observed;
}
bool ManagedProcess::isRunning() const noexcept {
  const auto value = state();
  return value == ProcessState::Starting || value == ProcessState::Running || value == ProcessState::Cancelling;
}
bool ManagedProcess::waitFor(std::chrono::milliseconds timeout) const {
  if (!impl_) return false;
  std::unique_lock<std::mutex> lock(impl_->mutex);
  return impl_->changed.wait_for(lock, timeout, [this] { return impl_->completed.has_value(); });
}
ShellResult ManagedProcess::wait() const {
  if (!impl_) throw std::logic_error("Cannot wait on an empty managed process");
  std::unique_lock<std::mutex> lock(impl_->mutex);
  impl_->changed.wait(lock, [this] { return impl_->completed.has_value(); });
  return *impl_->completed;
}
bool ManagedProcess::cancel() { return impl_ && impl_->requestCancel(); }
std::optional<ShellResult> ManagedProcess::result() const {
  if (!impl_) return std::nullopt;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->completed;
}
ManagedProcess ProcessExecutor::start(const ProcessSpec& spec) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(spec, false, ShellExecutor::getStreamCallback()));
}
ManagedProcess ProcessExecutor::startShell(const ProcessSpec& spec) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(spec, true, ShellExecutor::getStreamCallback()));
}
ShellResult ProcessExecutor::execute(const ProcessSpec& spec) { return start(spec).wait(); }
}  // namespace Praktor::Shell
