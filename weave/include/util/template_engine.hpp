#ifndef __TEMPLATE_ENGINE_HPP__
#define __TEMPLATE_ENGINE_HPP__

#include "dag/workflow_context.hpp"
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <string>
#include <vector>
#include <jsoncons/json.hpp>

namespace Weave::Util {

/**
 * @class TemplateEngine
 * @brief Handles template substitution for dynamic task generation
 */
class TemplateEngine {
public:
    /**
     * @brief Generate tasks from a template and JSON array
     * @param task_template The template to expand
     * @param items_json JSON array containing data for each task
     * @param base_context The workflow context for variable resolution
     * @return Vector of generated tasks
     */
    static std::vector<Task> generateTasks(
        const DynamicTaskTemplate& task_template,
        const jsoncons::json& items_json,
        const WorkflowContext& base_context
    );

    /**
     * @brief Replace template placeholders in a string
     * @param template_str String with {{item.field}} placeholders  
     * @param item_data JSON object for current item
     * @param context Workflow context for other variables
     * @return String with placeholders replaced
     */
    static std::string expandTemplate(
        const std::string& template_str,
        const jsoncons::json& item_data,
        const WorkflowContext& context
    );

private:
    /**
     * @brief Create a Task from template and item data
     */
    static Task createTaskFromTemplate(
        const DynamicTaskTemplate& task_template,
        const jsoncons::json& item_data,
        const WorkflowContext& context,
        size_t index
    );
};

} // namespace Weave::Util

#endif // __TEMPLATE_ENGINE_HPP__