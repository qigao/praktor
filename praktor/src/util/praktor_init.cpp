#include "util/praktor_init.hpp"
#include "util/logging.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Praktor {
namespace Utils {

bool PraktorInit::initialize(const std::string &template_name) {
  std::string filename = "praktor.yml";
  if (std::filesystem::exists(filename)) {
    loge("Error: {} already exists.", filename);
    return false;
  }

  // Try to find the templates directory
  // In development, it's in the source tree: praktor/templates
  // In installation, it might be near the binary
  std::filesystem::path exePath = std::filesystem::current_path(); // Simplification
  std::vector<std::filesystem::path> searchPaths = {
      exePath / "templates",
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
    loge("Error: Template '{}' not found.", template_name);
    loge("Available templates: basic, cpp-library");
    return false;
  }

  try {
    std::filesystem::copy_file(templatePath, filename);
    logi("Successfully initialized {} using '{}' template.", filename, template_name);
    logi("Source: {}", templatePath.string());
    return true;
  } catch (const std::exception &e) {
    loge("Error copying template: {}", e.what());
    return false;
  }
}

} // namespace Utils
} // namespace Praktor
