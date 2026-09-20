#include "script/script_engine.hpp"

#include "dag/workflow_context.hpp"

namespace Praktor::Script {

ScriptResult execute(const std::string& source, WorkflowContext& context,
                     const std::string& script_source_path) {
    (void)source;
    (void)context;
    (void)script_source_path;

    ScriptResult result;
    result.success = false;
    result.error_message = "TurboScript support is disabled in this Praktor build";
    result.errors.push_back({0, result.error_message});
    return result;
}

} // namespace Praktor::Script
