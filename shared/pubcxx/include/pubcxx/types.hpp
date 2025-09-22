#ifndef __TYPES_H__
#define __TYPES_H__

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <unordered_map>

using schar = signed char;
using uchar = unsigned char;
using ushort = unsigned short;
using uint = unsigned int;
using ulong = unsigned long;
using ullong = unsigned long long;
using llong = long long;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using cstr = char const*;

namespace fs = std::filesystem;
using ofs = std::ofstream;
using ifstream = std::ifstream;

template <typename T, typename U>
using umap = std::unordered_map<T, U>;

template <typename T>
using uptr = std::unique_ptr<T>;

template <typename T>
using sptr = std::shared_ptr<T>;

template <typename T>
inline std::shared_ptr<T> make_array(int len) {
    return std::shared_ptr<T>(new T[len], std::default_delete<T[]>());
}

template <typename T, typename... Args>
inline std::unique_ptr<T> make_uptr(Args&... args) {
    return std::make_unique<T>(args...);
}

template <typename T, typename... Args>
inline std::shared_ptr<T> make_sptr(Args&... args) {
    return std::make_shared<T>(args...);
}

#endif   // __TYPES_H__
