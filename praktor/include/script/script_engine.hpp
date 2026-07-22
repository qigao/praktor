#ifndef PRAKTOR_SCRIPT_ENGINE_HPP
#define PRAKTOR_SCRIPT_ENGINE_HPP

#include "data/workflow_value.hpp"
#include <string>
#include <vector>

class WorkflowContext;

namespace Praktor::Script {

struct ScriptError {
    int line;
    std::string message;
};

struct ScriptResult {
    bool success = true;
    WorkflowValue value;               // return value (if any)
    std::vector<ScriptError> errors;   // parse or runtime errors
    std::string error_message;         // first error as string
};

// Parse and execute a script string against a WorkflowContext.
// The script can read/write context variables, call DLLs, make HTTP requests, etc.
ScriptResult execute(const std::string& source, WorkflowContext& context);

} // namespace Praktor::Script

#endif // PRAKTOR_SCRIPT_ENGINE_HPP
