#pragma once

#include <filesystem>
#include <string>

namespace Praktor::Execution::Internal {

// Hashes the file using bounded storage. Returns empty and sets error on failure.
std::string sha256File(const std::filesystem::path& path, std::string* error);

}  // namespace Praktor::Execution::Internal
