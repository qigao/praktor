#include "shell_executor_posix.hpp"

#include "shell_executor_internal.hpp"

#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <signal.h>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

extern char** environ;

namespace Praktor::Shell::detail {

std::vector<char*> buildEnvironmentArray(const std::map<std::string, std::string>& env,
                                         std::vector<std::string>& storage) {
  std::vector<char*> env_ptrs;

  if (env.empty()) {
    for (char** e = ::environ; *e != nullptr; e++) {
      env_ptrs.push_back(*e);
    }
  } else {
    std::map<std::string, std::string> merged_env;

    for (char** e = ::environ; *e != nullptr; ++e) {
      const std::string_view entry(*e);
      const size_t equals = entry.find('=');
      if (equals == std::string_view::npos || equals == 0) {
        continue;
      }
      merged_env[std::string(entry.substr(0, equals))] = std::string(entry.substr(equals + 1));
    }

    for (const auto& [key, value] : env) {
      merged_env[key] = value;
    }

    storage.reserve(merged_env.size());
    for (const auto& [key, value] : merged_env) {
      storage.push_back(key + "=" + value);
    }
    for (auto& entry : storage) {
      env_ptrs.push_back(&entry[0]);
    }
  }

  env_ptrs.push_back(nullptr);
  return env_ptrs;
}

namespace {

void appendAndMirror(std::string& sink, const char* buffer, ssize_t bytes_read, std::ostream& stream,
                     bool stream_output, std::string& pending_line) {
  if (bytes_read <= 0) {
    return;
  }
  sink.append(buffer, buffer + bytes_read);
  (void)stream;
  mirrorWithPrefix(buffer, static_cast<size_t>(bytes_read), stream_output, pending_line);
}

} // namespace

ShellResult executePosixCommand(const std::string& command, const std::string& input,
                                const std::string& working_dir, int timeout_ms,
                                const std::map<std::string, std::string>& env,
                                bool stream_output) {
  ShellResult result;
  result.output_streamed_live = stream_output;

  int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    result.stderr_output = "Failed to create pipes";
    result.exit_code = -1;
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    result.stderr_output = "Failed to fork process";
    result.exit_code = -1;
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return result;
  }

  if (pid == 0) {
    setpgid(0, 0);

    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (!working_dir.empty()) {
      chdir(working_dir.c_str());
    }

    std::vector<std::string> env_storage;
    std::vector<char*> env_ptrs = buildEnvironmentArray(env, env_storage);

    execle("/bin/sh", "sh", "-c", command.c_str(), NULL, env_ptrs.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  if (!input.empty()) {
    size_t total_written = 0;
    while (total_written < input.size()) {
      const ssize_t written = write(stdin_pipe[1], input.data() + total_written, input.size() - total_written);
      if (written <= 0) {
        break;
      }
      total_written += static_cast<size_t>(written);
    }
  }
  close(stdin_pipe[1]);

  result.pid = static_cast<int>(pid);
  std::string stdout_pending_line;
  std::string stderr_pending_line;

  fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
  fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

  auto start = std::chrono::steady_clock::now();
  int status;
  bool timed_out = false;

  while (true) {
    pid_t wait_result = waitpid(pid, &status, WNOHANG);

    if (wait_result == pid) {
      break;
    }

    char buffer[4096];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
      appendAndMirror(result.stdout_output, buffer, bytes_read, std::cout, stream_output,
                      stdout_pending_line);
    }
    while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
      appendAndMirror(result.stderr_output, buffer, bytes_read, std::cerr, stream_output,
                      stderr_pending_line);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();

    if (elapsed > timeout_ms) {
      kill(-pid, SIGKILL);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      timed_out = true;
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (timed_out) {
    result.stderr_output = "Command timed out";
    result.exit_code = -1;
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    return result;
  }

  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  }

  char buffer[4096];
  ssize_t bytes_read;
  while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
    appendAndMirror(result.stdout_output, buffer, bytes_read, std::cout, stream_output,
                    stdout_pending_line);
  }

  while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
    appendAndMirror(result.stderr_output, buffer, bytes_read, std::cerr, stream_output,
                    stderr_pending_line);
  }
  flushPendingLine(stream_output, stdout_pending_line);
  flushPendingLine(stream_output, stderr_pending_line);

  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  return result;
}

bool killPosixProcess(int pid) {
  if (pid <= 0) {
    return false;
  }
  const pid_t process_group = -static_cast<pid_t>(pid);
  if (kill(process_group, SIGTERM) == 0) {
    return true;
  }
  return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

} // namespace Praktor::Shell::detail
