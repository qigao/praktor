// Compile-only public include contract. No "core/executor.hpp" or
// C++ actions target must be imported by consumers of Praktor headers.
#include "executors/declarative_tree_executor.hpp"

#include <type_traits>

static_assert(std::is_base_of_v<
    TaskExecutor, Praktor::Execution::DeclarativeTreeExecutor>);
static_assert(std::is_enum_v<Praktor::Execution::ActionTreeBackend>);
