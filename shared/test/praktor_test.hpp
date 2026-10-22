#pragma once

// Workflow tests use variables named context; keep that identifier available.
#define TT_NO_CONTEXT_KEYWORD
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <tinytest.hpp>

// These optional BDD aliases collide with Windows PPL's task::then and
// workflow APIs. Tests use spec/it directly; keep library identifiers intact.
#undef given
#undef when
#undef then

#include <array>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>

namespace praktor_test {

template <class Write>
std::string message(Write&& write) {
  std::ostringstream out;
  write(out);
  return out.str();
}

// Evaluate the operation once and check every diagnostic fragment against the
// same exception. Rethrow TinyTest's assertion unwind so failures stay failures.
template <class Operation>
bool throws_containing(Operation&& operation,
                       std::initializer_list<std::string> fragments) {
  try {
    operation();
  } catch (const ttest_fail_exception__&) {
    throw;
  } catch (const std::exception& error) {
    info("exception: %s", error.what());
    for (const auto& fragment : fragments) {
      if (std::string(error.what()).find(fragment) == std::string::npos) return false;
    }
    return true;
  } catch (...) {
    info("unexpected non-standard exception");
    return false;
  }
  info("expected exception was not thrown");
  return false;
}

}  // namespace praktor_test
