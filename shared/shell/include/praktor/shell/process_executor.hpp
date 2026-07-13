#pragma once

#include "praktor/shell/shell_executor.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Praktor::Shell {

inline constexpr std::size_t kDefaultMaxCapturedOutputBytes = 16 * 1024 * 1024;

struct ProcessSpec {
  std::string program;
  std::vector<std::string> args;
  std::string input;
  std::string working_dir;
  int timeout_ms = 30000;
  int cancel_grace_ms = 1000;
  std::size_t max_output_bytes = kDefaultMaxCapturedOutputBytes;
  std::map<std::string, std::string> env;
  bool stream_output = true;
};

class ProcessExecutor {
public:
  /**
   * Starts a program without shell parsing.
   * @param spec Program, arguments, environment, timeout, cancellation, and capture limits.
   * @return Move-only owner of the process session and its eventual result.
   * @throws std::runtime_error if required configuration is invalid.
   */
  static ManagedProcess start(const ProcessSpec& spec);
  /** Compatibility facade equivalent to start(spec).wait(). */
  static ShellResult execute(const ProcessSpec& spec);

private:
  static ManagedProcess startShell(const ProcessSpec& spec, std::string windows_command_line);

  friend class ShellExecutor;
};

} // namespace Praktor::Shell
