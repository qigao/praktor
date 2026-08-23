#include "util/praktor_init.hpp"
#include "util/logging.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Praktor {
namespace Utils {

namespace {

std::filesystem::path currentExecutableDirectory() {
#ifdef _WIN32
  std::vector<char> buffer(MAX_PATH);
  DWORD length = 0;
  while ((length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()))) >= buffer.size()) {
    buffer.resize(buffer.size() * 2);
  }
  if (length == 0) {
    return {};
  }
  return std::filesystem::path(std::string(buffer.data(), length)).parent_path();
#elif defined(__linux__)
  std::vector<char> buffer(1024);
  while (true) {
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length < 0) {
      return {};
    }
    if (static_cast<size_t>(length) < buffer.size()) {
      return std::filesystem::path(std::string(buffer.data(), static_cast<size_t>(length))).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
#else
  return {};
#endif
}

std::vector<std::string> availableTemplates(const std::vector<std::filesystem::path>& searchPaths) {
  std::vector<std::string> names;
  for (const auto& searchPath : searchPaths) {
    if (!std::filesystem::exists(searchPath) || !std::filesystem::is_directory(searchPath)) {
      continue;
    }
    for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".yml") {
        continue;
      }
      names.push_back(entry.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

std::string joinTemplateNames(const std::vector<std::string>& names) {
  if (names.empty()) {
    return "<none found>";
  }
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i != 0) {
      joined += ", ";
    }
    joined += names[i];
  }
  return joined;
}

} // namespace

bool PraktorInit::initialize(const std::string &template_name) {
  std::string filename = "praktor.yml";
  if (std::filesystem::exists(filename)) {
    logef("Error: {} already exists.", filename);
    return false;
  }

  const std::filesystem::path exeDir = currentExecutableDirectory();
  std::vector<std::filesystem::path> searchPaths = {
      exeDir / "templates",
      exeDir.parent_path() / "praktor" / "templates",
      std::filesystem::current_path() / "templates",
  };

  std::filesystem::path templatePath;
  for (const auto &path : searchPaths) {
    auto p = path / (template_name + ".yml");
    if (std::filesystem::exists(p)) {
      templatePath = p;
      break;
    }
  }

  if (templatePath.empty()) {
    logef("Error: Template '{}' not found.", template_name);
    logef("Available templates: {}", joinTemplateNames(availableTemplates(searchPaths)));
    return false;
  }

  try {
    std::filesystem::copy_file(templatePath, filename);
    logif("Successfully initialized {} using '{}' template.", filename, template_name);
    logif("Source: {}", templatePath.string());
    return true;
  } catch (const std::exception &e) {
    logef("Error copying template: {}", e.what());
    return false;
  }
}

} // namespace Utils
} // namespace Praktor
