#include "praktor/shell/shell_executor.hpp"

#include "praktor/shell/process_executor.hpp"

#include "shell_executor_internal.hpp"


namespace Praktor::Shell {

namespace {

thread_local ShellExecutor::StreamCallback g_stream_callback;

} // namespace

namespace detail {

void emitShellLogLine(const std::string &line) {
  if (line.empty()) {
    return;
  }

  ShellExecutor::StreamCallback callback = g_stream_callback;
  if (callback) {
    callback(line);
  }
}

void mirrorWithPrefix(const char *buffer, size_t bytes_read, bool stream_output,
                      std::string &pending_line) {
  if (!stream_output || bytes_read == 0) {
    return;
  }

  for (size_t i = 0; i < bytes_read; ++i) {
    const char ch = buffer[i];
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      emitShellLogLine(pending_line);
      pending_line.clear();
      continue;
    }
    pending_line.push_back(ch);
  }
}

void flushPendingLine(bool stream_output, std::string &pending_line) {
  if (!stream_output || pending_line.empty()) {
    return;
  }
  emitShellLogLine(pending_line);
  pending_line.clear();
}

} // namespace detail

void ShellExecutor::setStreamCallback(StreamCallback callback) {
  g_stream_callback = std::move(callback);
}

ShellExecutor::StreamCallback ShellExecutor::getStreamCallback() { return g_stream_callback; }

void ShellExecutor::emitStreamLine(const std::string &line) { detail::emitShellLogLine(line); }

ShellResult ShellExecutor::execute(const std::string &command, const std::string &input,
                                   const std::string &working_dir, int timeout_ms,
                                   const std::map<std::string, std::string> &env,
                                   bool stream_output) {
  return start(command, input, working_dir, timeout_ms, env, stream_output).wait();
}

ManagedProcess ShellExecutor::start(const std::string &command, const std::string &input,
                                    const std::string &working_dir, int timeout_ms,
                                    const std::map<std::string, std::string> &env,
                                    bool stream_output) {
  ProcessSpec spec;
  spec.program = command;
  spec.input = input;
  spec.working_dir = working_dir;
  spec.timeout_ms = timeout_ms;
  spec.env = env;
  spec.stream_output = stream_output;
  return ProcessExecutor::startShell(spec);
}

} // namespace Praktor::Shell
