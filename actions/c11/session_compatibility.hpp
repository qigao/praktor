#ifndef PRAKTOR_C11_SESSION_COMPATIBILITY_HPP
#define PRAKTOR_C11_SESSION_COMPATIBILITY_HPP

#include "tree_plan.hpp"
#include <string>
#include <vector>

namespace actions::c11::detail {
/* Implemented only by the optional legacy link target. Borrowed legacy objects
 * are never accessed by Process completion/recovery callbacks. */
struct SessionCompatibility {
  virtual ~SessionCompatibility() = default;
  virtual std::string get(const std::string& key) const = 0;
  virtual bool has(const std::string& key) const = 0;
  virtual void set(const std::string& key, const std::string& value) = 0;
  virtual salts_bt_node_status tick(const actions::Node& node) = 0;
  virtual std::vector<std::string> environment() const = 0;
  virtual void emit(const std::string& line) const = 0;
};
} // namespace actions::c11::detail
#endif
