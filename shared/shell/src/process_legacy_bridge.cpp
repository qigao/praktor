#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "praktor/shell/process_executor.hpp"
#include "praktor/process_worker.h"
#include <stdexcept>
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
size_t metadataBudget(const ProcessSpec& spec) {
  size_t bytes = 0;
  const auto add = [&bytes](size_t size) {
    if (size >= SIZE_MAX-bytes) throw std::length_error("Process metadata size overflow");
    bytes += size;
  };
  add(spec.program.size()+1);
  if (!spec.working_dir.empty()) add(spec.working_dir.size()+1);
  if (spec.args.size() >= SIZE_MAX/sizeof(char*) || spec.env.size() >= SIZE_MAX/sizeof(char*))
    throw std::length_error("Process metadata count overflow");
  add((spec.args.size()+1)*sizeof(char*));
  for (const auto& arg : spec.args) add(arg.size()+1);
  if (!spec.env.empty()) {
    add((spec.env.size()+1)*sizeof(char*));
    for (const auto& entry : spec.env) { add(entry.first.size()); add(entry.second.size()); add(2); }
  }
  return bytes;
}
static_assert(static_cast<int>(ProcessState::Starting) == PRAKTOR_PROCESS_STARTING);
static_assert(static_cast<int>(ProcessState::Running) == PRAKTOR_PROCESS_RUNNING);
static_assert(static_cast<int>(ProcessState::Cancelling) == PRAKTOR_PROCESS_CANCELLING);
static_assert(static_cast<int>(ProcessState::Exited) == PRAKTOR_PROCESS_EXITED);
static_assert(static_cast<int>(ProcessState::Signaled) == PRAKTOR_PROCESS_SIGNALED);
static_assert(static_cast<int>(ProcessState::TimedOut) == PRAKTOR_PROCESS_TIMED_OUT);
static_assert(static_cast<int>(ProcessState::Cancelled) == PRAKTOR_PROCESS_CANCELLED);
static_assert(static_cast<int>(ProcessState::SpawnFailed) == PRAKTOR_PROCESS_SPAWN_FAILED);
static_assert(static_cast<int>(ProcessState::WaitFailed) == PRAKTOR_PROCESS_WAIT_FAILED);
static_assert(static_cast<int>(ProcessState::OutputLimitExceeded) == PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED);
ShellResult convert(const praktor_process_worker_view& view) {
  ShellResult result;
  result.state = static_cast<ProcessState>(view.state); result.exit_code = view.result.exit_code;
  result.pid = view.result.pid; result.termination_signal = view.result.term_signal;
  result.output_streamed_live = view.output_streamed_live;
  result.stdout_output.assign(view.stdout_data,view.stdout_size);
  result.stderr_output.assign(view.stderr_data,view.stderr_size);
  if (view.diagnostic) {
    if (!result.stderr_output.empty()) result.stderr_output += '\n';
    result.stderr_output += view.diagnostic;
  }
  return result;
}
} // namespace

const char* processStateName(ProcessState state) noexcept {
  return praktor_process_state_name(static_cast<praktor_process_state>(state));
}

/* Explicit C++ value/callback conversion only. The joined thread, admission
 * copies, cancellation, completion state, synchronization and buffers live in C. */
