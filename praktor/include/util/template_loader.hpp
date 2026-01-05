#ifndef __TEMPLATE_LOADER_HPP__
#ifndef __TEMPLATE_LOADER_HPP__
#define __TEMPLATE_LOADER_HPP__

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

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

        std::cerr << "Warning: Template '" << name << "' not found in category '" << category << "'" << std::endl;
        return "";
    }

private:
    static std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        if (!file.is_open()) return "";
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

} // namespace Utils
} // namespace Praktor

#endif // __TEMPLATE_LOADER_HPP__
#endif
