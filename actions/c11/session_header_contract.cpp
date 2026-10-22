#include "c11/tree_executor_session.hpp"

#include <type_traits>

// This translation unit intentionally does not include core/executor.hpp.
// The session API must be parseable without the legacy Executor definition.
static_assert(std::is_enum_v<actions::c11::TreePlanTerminalAction>);
static_assert(sizeof(actions::c11::TreePlanTerminalAction) == sizeof(unsigned char));
