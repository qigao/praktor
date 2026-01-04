#ifndef __FILE_HASH_HPP__
#define __FILE_HASH_HPP__

#include <string>
#include <vector>
#include <filesystem>

namespace Prakter::Util {

struct FileState {
    std::string path;
    uint64_t last_modified;
    uint64_t size;
    std::string hash;
};

std::string computeFileHash(const std::filesystem::path& path);
std::string computeStringHash(const std::string& str);

} // namespace Prakter::Util

#endif // __FILE_HASH_HPP__
