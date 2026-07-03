#pragma once

#include <functional>
#include <map>
#include <string>

namespace Praktor::Shell {

struct ShellResult {
  int exit_code = 0;
  std::string stdout_output;
  std::string stderr_output;
  int pid = -1;
  bool output_streamed_live = false;

  bool success() const { return exit_code == 0; }
};

class ShellExecutor {
public:
  using StreamCallback = std::function<void(const std::string&)>;

  static ShellResult execute(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      int timeout_ms = 30000,
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true);

  static void executeAsync(
      const std::string& command,
      const std::string& input = "",
      const std::string& working_dir = "",
      const std::map<std::string, std::string>& env = {},
      bool stream_output = true);

  static bool killProcess(int pid);

  static void setStreamCallback(StreamCallback callback);
  static void emitStreamLine(const std::string& line);

private:
  static ShellResult executeWindows(
      const std::string& command,
      const std::string& input,
      const std::string& working_dir,
      int timeout_ms,
      const std::map<std::string, std::string>& env,
      bool stream_output);

  static ShellResult executeLinux(
      const std::string& command,
      const std::string& input,
      const std::string& working_dir,
      int timeout_ms,
      const std::map<std::string, std::string>& env,
      bool stream_output);

  static ShellResult executeBsd(
      const std::string& command,
      const std::string& input,
      const std::string& working_dir,
      int timeout_ms,
      const std::map<std::string, std::string>& env,
      bool stream_output);

  static bool killWindowsProcess(int pid);
  static bool killLinuxProcess(int pid);
  static bool killBsdProcess(int pid);
};

} // namespace Praktor::Shell
