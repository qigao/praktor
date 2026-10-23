#include "praktor/shell/shell_executor.hpp"

#include "praktor/shell/process_executor.hpp"



namespace Praktor::Shell {

namespace {

thread_local ShellExecutor::StreamCallback g_stream_callback;

} // namespace

void ShellExecutor::setStreamCallback(StreamCallback callback) {
  g_stream_callback = std::move(callback);
}

ShellExecutor::StreamCallback ShellExecutor::getStreamCallback() { return g_stream_callback; }

void ShellExecutor::emitStreamLine(const std::string &line) {
  auto callback = g_stream_callback;
  if (callback && !line.empty()) callback(line);
}

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
