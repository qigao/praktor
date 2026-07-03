#include "praktor/shell/shell_executor.hpp"

#include "shell_executor_posix.hpp"

namespace Praktor::Shell {

ShellResult ShellExecutor::executeBsd(const std::string& command, const std::string& input,
                                      const std::string& working_dir, int timeout_ms,
                                      const std::map<std::string, std::string>& env,
                                      bool stream_output) {
  return detail::executePosixCommand(command, input, working_dir, timeout_ms, env, stream_output);
}

bool ShellExecutor::killBsdProcess(int pid) {
  return detail::killPosixProcess(pid);
}

} // namespace Praktor::Shell