struct ManagedProcess::Impl {
  praktor_process_worker worker{};
  ShellExecutor::StreamCallback callback;
  static int setup(void* user) noexcept {
    auto* self = static_cast<Impl*>(user);
    try { ShellExecutor::setStreamCallback(std::move(self->callback)); return SALTS_OK; }
    catch (...) { return SALTS_EIO; }
  }
  static int emit(void*, const char* line, size_t size) noexcept {
    try { ShellExecutor::emitStreamLine(std::string(line,size)); return SALTS_OK; }
    catch (...) { return SALTS_EIO; }
  }
  Impl(const ProcessSpec& spec, bool shell, ShellExecutor::StreamCallback stream)
      : callback(std::move(stream)) {
    std::vector<const char*> args;
    for (const auto& arg : spec.args) args.push_back(arg.c_str());
    args.push_back(nullptr);
    std::vector<std::string> storage;
    for (const auto& entry : spec.env) storage.push_back(entry.first+"="+entry.second);
    std::vector<const char*> env;
    for (const auto& entry : storage) env.push_back(entry.c_str());
    env.push_back(nullptr);
    praktor_process_worker_config config;
    praktor_process_worker_config_init(&config);
    config.options.program = spec.program.c_str(); config.options.args = shell ? nullptr : args.data();
    config.options.env = spec.env.empty() ? nullptr : env.data();
    config.options.cwd = spec.working_dir.empty() ? nullptr : spec.working_dir.c_str();
    config.options.flags = shell ? SALTS_PROCESS_SHELL_COMMAND : 0;
    config.options.timeout_ms = static_cast<uint64_t>(spec.timeout_ms);
    config.input = spec.input.data(); config.input_size = config.max_input_bytes = spec.input.size();
    config.max_output_bytes = spec.max_output_bytes; config.max_metadata_bytes = metadataBudget(spec);
    config.max_arguments = spec.args.size(); config.max_environment_entries = spec.env.size();
    config.thread_start = setup; config.emit = spec.stream_output ? emit : nullptr; config.user = this;
    const int status = praktor_process_worker_start(&worker,&config);
    if (status != SALTS_OK) throw std::runtime_error("C11 Process worker initialization failed (status "+std::to_string(status)+")");
  }
  ~Impl() noexcept { if (praktor_process_worker_destroy(&worker) != SALTS_OK) std::terminate(); }
};

ManagedProcess::ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ManagedProcess::~ManagedProcess() noexcept = default;
ManagedProcess::ManagedProcess(ManagedProcess&& other) noexcept = default;
ManagedProcess& ManagedProcess::operator=(ManagedProcess&& other) noexcept = default;
bool ManagedProcess::valid() const noexcept { return static_cast<bool>(impl_); }
ProcessState ManagedProcess::state() const noexcept {
  praktor_process_state state;
  return impl_ && praktor_process_worker_state(&impl_->worker,&state) == SALTS_OK
      ? static_cast<ProcessState>(state) : ProcessState::SpawnFailed;
}
bool ManagedProcess::isRunning() const noexcept {
  const auto value = state();
  return value == ProcessState::Starting || value == ProcessState::Running || value == ProcessState::Cancelling;
}
bool ManagedProcess::waitFor(std::chrono::milliseconds timeout) const {
  if (!impl_) return false;
  const auto ms = timeout.count();
  const uint64_t ns = ms <= 0 ? 0 : static_cast<uint64_t>(ms) > (UINT64_MAX-1)/UINT64_C(1000000)
      ? UINT64_MAX-1 : static_cast<uint64_t>(ms)*UINT64_C(1000000);
  const int status = praktor_process_worker_wait(&impl_->worker,ns);
  if (status != SALTS_OK && status != SALTS_ETIMEDOUT) throw std::runtime_error("C11 Process worker wait failed");
  return status == SALTS_OK;
}
ShellResult ManagedProcess::wait() const {
  if (!impl_) throw std::logic_error("Cannot wait on an empty managed process");
  if (praktor_process_worker_wait(&impl_->worker,UINT64_MAX) != SALTS_OK) throw std::runtime_error("C11 Process worker wait failed");
  return *result();
}
bool ManagedProcess::cancel() {
  bool accepted = false;
  return impl_ && praktor_process_worker_cancel(&impl_->worker,&accepted) == SALTS_OK && accepted;
}
std::optional<ShellResult> ManagedProcess::result() const {
  if (!impl_) return std::nullopt;
  praktor_process_worker_view view{};
  const int status = praktor_process_worker_result(&impl_->worker,&view);
  if (status == SALTS_EBUSY) return std::nullopt;
  if (status != SALTS_OK) throw std::runtime_error("C11 Process worker result failed");
  return convert(view);
}
ManagedProcess ProcessExecutor::start(const ProcessSpec& spec) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(spec,false,ShellExecutor::getStreamCallback()));
}
ManagedProcess ProcessExecutor::startShell(const ProcessSpec& spec) {
  validateSpec(spec);
  return ManagedProcess(std::make_unique<ManagedProcess::Impl>(spec,true,ShellExecutor::getStreamCallback()));
}
ShellResult ProcessExecutor::execute(const ProcessSpec& spec) { return start(spec).wait(); }
} // namespace Praktor::Shell
