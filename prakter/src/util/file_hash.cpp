#include "util/file_hash.hpp"
#include <fstream>
#include <xxhash.h> // Assuming xxHash is available or I can use a simpler one

namespace Prakter::Util {

namespace {
// Simple fall-back hash if xxHash isn't linked
uint64_t simple_hash(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return 0;
    auto ftime = std::filesystem::last_write_time(path);
    auto size = std::filesystem::file_size(path);
    return static_cast<uint64_t>(ftime.time_since_epoch().count()) ^ (size << 32);
}
}

std::string computeFileHash(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return "";
    
    // For now, use mtime + size as a "hash" for speed
    auto h = simple_hash(path);
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

std::string computeStringHash(const std::string& str) {
    // Basic string hash
    std::hash<std::string> hasher;
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hasher(str));
    return std::string(buf);
}

} // namespace Prakter::Util
