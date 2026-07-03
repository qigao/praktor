#include "praktor/shell/process_executor.hpp"

#include "shell_executor_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
#include <windows.h>

namespace Praktor::Shell {

namespace {

struct CaseInsensitiveLess {
  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return _stricmp(lhs.c_str(), rhs.c_str()) < 0;
  }
};

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
      if (equals == std::string::npos) {
        continue;
      }
      merged_env[entry.substr(0, equals)] = entry.substr(equals + 1);
    }
    FreeEnvironmentStringsA(current_block);
  }

  for (const auto& [key, value] : env) {
    merged_env[key] = value;
  }

  std::vector<char> env_block;
  for (const auto& [key, value] : merged_env) {
    std::string entry = key + "=" + value;
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

void appendAndMirror(std::string& sink, const char* buffer, DWORD bytes_read,
                     bool stream_output, std::string& pending_line) {
  if (bytes_read == 0) {
    return;
  }
  sink.append(buffer, buffer + bytes_read);
  detail::mirrorWithPrefix(buffer, bytes_read, stream_output, pending_line);
}

void drainPipe(HANDLE pipe, std::string& sink, bool stream_output, std::string& pending_line) {
  for (;;) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL) || available == 0) {
      break;
    }

    char buffer[4096];
    DWORD bytes_to_read = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
    DWORD bytes_read = 0;
    if (!ReadFile(pipe, buffer, bytes_to_read, &bytes_read, NULL) || bytes_read == 0) {
      break;
    }

    appendAndMirror(sink, buffer, bytes_read, stream_output, pending_line);
  }
}

void writeInputAndClose(HANDLE pipe, const std::string& input) {
  if (pipe == NULL) {
    return;
  }

  if (!input.empty()) {
    size_t total_written = 0;
    while (total_written < input.size()) {
      DWORD bytes_written = 0;
      const DWORD remaining =
          static_cast<DWORD>(std::min<size_t>(input.size() - total_written, 1u << 15));
      if (!WriteFile(pipe, input.data() + total_written, remaining, &bytes_written, NULL) ||
          bytes_written == 0) {
        break;
      }
      total_written += bytes_written;
    }
  }

  CloseHandle(pipe);
}

HANDLE createProcessJob(HANDLE process_handle) {
  HANDLE job = CreateJobObjectA(NULL, NULL);
  if (job == NULL) {
    return NULL;
  }

  if (!AssignProcessToJobObject(job, process_handle)) {
    CloseHandle(job);
    return NULL;
  }

  return job;
}

} // namespace

ShellResult ProcessExecutor::executeWindows(const ProcessSpec& spec) {
  ShellResult result;
  result.output_streamed_live = spec.stream_output;

  HANDLE hStdinRead, hStdinWrite;
  HANDLE hStdoutRead, hStdoutWrite;
  HANDLE hStderrRead, hStderrWrite;
  SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

  if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0) ||
      !CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
      !CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
    result.stderr_output = "Failed to create pipes";
    result.exit_code = -1;
    return result;
  }

  SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si = {sizeof(STARTUPINFOA)};
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = hStdoutWrite;
  si.hStdError = hStderrWrite;
  si.hStdInput = hStdinRead;
  si.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION pi = {0};
  std::string command_line = buildCommandLine(spec);
  std::vector<char> env_block = buildEnvironmentBlock(spec.env);
  void* env_ptr = env_block.empty() ? NULL : env_block.data();

  BOOL success = CreateProcessA(NULL, command_line.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                env_ptr, spec.working_dir.empty() ? NULL : spec.working_dir.c_str(),
                                &si, &pi);

  CloseHandle(hStdinRead);
  CloseHandle(hStdoutWrite);
  CloseHandle(hStderrWrite);

  if (!success) {
    result.stderr_output = "Failed to create process";
    result.exit_code = -1;
    CloseHandle(hStdinWrite);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    return result;
  }

  writeInputAndClose(hStdinWrite, spec.input);
  HANDLE job = createProcessJob(pi.hProcess);

  result.pid = static_cast<int>(pi.dwProcessId);
  std::string stdout_pending_line;
  std::string stderr_pending_line;

  const DWORD poll_interval_ms = 50;
  auto start = std::chrono::steady_clock::now();
  for (;;) {
    drainPipe(hStdoutRead, result.stdout_output, spec.stream_output, stdout_pending_line);
    drainPipe(hStderrRead, result.stderr_output, spec.stream_output, stderr_pending_line);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, poll_interval_ms);
    if (wait_result == WAIT_OBJECT_0) {
      break;
    }
    if (wait_result == WAIT_TIMEOUT) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      if (elapsed > spec.timeout_ms) {
        if (job != NULL) {
          TerminateJobObject(job, 1);
        } else {
          TerminateProcess(pi.hProcess, 1);
        }
        result.stderr_output = "Command timed out";
        result.exit_code = -1;
        if (job != NULL) {
          CloseHandle(job);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return result;
      }
      continue;
    }

    result.stderr_output = "Failed while waiting for process";
    result.exit_code = -1;
    if (job != NULL) {
      CloseHandle(job);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);
    return result;
  }

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  result.exit_code = static_cast<int>(exit_code);

  drainPipe(hStdoutRead, result.stdout_output, spec.stream_output, stdout_pending_line);
  drainPipe(hStderrRead, result.stderr_output, spec.stream_output, stderr_pending_line);
  detail::flushPendingLine(spec.stream_output, stdout_pending_line);
  detail::flushPendingLine(spec.stream_output, stderr_pending_line);

  if (job != NULL) {
    CloseHandle(job);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(hStdoutRead);
  CloseHandle(hStderrRead);

  return result;
}

} // namespace Praktor::Shell
