#include "managed_process_internal.hpp"

#include "shell_executor_internal.hpp"
#include "shell_executor_posix.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

namespace Praktor::Shell::detail {

namespace {

constexpr int kPollIntervalMs = 10;

void closeFd(int& fd) noexcept {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

void appendError(std::string& stderr_output, const std::string& message) {
  if (!stderr_output.empty() && stderr_output.back() != '\n') {
    stderr_output.push_back('\n');
  }
  stderr_output += message;
}

bool createPipe(int (&fds)[2]) {
  fds[0] = -1;
  fds[1] = -1;
  return pipe(fds) == 0;
}

void closePipes(int (&stdin_pipe)[2], int (&stdout_pipe)[2], int (&stderr_pipe)[2],
                int (&exec_error_pipe)[2]) noexcept {
  closeFd(stdin_pipe[0]);
  closeFd(stdin_pipe[1]);
  closeFd(stdout_pipe[0]);
  closeFd(stdout_pipe[1]);
  closeFd(stderr_pipe[0]);
  closeFd(stderr_pipe[1]);
  closeFd(exec_error_pipe[0]);
  closeFd(exec_error_pipe[1]);
}

void childFailure(int error_fd, int error_code) noexcept {
  const int ignored = static_cast<int>(write(error_fd, &error_code, sizeof(error_code)));
  (void)ignored;
  _exit(127);
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
  const auto found = env.find("PATH");
  if (found != env.end()) {
    return found->second;
  }
  const char* path = std::getenv("PATH");
  return path == nullptr ? "/usr/local/bin:/usr/bin:/bin" : std::string(path);
}

std::vector<std::string> buildProgramCandidates(const ProcessSpec& spec) {
  if (containsSlash(spec.program)) {
    return {spec.program};
  }

  const std::string path_value = envPathValue(spec.env);
  std::stringstream stream(path_value);
  std::string dir;
  std::vector<std::string> candidates;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      dir = ".";
    }
    candidates.push_back(dir + "/" + spec.program);
  }
  return candidates;
}

void execPrepared(const std::vector<std::string>& candidates, char* const argv[],
                  char* const envp[], int error_fd) noexcept {
  int last_error = ENOENT;
  for (const auto& candidate : candidates) {
    execve(candidate.c_str(), argv, envp);
    if (errno != ENOENT && errno != ENOTDIR) {
      last_error = errno;
    }
  }
  childFailure(error_fd, last_error);
}

void writeInputAndClose(int pipe_fd, const std::string& input, ProcessControl& control,
                        const std::atomic_bool& stop) {
  sigset_t blocked_signals;
  sigemptyset(&blocked_signals);
  sigaddset(&blocked_signals, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &blocked_signals, nullptr);

  const int current_flags = fcntl(pipe_fd, F_GETFL, 0);
  if (current_flags >= 0) {
    fcntl(pipe_fd, F_SETFL, current_flags | O_NONBLOCK);
  }

  size_t total_written = 0;
  while (!stop.load(std::memory_order_relaxed) && !control.cancelRequested() &&
         total_written < input.size()) {
    const ssize_t written =
        write(pipe_fd, input.data() + total_written, input.size() - total_written);
    if (written > 0) {
      total_written += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd writable = {pipe_fd, POLLOUT, 0};
      poll(&writable, 1, kPollIntervalMs);
      continue;
    }
    break;
  }
  close(pipe_fd);
}

bool appendCaptured(std::string& sink, const char* buffer, ssize_t bytes_read,
                    std::size_t& captured_bytes, const ProcessSpec& spec,
                    std::string& pending_line) {
  if (bytes_read <= 0) {
    return true;
  }
  const std::size_t remaining = spec.max_output_bytes - captured_bytes;
  const std::size_t accepted =
      std::min<std::size_t>(static_cast<std::size_t>(bytes_read), remaining);
  sink.append(buffer, accepted);
  mirrorWithPrefix(buffer, accepted, spec.stream_output, pending_line);
  captured_bytes += accepted;
  return accepted == static_cast<std::size_t>(bytes_read);
}

bool drainFd(int fd, std::string& sink, std::size_t& captured_bytes,
             const ProcessSpec& spec, std::string& pending_line) {
  char buffer[4096];
  for (;;) {
    const ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
    if (bytes_read > 0) {
      if (!appendCaptured(sink, buffer, bytes_read, captured_bytes, spec, pending_line)) {
        return false;
      }
      continue;
    }
    if (bytes_read < 0 && errno == EINTR) {
      continue;
    }
    return true;
  }
}

void signalProcessGroup(pid_t pid, int signal_number) noexcept {
  if (kill(-pid, signal_number) != 0 && errno != ESRCH) {
    kill(pid, signal_number);
  }
}

} // namespace

