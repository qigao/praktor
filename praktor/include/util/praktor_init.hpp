#ifndef __PRAKTOR_INIT_HPP__
#define __PRAKTOR_INIT_HPP__

#include <string>

namespace Praktor {
namespace Utils {

class PraktorInit {
public:
  /**
   * @brief Initialize a new Praktor project with a template.
   * @param template_name The name of the template to use (e.g., "basic", "cpp-library").
   * @return True if initialization was successful, false otherwise.
   */
  static bool initialize(const std::string &template_name);
};

} // namespace Utils
} // namespace Praktor

#endif // __PRAKTOR_INIT_HPP__
