#pragma once

#include <functional>
#include <string>
#include <vector>
#include <map>

namespace btdsl {

// Result of shell command execution
struct ShellResult {
  int exit_code = 0;
  std::string stdout_output;
  std::string stderr_output;
  int pid = -1;  // Process ID (if available)
  bool output_streamed_live = false;

  bool success() const { return exit_code == 0; }
};

// Shell command executor - cross-platform
class ShellExecutor {
public:
  using StreamCallback = std::function<void(const std::string&)>;

  // Execute a command synchronously
  static ShellResult execute(
    const std::string& command,
    const std::string& input = "",
    const std::string& working_dir = "",
    int timeout_ms = 30000,
    const std::map<std::string, std::string>& env = {},
    bool stream_output = true
  );

  // Execute a command asynchronously (returns immediately)
  static void executeAsync(
    const std::string& command,
    const std::string& input = "",
    const std::string& working_dir = "",
    const std::map<std::string, std::string>& env = {},
    bool stream_output = true
  );

  // Kill a process by PID (cross-platform)
  static bool killProcess(int pid);

  static void setStreamCallback(StreamCallback callback);
  static void emitStreamLine(const std::string& line);

private:
  // Platform-specific implementations
#ifdef _WIN32
  static ShellResult executeWindows(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    int timeout_ms,
    const std::map<std::string, std::string>& env,
    bool stream_output
  );
#else
  static ShellResult executeUnix(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    int timeout_ms,
    const std::map<std::string, std::string>& env,
    bool stream_output
  );
#endif
};

} // namespace btdsl
