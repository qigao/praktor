#pragma once

#include "praktor/shell/shell_executor.hpp"

namespace actions {

using ShellResult = Praktor::Shell::ShellResult;

class ShellExecutor {
public:
  using StreamCallback = Praktor::Shell::ShellExecutor::StreamCallback;

  static ShellResult execute(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      int timeout_ms = 30000,
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true) {
    return Praktor::Shell::ShellExecutor::execute(command, input, working_dir, timeout_ms, env,
                                                  stream_output);
  }

  static void executeAsync(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true) {
    Praktor::Shell::ShellExecutor::executeAsync(command, input, working_dir, env, stream_output);
  }

  static bool killProcess(int pid);

  static void setStreamCallback(StreamCallback callback) {
    Praktor::Shell::ShellExecutor::setStreamCallback(callback);
  }

  static void emitStreamLine(const std::string& line) {
    Praktor::Shell::ShellExecutor::emitStreamLine(line);
  }
};

inline bool ShellExecutor::killProcess(int pid) {
  return Praktor::Shell::ShellExecutor::killProcess(pid);
}

} // namespace actions
