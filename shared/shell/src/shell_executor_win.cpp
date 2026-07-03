#include "praktor/shell/shell_executor.hpp"

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

void appendAndMirror(std::string& sink, const char* buffer, DWORD bytes_read, std::ostream& stream,
                     bool stream_output, std::string& pending_line) {
  if (bytes_read == 0) {
    return;
  }
  sink.append(buffer, buffer + bytes_read);
  (void)stream;
  detail::mirrorWithPrefix(buffer, bytes_read, stream_output, pending_line);
}

void drainPipe(HANDLE pipe, std::string& sink, std::ostream& stream, bool stream_output,
               std::string& pending_line) {
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

    appendAndMirror(sink, buffer, bytes_read, stream, stream_output, pending_line);
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
    // Log warning: Job Object creation failed (common in CI environments)
    std::cerr << "[WARN] Failed to create Job Object (Error: " << GetLastError() 
              << "). Subprocess cleanup may be incomplete in CI environments.\n";
    return NULL;
  }

  if (!AssignProcessToJobObject(job, process_handle)) {
    std::cerr << "[WARN] Failed to assign process to Job Object (Error: " << GetLastError() 
              << "). Process may already belong to another job (common in CI). "
              << "Subprocess cleanup may be incomplete.\n";
    CloseHandle(job);
    return NULL;
  }

  return job;
}

} // namespace

ShellResult ShellExecutor::executeWindows(const std::string& command, const std::string& input,
                                          const std::string& working_dir, int timeout_ms,
                                          const std::map<std::string, std::string>& env,
                                          bool stream_output) {
  ShellResult result;
  result.output_streamed_live = stream_output;

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
  std::string cmdLine = "cmd.exe /c \"" + command + "\"";
  std::vector<char> env_block = buildEnvironmentBlock(env);
  void* env_ptr = env_block.empty() ? NULL : env_block.data();

  BOOL success = CreateProcessA(NULL, const_cast<char*>(cmdLine.c_str()), NULL, NULL, TRUE,
                                CREATE_NO_WINDOW,
                                env_ptr, working_dir.empty() ? NULL : working_dir.c_str(), &si,
                                &pi);

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

  writeInputAndClose(hStdinWrite, input);
  HANDLE job = createProcessJob(pi.hProcess);

  result.pid = static_cast<int>(pi.dwProcessId);
  std::string stdout_pending_line;
  std::string stderr_pending_line;

  const DWORD poll_interval_ms = 50;
  auto start = std::chrono::steady_clock::now();
  for (;;) {
    drainPipe(hStdoutRead, result.stdout_output, std::cout, stream_output, stdout_pending_line);
    drainPipe(hStderrRead, result.stderr_output, std::cerr, stream_output, stderr_pending_line);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, poll_interval_ms);
    if (wait_result == WAIT_OBJECT_0) {
      break;
    }
    if (wait_result == WAIT_TIMEOUT) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      if (elapsed > timeout_ms) {
        if (job != NULL) {
          TerminateJobObject(job, 1);
        } else {
          TerminateProcess(pi.hProcess, 1);
        }
        WaitForSingleObject(pi.hProcess, 1000);
        drainPipe(hStdoutRead, result.stdout_output, std::cout, stream_output, stdout_pending_line);
        drainPipe(hStderrRead, result.stderr_output, std::cerr, stream_output, stderr_pending_line);
        detail::flushPendingLine(stream_output, stdout_pending_line);
        detail::flushPendingLine(stream_output, stderr_pending_line);
        if (!result.stderr_output.empty() && result.stderr_output.back() != '\n') {
          result.stderr_output += '\n';
        }
        result.stderr_output += "Command timed out";
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

  drainPipe(hStdoutRead, result.stdout_output, std::cout, stream_output, stdout_pending_line);
  drainPipe(hStderrRead, result.stderr_output, std::cerr, stream_output, stderr_pending_line);
  detail::flushPendingLine(stream_output, stdout_pending_line);
  detail::flushPendingLine(stream_output, stderr_pending_line);

  if (job != NULL) {
    CloseHandle(job);
  }
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(hStdoutRead);
  CloseHandle(hStderrRead);

  return result;
}

bool ShellExecutor::killWindowsProcess(int pid) {
  if (pid <= 0) {
    return false;
  }

  HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (hProcess == NULL) {
    return false;
  }
  BOOL result = TerminateProcess(hProcess, 1);
  CloseHandle(hProcess);
  return result != 0;
}

} // namespace Praktor::Shell
