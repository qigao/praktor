#pragma once

#include "logging.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>

namespace Praktor {
namespace Utils {

class TemplateLoader {
public:
    static std::string loadTemplate(const std::string& category, const std::string& name) {
        std::filesystem::path exePath = std::filesystem::current_path();
        std::vector<std::filesystem::path> searchPaths = {
            exePath / "templates" / category
        };

        for (const auto& path : searchPaths) {
            auto p = path / name;
            if (std::filesystem::exists(p)) {
                return readFile(p);
            }
        }

        throw std::runtime_error("Template '" + name + "' not found in category '" + category + "'");
    }

private:
    static std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open template file: " + path.string());
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

} // namespace Utils
} // namespace Praktor
