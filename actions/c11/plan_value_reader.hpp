#ifndef PRAKTOR_C11_PLAN_VALUE_READER_HPP
#define PRAKTOR_C11_PLAN_VALUE_READER_HPP
#include "tree_plan.hpp"
#include <functional>
namespace actions::c11 {
namespace detail {
struct PlanValueReader {
  std::function<std::string(const std::string&)> get;
  std::function<bool(const std::string&)> has;
};
}
int64_t resolveIntegerParameter(const actions::Node&, const detail::PlanValueReader&,
                                const char*, int64_t, bool);
uint64_t resolveDurationParameter(const actions::Node&, const detail::PlanValueReader&,
                                  const char*, int64_t);
TreePlan compileTreePlan(const actions::Tree&, const detail::PlanValueReader&,
                         const salts_bt_tree_config&);
}
#endif
