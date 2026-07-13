#include "praktor/shell/shell_executor.hpp"

#include <windows.h>

namespace Praktor::Shell {

bool ShellExecutor::killWindowsProcess(int pid) {
  if (pid <= 0) {
    return false;
  }

  HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (process == NULL) {
    return false;
  }
  const BOOL terminated = TerminateProcess(process, 1);
  CloseHandle(process);
  return terminated != 0;
}

} // namespace Praktor::Shell
