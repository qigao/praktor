#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "praktor/shell/process_executor.hpp"
#include "shell_executor_internal.hpp"

#include <salts/process.h>
#include <salts/error_codes.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Praktor::Shell {
namespace {
bool stdinPeerClosed(int error) noexcept {
  if (error == SALTS_EPIPE) return true;
#ifdef _WIN32
  return error == -static_cast<int>(ERROR_BROKEN_PIPE) ||
         error == -static_cast<int>(ERROR_NO_DATA);
#else
  return error == -EPIPE;
#endif
}

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

// This facade owns no OS handles, process groups, pipe polling or native wait
// implementation. Exactly one driver calls the C11 Salts::Process API. The
// caller's cancel/observer threads touch only synchronized facade metadata.
struct ManagedProcess::Impl {
  ProcessSpec spec;
  bool shell_command;
  ShellExecutor::StreamCallback callback;
  mutable std::mutex mutex;
  mutable std::condition_variable changed;
  ProcessState observed = ProcessState::Starting;
  bool cancel_requested = false;
  std::optional<ShellResult> completed;

  cflow_process process{};
  ShellResult output;
  cmeta_process_result_t native{};
  bool terminal = false;
  bool closing = false;
  bool input_pending = false;
  bool input_closed = false;
  bool stdout_pending = false;
  bool stderr_pending = false;
  bool stdout_eof = false;
  bool stderr_eof = false;
  bool failed = false;
  bool output_limit = false;
  std::size_t input_offset = 0;
  std::size_t input_chunk = 0;
  std::array<char, 4096> input_buffer{};
  std::array<char, 4096> stdout_buffer{};
  std::array<char, 4096> stderr_buffer{};
  std::string stdout_line;
  std::string stderr_line;
  std::thread driver;  // Last: every borrowed field exists before the thread starts.

  Impl(ProcessSpec source, bool shell, ShellExecutor::StreamCallback stream)
      : spec(std::move(source)), shell_command(shell), callback(std::move(stream)),
        driver([this] { run(); }) {}

  ~Impl() noexcept {
    requestCancel();
    if (driver.joinable()) driver.join();
  }

  bool requestCancel() {
    std::lock_guard<std::mutex> lock(mutex);
    if (completed || cancel_requested) return false;
    cancel_requested = true;
    observed = ProcessState::Cancelling;
    return true;
  }
  bool cancelled() const {
    std::lock_guard<std::mutex> lock(mutex);
    return cancel_requested;
  }

  static void onCompletion(void* user, cflow_io_request_id, cflow_io_lease_id,
                           cflow_process_stream stream,
                           const cflow_io_completion* completion) noexcept {
    auto& self = *static_cast<Impl*>(user);
    if (stream == CFLOW_PROCESS_STDIN) self.input_pending = false;
    else if (stream == CFLOW_PROCESS_STDOUT) self.stdout_pending = false;
    else self.stderr_pending = false;
    if (self.closing) return;  // Buffers stay alive through terminal acknowledgement.
    try {
      if (stream == CFLOW_PROCESS_STDIN) {
        if (completion->kind == CFLOW_IO_COMPLETION_OK) {
          if (completion->bytes == 0 || completion->bytes > self.input_chunk)
            self.failed = true;
          else self.input_offset += completion->bytes;
        } else if (completion->kind == CFLOW_IO_COMPLETION_EOF ||
                   (completion->kind == CFLOW_IO_COMPLETION_FAILED &&
                    stdinPeerClosed(completion->error))) {
          // A child may legitimately exit without consuming all of its input.
          self.input_offset = self.spec.input.size();
        } else self.failed = true;
        return;
      }
      const bool is_stdout = stream == CFLOW_PROCESS_STDOUT;
      auto& eof = is_stdout ? self.stdout_eof : self.stderr_eof;
      auto& buffer = is_stdout ? self.stdout_buffer : self.stderr_buffer;
      auto& sink = is_stdout ? self.output.stdout_output : self.output.stderr_output;
      auto& line = is_stdout ? self.stdout_line : self.stderr_line;
      if (completion->kind == CFLOW_IO_COMPLETION_EOF) { eof = true; return; }
      if (completion->kind != CFLOW_IO_COMPLETION_OK || completion->bytes > buffer.size()) {
        self.failed = true;
        return;
      }
      const auto used = self.output.stdout_output.size() + self.output.stderr_output.size();
      const auto accepted = (std::min)(completion->bytes, self.spec.max_output_bytes - used);
      sink.append(buffer.data(), accepted);
      detail::mirrorWithPrefix(buffer.data(), accepted, self.spec.stream_output, line);
      if (accepted != completion->bytes) self.output_limit = true;
    } catch (...) { self.failed = true; }
  }

