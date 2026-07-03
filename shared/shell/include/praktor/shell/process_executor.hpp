#pragma once

#include "praktor/shell/shell_executor.hpp"

#include <map>
#include <string>
#include <vector>

namespace Praktor::Shell {

struct ProcessSpec {
  std::string program;
  std::vector<std::string> args;
  std::string input;
  std::string working_dir;
  int timeout_ms = 30000;
  std::map<std::string, std::string> env;
  bool stream_output = true;
};

class ProcessExecutor {
public:
  static ShellResult execute(const ProcessSpec& spec);

private:
  static ShellResult executeWindows(const ProcessSpec& spec);
  static ShellResult executeLinux(const ProcessSpec& spec);
  static ShellResult executeBsd(const ProcessSpec& spec);
};

} // namespace Praktor::Shell
