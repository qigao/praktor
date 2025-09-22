#pragma once
#include "types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
namespace fs = std::filesystem;

inline int64_t get_file_size(std::string const& filename) {
    std::ifstream infile(filename.c_str(), std::ios_base::in | std::ios_base::binary);
    if (!infile) {
        // throw std::runtime_error("failed to open " + filename + " for
        // read!");
        return 0;
    }
    auto pos0 = infile.tellg();
    infile.seekg(0, std::ios_base::end);
    auto pos = infile.tellg();
    std::streamoff size = pos - pos0;
    infile.close();

    return size;
}

inline void rename(std::string const& oname, std::string const& nname) {
    if (std::rename(oname.c_str(), nname.c_str()) != 0) {
        throw std::runtime_error("failed to rename " + oname + " => " + nname);
    }
}

inline void append(std::string const& filename, char const* data, size_t size) {
    ofs outfile(filename.c_str(), std::ios_base::out | std::ios_base::app);
    if (!outfile) { throw std::runtime_error("failed to open " + filename + " for write"); }
    std::cout << "fs::append(" << filename << ", " << static_cast<void const*>(data) << ", " << size << ")"
              << std::endl;
    outfile.write(data, static_cast<long>(size));
}

inline void fs_write(std::string const& filename, char const* data, size_t size) {
    ofs outfile(filename.c_str(), std::ios_base::out | std::ios_base::trunc);
    if (!outfile) { throw std::runtime_error("failed to open " + filename + " for write"); }
    outfile.write(data, static_cast<long>(size));
    outfile.close();
}

inline bool breakup_url(std::string const& url, std::string& scheme_and_host, std::string& path) {
    std::string::size_type off = 0;
    std::string scheme_notation = "://";
    std::string::size_type pos = url.find(scheme_notation, off);
    if (pos == std::string::npos) { return false; }
    off = pos + scheme_notation.length();
    pos = url.find('/', off);
    if (pos == std::string::npos) { pos = url.length(); }
    scheme_and_host = url.substr(0, pos);
    path = url.substr(pos);
    if (path.empty()) { path = "/"; }
    return true;
}

inline std::string create_random_str(int n) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis('a',
                                        'z');   // Generate lowercase letters
    std::string random_chars(n, '\0');
    std::generate_n(random_chars.begin(), random_chars.size(), [&dis, &gen]() { return dis(gen); });
    return random_chars;
}

inline bool createRandomDir(fs::path& temp_dir) {
    auto random_chars = create_random_str(10);

    fs::path random_dir = temp_dir / random_chars;
    while (true) {
        if (fs::exists(random_dir)) {
            random_chars = create_random_str(10);
            random_dir = temp_dir / random_chars;
        }
        break;
    }
    if (fs::create_directory(random_dir)) {
        temp_dir = random_dir;
        return true;   // Successfully created the directory
    } else {
        // Handle potential error during directory creation
        std::cerr << "Error creating directory: " << random_dir << std::endl;
        return false;
    }
}

inline bool caseInsensitiveEqual(char a, char b) { return std::tolower(a) == std::tolower(b); }

inline void ensureDirectoryExists(std::string const& path) {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
            std::cout << "Directory created: " << path << std::endl;
        } else {
            std::cout << "Directory already exists: " << path << std::endl;
        }
    } catch (fs::filesystem_error const& e) { std::cerr << "Filesystem error: " << e.what() << std::endl; }
}

inline void ensureDirectoryExists(char const* path) {
    try {
        fs::path dirPath(path);
        if (!fs::exists(dirPath)) {
            fs::create_directories(dirPath);
            std::cout << "Directory created: " << path << std::endl;
        } else {
            std::cout << "Directory already exists: " << path << std::endl;
        }
    } catch (fs::filesystem_error const& e) { std::cerr << "Filesystem error: " << e.what() << std::endl; }
}

static char const hex_chars[] = "0123456789ABCDEF";

inline std::string to_hex(unsigned char const* input, size_t length) {
    std::string result;
    result.reserve(length * 2);

    for (size_t i = 0; i < length; ++i) {
        result.push_back(hex_chars[input[i] >> 4]);
        result.push_back(hex_chars[input[i] & 0x0F]);
    }

    return result;
}
