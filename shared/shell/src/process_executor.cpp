#include "praktor/shell/process_executor.hpp"

#include <stdexcept>

namespace Praktor::Shell {

ShellResult ProcessExecutor::execute(const ProcessSpec& spec) {
  if (spec.program.empty()) {
    throw std::runtime_error("Process program cannot be empty");
  }

#if defined(_WIN32)
  return executeWindows(spec);
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
  return executeBsd(spec);
#elif defined(__linux__)
  return executeLinux(spec);
#else
  #error "Unsupported platform"
#endif
}

} // namespace Praktor::Shell
