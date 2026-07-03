#pragma once

#include "praktor/shell/shell_executor.hpp"

#include <map>
#include <string>
#include <vector>

namespace Praktor::Shell::detail {

std::vector<char*> buildEnvironmentArray(const std::map<std::string, std::string>& env,
                                         std::vector<std::string>& storage);

ShellResult executePosixCommand(const std::string& command, const std::string& input,
                                const std::string& working_dir, int timeout_ms,
                                const std::map<std::string, std::string>& env,
                                bool stream_output);

bool killPosixProcess(int pid);

} // namespace Praktor::Shell::detail
