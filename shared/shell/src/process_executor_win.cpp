#include "managed_process_internal.hpp"

#include "shell_executor_internal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

namespace Praktor::Shell::detail {

namespace {

constexpr DWORD kPollIntervalMs = 25;
constexpr DWORD kManagedProcessExitCode = 1;

struct CaseInsensitiveLess {
  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
  }
};

void closeHandle(HANDLE& handle) noexcept {
  if (handle != NULL && handle != INVALID_HANDLE_VALUE) {
    CloseHandle(handle);
    handle = NULL;
  }
}

void appendError(std::string& stderr_output, const std::string& message) {
  if (!stderr_output.empty() && stderr_output.back() != '\n') {
    stderr_output.push_back('\n');
  }
  stderr_output += message;
}

std::vector<char> buildEnvironmentBlock(const std::map<std::string, std::string>& env) {
  if (env.empty()) {
    return {};
  }

  std::map<std::string, std::string, CaseInsensitiveLess> merged_env;
  LPCH current_block = GetEnvironmentStringsA();
  if (current_block != nullptr) {
    for (LPCCH cursor = current_block; *cursor != '\0'; cursor += std::strlen(cursor) + 1) {
      const std::string entry(cursor);
      const size_t equals = entry[0] == '=' ? entry.find('=', 1) : entry.find('=');
      if (equals != std::string::npos) {
        merged_env[entry.substr(0, equals)] = entry.substr(equals + 1);
      }
    }
    FreeEnvironmentStringsA(current_block);
  }

  for (const auto& [key, value] : env) {
    merged_env[key] = value;
  }

  std::vector<char> env_block;
  for (const auto& [key, value] : merged_env) {
    const std::string entry = key + "=" + value;
    env_block.insert(env_block.end(), entry.begin(), entry.end());
    env_block.push_back('\0');
  }
  env_block.push_back('\0');
  return env_block;
}

bool needsQuoting(const std::string& arg) {
  return arg.empty() || arg.find_first_of(" \t\n\v\"") != std::string::npos;
}

std::string quoteWindowsArg(const std::string& arg) {
  if (!needsQuoting(arg)) {
    return arg;
  }

  std::string quoted = "\"";
  size_t backslashes = 0;
  for (char ch : arg) {
    if (ch == '\\') {
      ++backslashes;
      continue;
    }
    if (ch == '"') {
      quoted.append(backslashes * 2 + 1, '\\');
      quoted.push_back('"');
      backslashes = 0;
      continue;
    }
    quoted.append(backslashes, '\\');
    backslashes = 0;
    quoted.push_back(ch);
  }
  quoted.append(backslashes * 2, '\\');
  quoted.push_back('"');
  return quoted;
}

std::string buildCommandLine(const ProcessSpec& spec) {
  std::string command_line = quoteWindowsArg(spec.program);
  for (const auto& arg : spec.args) {
    command_line.push_back(' ');
    command_line += quoteWindowsArg(arg);
  }
  return command_line;
}

bool appendCaptured(std::string& sink, const char* buffer, DWORD bytes_read,
                    std::size_t& captured_bytes, const ProcessSpec& spec,
                    std::string& pending_line) {
  if (bytes_read == 0) {
    return true;
  }
  const std::size_t remaining = spec.max_output_bytes - captured_bytes;
  const std::size_t accepted = std::min<std::size_t>(bytes_read, remaining);
  sink.append(buffer, accepted);
  mirrorWithPrefix(buffer, accepted, spec.stream_output, pending_line);
  captured_bytes += accepted;
  return accepted == bytes_read;
}

bool drainPipe(HANDLE pipe, std::string& sink, std::size_t& captured_bytes,
               const ProcessSpec& spec, std::string& pending_line) {
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL) || available == 0) {
      return true;
    }

    char buffer[4096];
    const DWORD bytes_to_read = std::min<DWORD>(available, sizeof(buffer));
    DWORD bytes_read = 0;
    if (!ReadFile(pipe, buffer, bytes_to_read, &bytes_read, NULL) || bytes_read == 0) {
      return true;
    }
    if (!appendCaptured(sink, buffer, bytes_read, captured_bytes, spec, pending_line)) {
      return false;
    }
  }
}