PlatformProcessResult executeManagedPosix(const ProcessSpec& spec, ProcessControl& control) {
  PlatformProcessResult completed;
  completed.result.output_streamed_live = spec.stream_output;

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  int exec_error_pipe[2] = {-1, -1};
  if (!createPipe(stdin_pipe) || !createPipe(stdout_pipe) || !createPipe(stderr_pipe) ||
      !createPipe(exec_error_pipe)) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to create process pipes";
    completed.state = ProcessState::SpawnFailed;
    closePipes(stdin_pipe, stdout_pipe, stderr_pipe, exec_error_pipe);
    return completed;
  }
  if (fcntl(exec_error_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to configure process startup handshake";
    completed.state = ProcessState::SpawnFailed;
    closePipes(stdin_pipe, stdout_pipe, stderr_pipe, exec_error_pipe);
    return completed;
  }

  // Prepare every allocating data structure before fork. The child may only call
  // async-signal-safe functions because the parent process is multi-threaded.
  std::vector<std::string> env_storage;
  std::vector<char*> env_ptrs = buildEnvironmentArray(spec.env, env_storage);
  std::vector<std::string> argv_storage;
  std::vector<char*> argv = buildArgv(spec, argv_storage);
  const std::vector<std::string> program_candidates = buildProgramCandidates(spec);

  const pid_t pid = fork();
  if (pid < 0) {
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to fork process";
    completed.state = ProcessState::SpawnFailed;
    closePipes(stdin_pipe, stdout_pipe, stderr_pipe, exec_error_pipe);
    return completed;
  }

  if (pid == 0) {
    closeFd(stdin_pipe[1]);
    closeFd(stdout_pipe[0]);
    closeFd(stderr_pipe[0]);
    closeFd(exec_error_pipe[0]);

    if (setpgid(0, 0) != 0 || dup2(stdin_pipe[0], STDIN_FILENO) < 0 ||
        dup2(stdout_pipe[1], STDOUT_FILENO) < 0 || dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
      childFailure(exec_error_pipe[1], errno);
    }
    closeFd(stdin_pipe[0]);
    closeFd(stdout_pipe[1]);
    closeFd(stderr_pipe[1]);

    if (!spec.working_dir.empty() && chdir(spec.working_dir.c_str()) != 0) {
      childFailure(exec_error_pipe[1], errno);
    }

    execPrepared(program_candidates, argv.data(), env_ptrs.data(), exec_error_pipe[1]);
  }

  closeFd(stdin_pipe[0]);
  closeFd(stdout_pipe[1]);
  closeFd(stderr_pipe[1]);
  closeFd(exec_error_pipe[1]);
  setpgid(pid, pid);

  int exec_error = 0;
  ssize_t error_bytes;
  do {
    error_bytes = read(exec_error_pipe[0], &exec_error, sizeof(exec_error));
  } while (error_bytes < 0 && errno == EINTR);
  closeFd(exec_error_pipe[0]);
  if (error_bytes > 0) {
    int status = 0;
    waitpid(pid, &status, 0);
    completed.result.exit_code = -1;
    completed.result.stderr_output = "Failed to start process (errno " +
                                      std::to_string(exec_error) + ")";
    completed.state = ProcessState::SpawnFailed;
    closeFd(stdin_pipe[1]);
    closeFd(stdout_pipe[0]);
    closeFd(stderr_pipe[0]);
    return completed;
  }

  fcntl(stdout_pipe[0], F_SETFL, fcntl(stdout_pipe[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(stderr_pipe[0], F_SETFL, fcntl(stderr_pipe[0], F_GETFL, 0) | O_NONBLOCK);

  control.publishRunning(static_cast<int>(pid), [pid]() { signalProcessGroup(pid, SIGTERM); });
  std::atomic_bool stop_input{false};
  std::thread input_writer([write_pipe = stdin_pipe[1], &spec, &control, &stop_input]() {
    writeInputAndClose(write_pipe, spec.input, control, stop_input);
  });
  stdin_pipe[1] = -1;

  const auto started_at = std::chrono::steady_clock::now();
  std::optional<std::chrono::steady_clock::time_point> cancelling_since;
  std::string stdout_pending_line;
  std::string stderr_pending_line;
  std::size_t captured_bytes = 0;
  ProcessState terminal_state = ProcessState::Exited;
  int status = 0;

  for (;;) {
    const bool stdout_ok = drainFd(stdout_pipe[0], completed.result.stdout_output, captured_bytes,
                                   spec, stdout_pending_line);
    const bool stderr_ok = drainFd(stderr_pipe[0], completed.result.stderr_output, captured_bytes,
                                   spec, stderr_pending_line);
    if (!stdout_ok || !stderr_ok) {
      terminal_state = ProcessState::OutputLimitExceeded;
      signalProcessGroup(pid, SIGKILL);
    }

    pid_t wait_result = 0;
    control.probeCompletion([&]() {
      wait_result = waitpid(pid, &status, WNOHANG);
      return wait_result == pid || (wait_result < 0 && errno != EINTR);
    });
    if (wait_result == pid) {
      if (terminal_state != ProcessState::OutputLimitExceeded) {
        terminal_state = control.cancelRequested() ? ProcessState::Cancelled
                                                    : ProcessState::Exited;
      }
      break;
    }
    if (wait_result < 0 && errno != EINTR) {
      terminal_state = ProcessState::WaitFailed;
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    if (control.cancelRequested()) {
      if (!cancelling_since) {
        cancelling_since = now;
      } else if (now - *cancelling_since >= std::chrono::milliseconds(spec.cancel_grace_ms)) {
        signalProcessGroup(pid, SIGKILL);
      }
    } else if (now - started_at >= std::chrono::milliseconds(spec.timeout_ms)) {
      terminal_state = ProcessState::TimedOut;
      signalProcessGroup(pid, SIGKILL);
    }

    if (terminal_state == ProcessState::TimedOut ||
        terminal_state == ProcessState::OutputLimitExceeded) {
      control.probeCompletion([&]() {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        return true;
      });
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
  }

  control.releaseNative();
  stop_input.store(true, std::memory_order_relaxed);
  if (input_writer.joinable()) {
    input_writer.join();
  }
  const bool final_stdout_ok = drainFd(stdout_pipe[0], completed.result.stdout_output,
                                       captured_bytes, spec, stdout_pending_line);
  const bool final_stderr_ok = drainFd(stderr_pipe[0], completed.result.stderr_output,
                                       captured_bytes, spec, stderr_pending_line);
  if ((!final_stdout_ok || !final_stderr_ok) && terminal_state == ProcessState::Exited) {
    terminal_state = ProcessState::OutputLimitExceeded;
  }
  flushPendingLine(spec.stream_output, stdout_pending_line);
  flushPendingLine(spec.stream_output, stderr_pending_line);

  if (terminal_state == ProcessState::Exited) {
    if (WIFEXITED(status)) {
      completed.result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      terminal_state = ProcessState::Signaled;
      completed.result.termination_signal = WTERMSIG(status);
      completed.result.exit_code = 128 + completed.result.termination_signal;
    }
  } else {
    completed.result.exit_code = -1;
  }

  if (terminal_state == ProcessState::TimedOut) {
    appendError(completed.result.stderr_output, "Command timed out");
  } else if (terminal_state == ProcessState::Cancelled) {
    appendError(completed.result.stderr_output, "Command cancelled");
  } else if (terminal_state == ProcessState::OutputLimitExceeded) {
    appendError(completed.result.stderr_output,
                "Captured process output exceeded the configured limit");
  } else if (terminal_state == ProcessState::WaitFailed) {
    appendError(completed.result.stderr_output, "Failed while waiting for process");
  }
  completed.state = terminal_state;

  closeFd(stdout_pipe[0]);
  closeFd(stderr_pipe[0]);
  return completed;
}

} // namespace Praktor::Shell::detail
