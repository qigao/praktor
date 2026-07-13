#pragma once

#include "praktor/shell/shell_executor.hpp"

#include <map>
#include <string>
#include <vector>

namespace Praktor::Shell::detail {

std::vector<char*> buildEnvironmentArray(const std::map<std::string, std::string>& env,
                                         std::vector<std::string>& storage);

bool killPosixProcess(int pid);

} // namespace Praktor::Shell::detail
