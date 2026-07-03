#pragma once

#include <filesystem>
#include <string>

namespace Praktor::util {

/**
 * @brief Resolve a child path relative to the workflow source file that declared it.
 * @param source_path Path to the declaring workflow/task source file.
 * @param child Child path to resolve.
 * @return A normalized absolute-or-relative path rooted at the source file directory.
 */
inline std::filesystem::path resolveRelativePath(const std::string& source_path,
                                                 const std::string& child) {
  std::filesystem::path relative(child);
  if (relative.is_absolute()) {
    return relative.lexically_normal();
  }

  const std::filesystem::path base =
      source_path.empty() ? std::filesystem::current_path()
                          : std::filesystem::path(source_path).parent_path();
  return (base / relative).lexically_normal();
}

} // namespace Praktor::util