void writeInputAndClose(HANDLE pipe, const std::string& input, const std::atomic_bool& stop) {
  size_t total_written = 0;
  while (!stop.load(std::memory_order_relaxed) && total_written < input.size()) {
    DWORD bytes_written = 0;
    const DWORD remaining =
        static_cast<DWORD>(std::min<size_t>(input.size() - total_written, 1u << 15));
    if (!WriteFile(pipe, input.data() + total_written, remaining, &bytes_written, NULL) ||
        bytes_written == 0) {
      break;
    }
    total_written += bytes_written;
  }
  CloseHandle(pipe);
}

void stopAndJoinWriter(std::thread& writer, std::atomic_bool& stop) noexcept {
  stop.store(true, std::memory_order_relaxed);
  CancelSynchronousIo(writer.native_handle());
  if (writer.joinable()) {
    writer.join();
  }
}

HANDLE createKillOnCloseJob(HANDLE process) {
  HANDLE job = CreateJobObjectA(NULL, NULL);
  if (job == NULL) {
    return NULL;
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job, process)) {
    CloseHandle(job);
    return NULL;
  }
  return job;
}

} // namespace

PlatformProcessResult executeManagedWindows(const ProcessSpec& spec, ProcessControl& control,
                                            const std::string& command_line_override) {
  PlatformProcessResult completed;
  completed.result.output_streamed_live = spec.stream_output;

  HANDLE stdin_read = NULL;
  HANDLE stdin_write = NULL;
  HANDLE stdout_read = NULL;
  HANDLE stdout_write = NULL;
  HANDLE stderr_read = NULL;
  HANDLE stderr_write = NULL;
  SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  if (!CreatePipe(&stdin_read, &stdin_write, &security, 0) ||
      !CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
      !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to create process pipes";
    completed.state = ProcessState::SpawnFailed;
    closeHandle(stdin_read);
    closeHandle(stdin_write);
    closeHandle(stdout_read);
    closeHandle(stdout_write);
    closeHandle(stderr_read);
    closeHandle(stderr_write);
    return completed;
  }

  if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to isolate parent pipe handles";
    completed.state = ProcessState::SpawnFailed;
    closeHandle(stdin_read);
    closeHandle(stdin_write);
    closeHandle(stdout_read);
    closeHandle(stdout_write);
    closeHandle(stderr_read);
    closeHandle(stderr_write);
    return completed;
  }

  STARTUPINFOA startup = {sizeof(STARTUPINFOA)};
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.hStdInput = stdin_read;
  startup.hStdOutput = stdout_write;
  startup.hStdError = stderr_write;
  startup.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION process_info = {};
  std::string command_line = command_line_override.empty()
                                 ? buildCommandLine(spec)
                                 : command_line_override;
  std::vector<char> environment = buildEnvironmentBlock(spec.env);
  const DWORD creation_flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
  const BOOL created = CreateProcessA(
      NULL, command_line.data(), NULL, NULL, TRUE, creation_flags,
      environment.empty() ? NULL : environment.data(),
      spec.working_dir.empty() ? NULL : spec.working_dir.c_str(), &startup, &process_info);

  closeHandle(stdin_read);
  closeHandle(stdout_write);
  closeHandle(stderr_write);

  if (!created) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to create process (Win32 error " +
                                      std::to_string(GetLastError()) + ")";
    completed.state = ProcessState::SpawnFailed;
    closeHandle(stdin_write);
    closeHandle(stdout_read);
    closeHandle(stderr_read);
    return completed;
  }

  HANDLE job = createKillOnCloseJob(process_info.hProcess);
  if (job == NULL) {
    TerminateProcess(process_info.hProcess, kManagedProcessExitCode);
    WaitForSingleObject(process_info.hProcess, INFINITE);
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to place process in a managed Job Object";
    completed.state = ProcessState::SpawnFailed;
    closeHandle(process_info.hProcess);
    closeHandle(process_info.hThread);
    closeHandle(stdin_write);
    closeHandle(stdout_read);
    closeHandle(stderr_read);
    return completed;
  }

  const int pid = static_cast<int>(process_info.dwProcessId);
  const bool should_start = control.publishRunning(pid, [job]() {
    TerminateJobObject(job, kManagedProcessExitCode);
  });
  if (should_start && ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
    TerminateJobObject(job, kManagedProcessExitCode);
    control.releaseNative();
    WaitForSingleObject(process_info.hProcess, INFINITE);
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to resume managed process";
    completed.state = ProcessState::SpawnFailed;
    closeHandle(job);
    closeHandle(process_info.hProcess);
    closeHandle(process_info.hThread);
    closeHandle(stdin_write);
    closeHandle(stdout_read);
    closeHandle(stderr_read);
    return completed;
  }

  std::atomic_bool stop_input{!should_start};
  std::thread input_writer([stdin_write, &spec, &stop_input]() {
    writeInputAndClose(stdin_write, spec.input, stop_input);
  });
  stdin_write = NULL;

  const auto started_at = std::chrono::steady_clock::now();
  std::string stdout_pending_line;
  std::string stderr_pending_line;
  std::size_t captured_bytes = 0;
  ProcessState terminal_state = ProcessState::Exited;

  for (;;) {
    const bool stdout_ok = drainPipe(stdout_read, completed.result.stdout_output, captured_bytes,
                                     spec, stdout_pending_line);
    const bool stderr_ok = drainPipe(stderr_read, completed.result.stderr_output, captured_bytes,
                                     spec, stderr_pending_line);
    if (!stdout_ok || !stderr_ok) {
      terminal_state = ProcessState::OutputLimitExceeded;
      TerminateJobObject(job, kManagedProcessExitCode);
      break;
    }

    const DWORD wait_result = WaitForSingleObject(process_info.hProcess, kPollIntervalMs);
    if (wait_result == WAIT_OBJECT_0) {
      terminal_state = control.cancelRequested() ? ProcessState::Cancelled : ProcessState::Exited;
      break;
    }
    if (wait_result != WAIT_TIMEOUT) {
      terminal_state = ProcessState::WaitFailed;
      TerminateJobObject(job, kManagedProcessExitCode);
      break;
    }

    if (std::chrono::steady_clock::now() - started_at >=
        std::chrono::milliseconds(spec.timeout_ms)) {
      terminal_state = ProcessState::TimedOut;
      TerminateJobObject(job, kManagedProcessExitCode);
      break;
    }
  }

  WaitForSingleObject(process_info.hProcess, INFINITE);
  control.releaseNative();
  stopAndJoinWriter(input_writer, stop_input);
  const bool final_stdout_ok = drainPipe(stdout_read, completed.result.stdout_output,
                                         captured_bytes, spec, stdout_pending_line);
  const bool final_stderr_ok = drainPipe(stderr_read, completed.result.stderr_output,
                                         captured_bytes, spec, stderr_pending_line);
  if ((!final_stdout_ok || !final_stderr_ok) && terminal_state == ProcessState::Exited) {
    terminal_state = ProcessState::OutputLimitExceeded;
  }
  flushPendingLine(spec.stream_output, stdout_pending_line);
  flushPendingLine(spec.stream_output, stderr_pending_line);

  DWORD exit_code = kManagedProcessExitCode;
  if (!GetExitCodeProcess(process_info.hProcess, &exit_code)) {
    terminal_state = ProcessState::WaitFailed;
  }
  completed.result.exit_code = terminal_state == ProcessState::Exited
                                   ? static_cast<int>(exit_code)
                                   : -1;
  if (terminal_state == ProcessState::TimedOut) {
    appendError(completed.result.stderr_output, "Command timed out");
  } else if (terminal_state == ProcessState::Cancelled) {
    appendError(completed.result.stderr_output, "Command cancelled");
  } else if (terminal_state == ProcessState::OutputLimitExceeded) {
    appendError(completed.result.stderr_output, "Captured process output exceeded the configured limit");
  } else if (terminal_state == ProcessState::WaitFailed) {
    appendError(completed.result.stderr_output, "Failed while waiting for process");
  }
  completed.state = terminal_state;

  closeHandle(job);
  closeHandle(process_info.hProcess);
  closeHandle(process_info.hThread);
  closeHandle(stdout_read);
  closeHandle(stderr_read);
  return completed;
}

} // namespace Praktor::Shell::detail
