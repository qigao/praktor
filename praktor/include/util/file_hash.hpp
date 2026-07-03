#pragma once

#include <filesystem>
#include <string>
#include <vector>


namespace Praktor::Util {

struct FileState {
  std::string path;
  uint64_t last_modified;
  uint64_t size;
  std::string hash;
};

std::string computeFileHash(const std::filesystem::path &path);
std::string computeStringHash(const std::string &str);

} // namespace Praktor::Util