  void submitRead(bool is_stdout) {
    auto& pending = is_stdout ? stdout_pending : stderr_pending;
    if (pending || (is_stdout ? stdout_eof : stderr_eof)) return;
    auto& buffer = is_stdout ? stdout_buffer : stderr_buffer;
    const auto submitted = is_stdout
        ? cflow_process_try_read_stdout(&process, 2, buffer.data(), buffer.size())
        : cflow_process_try_read_stderr(&process, 3, buffer.data(), buffer.size());
    if (submitted.status == CFLOW_PROCESS_SUBMIT_ACCEPTED) pending = true;
    else if (submitted.status != CFLOW_PROCESS_SUBMIT_FULL) failed = true;
  }
  void submitInput() {
    if (input_pending || input_closed) return;
    if (!terminal && input_offset < spec.input.size()) {
      input_chunk = (std::min)(input_buffer.size(), spec.input.size() - input_offset);
      std::copy_n(spec.input.data() + input_offset, input_chunk, input_buffer.data());
      const auto submitted = cflow_process_try_write_stdin(&process, 1, input_buffer.data(), input_chunk);
      if (submitted.status == CFLOW_PROCESS_SUBMIT_ACCEPTED) input_pending = true;
      else if (submitted.status != CFLOW_PROCESS_SUBMIT_FULL) failed = true;
    } else {
      const int status = cflow_process_close_stdin(&process);
      if (status == SALTS_OK) input_closed = true;
      else if (status != SALTS_EBUSY) failed = true;
    }
  }

  void drain() noexcept {
    if (process.impl == nullptr) return;
    closing = true;
    // Synchronous facade lifetime: retain this driver, callback and buffers
    // until C11 acknowledgements and destruction succeed. No forced free,
    // detached cleanup, raw-PID termination or alternative native backend.
    bool finalizing = false;
    while (process.impl != nullptr) {
      if (!finalizing) {
        const int close_status = cflow_process_close(&process);
        if (close_status != SALTS_OK && close_status != SALTS_EBUSY) failed = true;
        size_t progressed = 0;
        const int status = cflow_process_run_ready(&process, 64, &progressed);
        if (status != SALTS_OK && status != SALTS_EBUSY) failed = true;
        finalizing = cflow_process_is_quiescent(&process);
      }
      if (finalizing) {
        const int status = cflow_process_destroy(&process);
        if (status != SALTS_OK && status != SALTS_EBUSY) failed = true;
      }
      if (process.impl != nullptr) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void execute() {
    std::vector<const char*> args;
    for (const auto& arg : spec.args) args.push_back(arg.c_str());
    args.push_back(nullptr);
    std::vector<std::string> env_storage;
    for (const auto& entry : spec.env) env_storage.push_back(entry.first + "=" + entry.second);
    std::vector<const char*> env;
    for (const auto& entry : env_storage) env.push_back(entry.c_str());
    env.push_back(nullptr);

    cmeta_process_options_t options{};
    cmeta_process_options_init(&options);
    options.program = spec.program.c_str();
    options.args = shell_command ? nullptr : args.data();
    options.env = spec.env.empty() ? nullptr : env.data();
    options.cwd = spec.working_dir.empty() ? nullptr : spec.working_dir.c_str();
    options.flags = shell_command ? SALTS_PROCESS_SHELL_COMMAND : 0;
    options.timeout_ms = static_cast<uint64_t>(spec.timeout_ms);
    cflow_process_config config{};
#if defined(_WIN32)
    config.backend_kind = CFLOW_IO_NATIVE_IOCP;
#elif defined(__linux__)
    config.backend_kind = CFLOW_IO_NATIVE_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    config.backend_kind = CFLOW_IO_NATIVE_KQUEUE;
#else
#error "Salts::Process pipe backend is required"
#endif
    config.request_capacity = 8;
    config.command_capacity = 8;
    config.completion_batch_capacity = 8;
    config.completion = onCompletion;
    config.completion_user = this;
    const int start_status = cflow_process_start(&process, &options, &config);
    if (start_status != SALTS_OK) {
      output.state = ProcessState::SpawnFailed;
      output.stderr_output = "Salts::Process start failed (status " + std::to_string(start_status) + ")";
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!cancel_requested) observed = ProcessState::Running;
    }
    bool termination_requested = false;
    while (!failed && !output_limit) {
      if (cancelled() && !termination_requested) {
        termination_requested = true;
        if (cflow_process_terminate(&process) != SALTS_OK) failed = true;
      }
      submitRead(true);
      submitRead(false);
      submitInput();
      size_t progressed = 0;
      if (cflow_process_run_ready(&process, 64, &progressed) != SALTS_OK) failed = true;
      const int polled = cflow_process_poll(&process, &native);
      if (polled == SALTS_OK) terminal = true;
      else if (polled != SALTS_EBUSY) failed = true;
      if (terminal && stdout_eof && stderr_eof && input_closed && !input_pending) break;
      if (!progressed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    output.pid = terminal ? native.pid : -1;
    output.termination_signal = terminal ? native.term_signal : 0;
    output.state = terminal ? terminalState(native.state) : ProcessState::WaitFailed;
    output.exit_code = terminal ? native.exit_code : -1;
    if (output_limit) output.state = ProcessState::OutputLimitExceeded;
    else if (failed) output.state = ProcessState::WaitFailed;
    else if (cancelled() && output.state != ProcessState::TimedOut)
      output.state = ProcessState::Cancelled;
    detail::flushPendingLine(spec.stream_output, stdout_line);
    detail::flushPendingLine(spec.stream_output, stderr_line);
  }

  void run() noexcept {
    output.exit_code = -1;
    output.output_streamed_live = spec.stream_output;
    try {
      ShellExecutor::setStreamCallback(std::move(callback));
      execute();
    } catch (...) { failed = true; }
    drain();
    if (failed && output.state != ProcessState::OutputLimitExceeded)
      output.state = ProcessState::WaitFailed;
    try {
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
    {
      std::lock_guard<std::mutex> lock(mutex);
      observed = output.state;
      completed = std::move(output);
    }
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
