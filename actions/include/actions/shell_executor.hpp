#pragma once

#include "actions/async_executor.hpp"
#include "praktor/shell/shell_executor.hpp"

namespace actions {

using ShellResult = Praktor::Shell::ShellResult;

class AsyncExecutor;

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
    auto process = std::make_shared<Praktor::Shell::ManagedProcess>(
        Praktor::Shell::ShellExecutor::start(command, input, working_dir, timeout_ms, env,
                                             stream_output));
    auto cancellation = getCancellationContext();
    if (cancellation) {
      cancellation->setHandler([process]() { process->cancel(); });
      if (cancellation->isCancellationRequested()) {
        process->cancel();
      }
    }
    try {
      auto result = process->wait();
      if (cancellation) {
        cancellation->clearHandler();
      }
      return result;
    } catch (...) {
      if (cancellation) {
        cancellation->clearHandler();
      }
      throw;
    }
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

private:
  friend class AsyncExecutor;

  static StreamCallback getStreamCallback() {
    return Praktor::Shell::ShellExecutor::getStreamCallback();
  }

  static void setCancellationContext(std::shared_ptr<CancellationContext> cancellation);
  static std::shared_ptr<CancellationContext> getCancellationContext();
};

inline bool ShellExecutor::killProcess(int pid) {
  return Praktor::Shell::ShellExecutor::killProcess(pid);
}

} // namespace actions
