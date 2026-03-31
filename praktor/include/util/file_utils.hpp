#ifndef __FILE_UTILS_HPP__
#define __FILE_UTILS_HPP__

#include "turbo_fs.h"

#include <array>
#include <stdexcept>
#include <string>

namespace FileUtils {

class FileException : public std::runtime_error {
public:
  explicit FileException(std::string const& message) : std::runtime_error(message) {}
};

inline std::string turboFsErrorToString(int error_code) {
  return "turbo_fs error: " + std::to_string(error_code);
}

inline bool fileExists(std::string const& filePath) {
  turbo_fs_stat_t stat {};
  return turbo_fs_stat(filePath.c_str(), &stat) == 0;
}

inline std::string readFile(std::string const& filePath) {
  turbo_fs_buf_t buf {};
  const int rc = turbo_fs_read_file(filePath.c_str(), &buf);
  if (rc != 0) {
    throw FileException("Failed to read file: " + filePath + " - " + turboFsErrorToString(rc));
  }

  std::string content;
  if (buf.base != nullptr && buf.len > 0) {
    content.assign(buf.base, buf.len);
  }
  turbo_fs_buf_free(&buf);
  return content;
}

inline bool writeFile(std::string const& filePath, std::string const& content) {
  auto buffer = turbo_fs_buf_init(const_cast<char*>(content.data()), content.size());
  return turbo_fs_write_file(filePath.c_str(), &buffer) == 0;
}

inline bool createDirectory(std::string const& dirPath) {
  turbo_fs_stat_t stat {};
  if (turbo_fs_stat(dirPath.c_str(), &stat) == 0) {
    return stat.is_directory;
  }
  return turbo_fs_mkdir(dirPath.c_str(), 0755) == 0;
}

inline std::string getParentDirectory(std::string const& filePath) {
  std::array<char, TURBO_FS_MAX_PATH> buffer {};
  if (turbo_fs_path_dirname(filePath.c_str(), buffer.data(), buffer.size()) != 0) {
    return {};
  }
  return std::string(buffer.data());
}

inline std::string getFilename(std::string const& filePath) {
  std::array<char, TURBO_FS_MAX_PATH> buffer {};
  if (turbo_fs_path_basename(filePath.c_str(), buffer.data(), buffer.size()) != 0) {
    return {};
  }
  return std::string(buffer.data());
}

inline std::string getFileExtension(std::string const& filePath) {
  const std::string filename = getFilename(filePath);
  const size_t last_dot_pos = filename.find_last_of('.');
  if (last_dot_pos == std::string::npos || last_dot_pos == 0) {
    return {};
  }
  return filename.substr(last_dot_pos);
}

} // namespace FileUtils

#endif // __FILE_UTILS_HPP__
