#pragma once

#include "data/workflow_value.hpp"
#include "execution/execution_control.hpp"
#include "execution/execution_observer.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace Praktor::Execution {

enum class HostToolStatus {
    Ok = 0,
    NotFound,
    Denied,
    Failed,
    Cancelled,
    TimedOut,
};

struct HostToolResult {
    HostToolStatus status{HostToolStatus::Failed};
    WorkflowValue value{WorkflowValue::null()};
    std::string error_message;
};

/**
 * Backend-neutral host tool boundary.
 *
 * Implementations are supplied by an embedding host. Praktor owns neither the
 * tool registry nor the backend and never receives backend handles.
 */
class HostToolHost {
public:
    virtual ~HostToolHost() = default;

    /**
     * Preflight one frozen tool identity and its reviewed argument template.
     * This is called before reviewed-plan execution starts.
     */
    virtual bool validate(std::string_view tool_name,
                          const WorkflowValue& argument_template,
                          std::string* error_message) = 0;

    /**
     * Invoke one already-preflighted host tool.
     *
     * arguments is a fully resolved typed WorkflowValue object. Execution
     * control and observation are borrowed for the synchronous call.
     */
    virtual HostToolResult invoke(
        std::string_view tool_name,
        const WorkflowValue& arguments,
        const std::shared_ptr<ExecutionControl>& execution_control,
        const std::shared_ptr<ExecutionObserver>& execution_observer) = 0;
};

} // namespace Praktor::Execution
