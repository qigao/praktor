#pragma once

#include "yml/task.hpp"

#include <string>

namespace Praktor::Execution::Internal {

std::string computeTaskActionHash(const Task& task);

}  // namespace Praktor::Execution::Internal
