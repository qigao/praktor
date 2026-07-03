#include "praktor/shell/process_executor.hpp"

#include "shell_executor_internal.hpp"
#include "shell_executor_posix.hpp"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace Praktor::Shell {

namespace {

void appendAndMirror(std::string& sink, const char* buffer, ssize_t bytes_read,
                     bool stream_output, std::string& pending_line) {
  if (bytes_read <= 0) {
    return;
  }
  sink.append(buffer, buffer + bytes_read);
  detail::mirrorWithPrefix(buffer, static_cast<size_t>(bytes_read), stream_output, pending_line);
}

std::vector<char*> buildArgv(const ProcessSpec& spec, std::vector<std::string>& storage) {
  storage.clear();
  storage.reserve(spec.args.size() + 1);
  storage.push_back(spec.program);
  for (const auto& arg : spec.args) {
    storage.push_back(arg);
  }

  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& item : storage) {
    argv.push_back(item.data());
  }
  argv.push_back(nullptr);
  return argv;
}

bool containsSlash(const std::string& value) {
  return value.find('/') != std::string::npos;
}

std::string envPathValue(const std::map<std::string, std::string>& env) {
  auto it = env.find("PATH");
  if (it != env.end()) {
    return it->second;
  }
  const char* path = std::getenv("PATH");
  return path == nullptr ? "/usr/local/bin:/usr/bin:/bin" : std::string(path);
}

void execWithPathSearch(const ProcessSpec& spec, char* const argv[], char* const envp[]) {
  if (containsSlash(spec.program)) {
    execve(spec.program.c_str(), argv, envp);
    _exit(127);
  }

  std::string path_value = envPathValue(spec.env);
  std::stringstream stream(path_value);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      dir = ".";
    }
    std::string candidate = dir + "/" + spec.program;
    execve(candidate.c_str(), argv, envp);
  }
  _exit(127);
}

ShellResult executePosixProcess(const ProcessSpec& spec) {
  ShellResult result;
  result.output_streamed_live = spec.stream_output;

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

    if (!spec.working_dir.empty()) {
      chdir(spec.working_dir.c_str());
    }

    std::vector<std::string> env_storage;
    std::vector<char*> env_ptrs = detail::buildEnvironmentArray(spec.env, env_storage);
    std::vector<std::string> argv_storage;
    std::vector<char*> argv = buildArgv(spec, argv_storage);
    execWithPathSearch(spec, argv.data(), env_ptrs.data());
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  if (!spec.input.empty()) {
    size_t total_written = 0;
    while (total_written < spec.input.size()) {
      const ssize_t written =
          write(stdin_pipe[1], spec.input.data() + total_written, spec.input.size() - total_written);
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
  int status = 0;
  bool timed_out = false;

  while (true) {
    pid_t wait_result = waitpid(pid, &status, WNOHANG);
    if (wait_result == pid) {
      break;
    }

    char buffer[4096];
    ssize_t bytes_read = 0;
    while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
      appendAndMirror(result.stdout_output, buffer, bytes_read, spec.stream_output,
                      stdout_pending_line);
    }
    while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
      appendAndMirror(result.stderr_output, buffer, bytes_read, spec.stream_output,
                      stderr_pending_line);
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (elapsed > spec.timeout_ms) {
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
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }

  char buffer[4096];
  ssize_t bytes_read = 0;
  while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
    appendAndMirror(result.stdout_output, buffer, bytes_read, spec.stream_output,
                    stdout_pending_line);
  }
  while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
    appendAndMirror(result.stderr_output, buffer, bytes_read, spec.stream_output,
                    stderr_pending_line);
  }
  detail::flushPendingLine(spec.stream_output, stdout_pending_line);
  detail::flushPendingLine(spec.stream_output, stderr_pending_line);

  close(stdout_pipe[0]);
  close(stderr_pipe[0]);

  return result;
}

} // namespace

ShellResult ProcessExecutor::executeLinux(const ProcessSpec& spec) {
  return executePosixProcess(spec);
}

ShellResult ProcessExecutor::executeBsd(const ProcessSpec& spec) {
  return executePosixProcess(spec);
}

} // namespace Praktor::Shell
