#ifndef __FILE_UTILS_HPP__
#define __FILE_UTILS_HPP__

#include <stdexcept>
#include <string>
#include <uv.h>   // For libuv functions
#include <vector> // For uv_buf_t


#include <sys/stat.h> // For S_ISDIR and _S_IFDIR

namespace FileUtils {

class FileException : public std::runtime_error {
public:
  FileException(std::string const &message) : std::runtime_error(message) {}
};

// Helper to convert libuv error codes to string
inline std::string uvErrorToString(int error_code) { return std::string(uv_strerror(error_code)); }

/**
 * @brief Check if a file exists using libuv
 * @param filePath Path to the file
 * @return True if the file exists, false otherwise
 */
inline bool fileExists(std::string const &filePath) {
  uv_fs_t req;
  int r = uv_fs_stat(uv_default_loop(), &req, filePath.c_str(), NULL);
  uv_fs_req_cleanup(&req);
  return r == 0;
}

/**
 * @brief Read the contents of a file using libuv
 * @param filePath Path to the file
 * @return Contents of the file as a string
 * @throws FileException if the file cannot be read
 */
inline std::string readFile(std::string const &filePath) {
  if (!fileExists(filePath)) {
    throw FileException("File not found: " + filePath);
  }

  uv_fs_t open_req;
  int r = uv_fs_open(uv_default_loop(), &open_req, filePath.c_str(), O_RDONLY, 0, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&open_req);
    throw FileException("Failed to open file: " + filePath + " - " + uvErrorToString(r));
  }
  uv_file fd = r;
  uv_fs_req_cleanup(&open_req);

  // Get file size
  uv_fs_t stat_req;
  r = uv_fs_fstat(uv_default_loop(), &stat_req, fd, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&stat_req);
    uv_fs_t close_req;
    uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);
    throw FileException("Failed to stat file: " + filePath + " - " + uvErrorToString(r));
  }
  size_t file_size = stat_req.statbuf.st_size;
  uv_fs_req_cleanup(&stat_req);

  std::string content;
  content.resize(file_size);
  uv_buf_t buf = uv_buf_init(const_cast<char *>(content.data()), file_size);

  uv_fs_t read_req;
  r = uv_fs_read(uv_default_loop(), &read_req, fd, &buf, 1, 0, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&read_req);
    uv_fs_t close_req;
    uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);
    throw FileException("Failed to read file: " + filePath + " - " + uvErrorToString(r));
  }
  uv_fs_req_cleanup(&read_req);

  uv_fs_t close_req;
  r = uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&close_req);
    throw FileException("Failed to close file: " + filePath + " - " + uvErrorToString(r));
  }
  uv_fs_req_cleanup(&close_req);

  return content;
}

/**
 * @brief Write a string to a file using libuv
 * @param filePath Path to the file
 * @param content Content to write
 * @return True if the file was written successfully, false otherwise
 */
inline bool writeFile(std::string const &filePath, std::string const &content) {
  uv_fs_t open_req;
  int r = uv_fs_open(uv_default_loop(), &open_req, filePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                     0644, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&open_req);
    return false;
  }
  uv_file fd = r;
  uv_fs_req_cleanup(&open_req);

  uv_buf_t buf = uv_buf_init(const_cast<char *>(content.data()), content.size());

  uv_fs_t write_req;
  r = uv_fs_write(uv_default_loop(), &write_req, fd, &buf, 1, 0, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&write_req);
    uv_fs_t close_req;
    uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);
    return false;
  }
  uv_fs_req_cleanup(&write_req);

  uv_fs_t close_req;
  r = uv_fs_close(uv_default_loop(), &close_req, fd, NULL);
  if (r < 0) {
    uv_fs_req_cleanup(&close_req);
    return false;
  }
  uv_fs_req_cleanup(&close_req);

  return true;
}

/**
 * @brief Create a directory if it doesn't exist using libuv
 * @param dirPath Path to the directory
 * @return True if the directory was created or already exists, false otherwise
 */
inline bool createDirectory(std::string const &dirPath) {
  // Check if it exists and is a directory
  uv_fs_t stat_req;
  int r = uv_fs_stat(uv_default_loop(), &stat_req, dirPath.c_str(), NULL);
  if (r == 0) {
    uv_fs_req_cleanup(&stat_req);
#ifdef _WIN32
    return (stat_req.statbuf.st_mode & _S_IFDIR) == _S_IFDIR; // Already exists and is a directory
#else
    return (stat_req.statbuf.st_mode & S_IFMT) == S_IFDIR; // Already exists and is a directory
#endif
  }
  uv_fs_req_cleanup(&stat_req);

  // If it doesn't exist or is not a directory, try to create it
  uv_fs_t mkdir_req;
  r = uv_fs_mkdir(uv_default_loop(), &mkdir_req, dirPath.c_str(), 0755, NULL);
  uv_fs_req_cleanup(&mkdir_req);

  // r == 0 means success, UV_EEXIST means it already exists (which is fine)
  return r == 0 || r == UV_EEXIST;
}

/**
 * @brief Get the parent directory of a file
 * @param filePath Path to the file
 * @return Parent directory path
 */
inline std::string getParentDirectory(std::string const &filePath) {
  // libuv does not provide direct path manipulation functions like std::filesystem.
  // This would typically be handled by a dedicated path library or string manipulation.
  // For now, keeping a basic string manipulation approach.
  size_t last_slash_pos = filePath.find_last_of("/\\");
  if (last_slash_pos == std::string::npos) {
    return ""; // No directory component
  }
  return filePath.substr(0, last_slash_pos);
}

/**
 * @brief Get the filename from a path
 * @param filePath Path to the file
 * @return Filename
 */
inline std::string getFilename(std::string const &filePath) {
  size_t last_slash_pos = filePath.find_last_of("/\\");
  if (last_slash_pos == std::string::npos) {
    return filePath; // No directory component, so the whole path is the filename
  }
  return filePath.substr(last_slash_pos + 1);
}

/**
 * @brief Get the file extension
 * @param filePath Path to the file
 * @return File extension
 */
inline std::string getFileExtension(std::string const &filePath) {
  std::string filename = getFilename(filePath);
  size_t last_dot_pos = filename.find_last_of('.');
  if (last_dot_pos == std::string::npos || last_dot_pos == 0) {
    return ""; // No extension or filename starts with a dot
  }
  return filename.substr(last_dot_pos);
}

} // namespace FileUtils

#endif // __FILE_UTILS_HPP__
