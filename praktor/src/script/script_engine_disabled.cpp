#include "script/script_engine.hpp"

namespace Praktor::Script {

ScriptResult execute(const std::string&, WorkflowContext&, const std::string&) {
    ScriptResult result;
    result.success = false;
    result.error_message =
        "Praktor script engine is disabled in this build (ENABLE_SCRIPT_ENGINE=OFF)";
    result.errors.push_back({0, result.error_message});
    return result;
}

} // namespace Praktor::Script
