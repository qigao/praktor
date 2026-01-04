#include "util/prakter_init.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Prakter {
namespace Utils {

bool PrakterInit::initialize(const std::string& template_name) {
    std::string filename = "prakter.yml";
    if (std::filesystem::exists(filename)) {
        std::cerr << "Error: " << filename << " already exists." << std::endl;
        return false;
    }

    // Try to find the templates directory
    // In development, it's in the source tree: prakter/templates
    // In installation, it might be near the binary
    std::filesystem::path exePath = std::filesystem::current_path(); // Simplification
    std::vector<std::filesystem::path> searchPaths = {
        exePath / "templates",
        exePath / "prakter" / "templates",
        exePath.parent_path() / "prakter" / "templates",
        "C:/projects/cpp/weave/prakter/templates" // Fallback for dev environment
    };

    std::filesystem::path templatePath;
    for (const auto& path : searchPaths) {
        auto p = path / (template_name + ".yml");
        if (std::filesystem::exists(p)) {
            templatePath = p;
            break;
        }
    }

    if (templatePath.empty()) {
        std::cerr << "Error: Template '" << template_name << "' not found." << std::endl;
        std::cerr << "Available templates: basic, cpp-library" << std::endl;
        return false;
    }

    try {
        std::filesystem::copy_file(templatePath, filename);
        std::cout << "Successfully initialized " << filename << " using '" << template_name << "' template." << std::endl;
        std::cout << "Source: " << templatePath.string() << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error copying template: " << e.what() << std::endl;
        return false;
    }
}

} // namespace Utils
} // namespace Prakter
