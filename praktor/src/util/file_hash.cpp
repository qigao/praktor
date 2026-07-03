#include "util/file_hash.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace Praktor::Util {

namespace {

uint64_t fnv1a64(const char* data, size_t size) {
    constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;

    uint64_t hash = kOffsetBasis;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= kPrime;
    }
    return hash;
}

} // namespace

std::string computeFileHash(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Cannot hash missing file: " + path.string());
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for hashing: " + path.string());
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    auto h = fnv1a64(bytes.data(), bytes.size());
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

std::string computeStringHash(const std::string& str) {
    auto h = fnv1a64(str.data(), str.size());
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

} // namespace Praktor::Util
