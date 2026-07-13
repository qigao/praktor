#include "shell_executor_posix.hpp"

#include <map>
#include <signal.h>
#include <string_view>

extern char** environ;

namespace Praktor::Shell::detail {

std::vector<char*> buildEnvironmentArray(const std::map<std::string, std::string>& env,
                                         std::vector<std::string>& storage) {
  std::vector<char*> env_ptrs;

  if (env.empty()) {
    for (char** e = ::environ; *e != nullptr; e++) {
      env_ptrs.push_back(*e);
    }
  } else {
    std::map<std::string, std::string> merged_env;

    for (char** e = ::environ; *e != nullptr; ++e) {
      const std::string_view entry(*e);
      const size_t equals = entry.find('=');
      if (equals == std::string_view::npos || equals == 0) {
        continue;
      }
      merged_env[std::string(entry.substr(0, equals))] = std::string(entry.substr(equals + 1));
    }

    for (const auto& [key, value] : env) {
      merged_env[key] = value;
    }

    storage.reserve(merged_env.size());
    for (const auto& [key, value] : merged_env) {
      storage.push_back(key + "=" + value);
    }
    for (auto& entry : storage) {
      env_ptrs.push_back(&entry[0]);
    }
  }

  env_ptrs.push_back(nullptr);
  return env_ptrs;
}

bool killPosixProcess(int pid) {
  if (pid <= 0) {
    return false;
  }
  const pid_t process_group = -static_cast<pid_t>(pid);
  if (kill(process_group, SIGTERM) == 0) {
    return true;
  }
  return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

} // namespace Praktor::Shell::detail
