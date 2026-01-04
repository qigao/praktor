#ifndef __PRAKTER_INIT_HPP__
#define __PRAKTER_INIT_HPP__

#include <string>

namespace Prakter {
namespace Utils {

class PrakterInit {
public:
    /**
     * @brief Initialize a new Prakter project with a template.
     * @param template_name The name of the template to use (e.g., "basic", "cpp-library").
     * @return True if initialization was successful, false otherwise.
     */
    static bool initialize(const std::string& template_name);
};

} // namespace Utils
} // namespace Prakter

#endif // __PRAKTER_INIT_HPP__
