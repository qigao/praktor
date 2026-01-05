#ifndef __TRIGGER_EXECUTOR_HPP__
#define __TRIGGER_EXECUTOR_HPP__

#include "workflow_context.hpp"
#include "yml/task.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Praktor {
namespace Execution {

/**
 * @class TriggerExecutor
 * @brief Executes event-driven triggers (on_success, on_failure, on_complete)
 */
class TriggerExecutor {
public:
    /**
     * @brief Execute triggers for a task based on its completion status
     * @param task The task that completed
     * @param success Whether the task succeeded
     * @param context The workflow context
     * @param environment Environment variables for trigger execution
     */
    void executeTriggers(const Task& task,
                        bool success,
                        WorkflowContext& context,
                        const std::unordered_map<std::string, std::string>& environment);

private:
    void executeHttpPost(const HttpPostTrigger& trigger,
                        WorkflowContext& context,
                        const std::unordered_map<std::string, std::string>& environment);

    void executeWriteFile(const WriteFileTrigger& trigger,
                         WorkflowContext& context);

    void executeRunTask(const RunTaskTrigger& trigger,
                       WorkflowContext& context,
                       const std::unordered_map<std::string, std::string>& environment);
    void executePraktorNotify(const PraktorNotifyTrigger& trigger,
                           WorkflowContext& context,
                           const std::unordered_map<std::string, std::string>& environment);

    void executeTriggerAction(const TriggerAction& action,
                             WorkflowContext& context,
                             const std::unordered_map<std::string, std::string>& environment);
};

} // namespace Execution
} // namespace Praktor

#endif // __TRIGGER_EXECUTOR_HPP__

