#include "praktor/shell/shell_executor.hpp"

#include "shell_executor_posix.hpp"

namespace Praktor::Shell {

bool ShellExecutor::killLinuxProcess(int pid) {
  return detail::killPosixProcess(pid);
}

} // namespace Praktor::Shell
