#include "util/template_engine.hpp"
#include "util/variable_substitution.hpp"

#include <regex>
#include <sstream>
#include <jsoncons_ext/jmespath/jmespath.hpp>

namespace Weave::Util {

std::vector<Task> TemplateEngine::generateTasks(
    const DynamicTaskTemplate& task_template,
    const jsoncons::json& items_json,
    const WorkflowContext& base_context
) {
    std::vector<Task> generated_tasks;
    
    if (!items_json.is_array()) {
        throw std::runtime_error("Dynamic tasks items must be a JSON array");
    }
    
    for (size_t i = 0; i < items_json.size(); ++i) {
        const auto& item_data = items_json[i];
        Task task = createTaskFromTemplate(task_template, item_data, base_context, i);
        generated_tasks.push_back(std::move(task));
    }
    
    return generated_tasks;
}

std::string TemplateEngine::expandTemplate(
    const std::string& template_str,
    const jsoncons::json& item_data,
    const WorkflowContext& context
) {
    if (template_str.empty()) {
        return template_str;
    }
    
    std::string result = template_str;
    
    // Replace {{item.field}} patterns
    std::regex item_pattern(R"(\{\{item\.([^}]+)\}\})");
    std::smatch match;
    
    while (std::regex_search(result, match, item_pattern)) {
        std::string field_path = match[1].str();
        std::string full_match = match[0].str();
        
        try {
            // Use JMESPath to query the item data
            jsoncons::json field_value = jsoncons::jmespath::search(item_data, field_path);
            std::string replacement;
            
            if (field_value.is_string()) {
                replacement = field_value.as<std::string>();
            } else if (field_value.is_number()) {
                replacement = std::to_string(field_value.as<double>());
            } else if (field_value.is_bool()) {
                replacement = field_value.as<bool>() ? "true" : "false";
            } else {
                replacement = field_value.to_string();
            }
            
            // Replace the first occurrence
            size_t pos = result.find(full_match);
            if (pos != std::string::npos) {
                result.replace(pos, full_match.length(), replacement);
            }
        } catch (const std::exception& e) {
            // If field doesn't exist, replace with empty string or keep original
            size_t pos = result.find(full_match);
            if (pos != std::string::npos) {
                result.replace(pos, full_match.length(), "");
            }
        }
    }
    
    // Handle {{item}} (entire object as JSON string)
    std::regex item_object_pattern(R"(\{\{item\}\})");
    if (std::regex_search(result, item_object_pattern)) {
        std::string item_json_str = item_data.to_string();
        result = std::regex_replace(result, item_object_pattern, item_json_str);
    }
    
    // Use existing variable substitution for other {{variable}} patterns
    // This handles regular context variables
    result = substituteVariables(result, context);
    
    return result;
}

Task TemplateEngine::createTaskFromTemplate(
    const DynamicTaskTemplate& task_template,
    const jsoncons::json& item_data,
    const WorkflowContext& context,
    size_t index
) {
    Task task;
    
    // Expand template fields
    task.name = expandTemplate(task_template.name, item_data, context);
    task.type = task_template.type;  // Type is usually not templated
    
    // Handle timeout
    if (!task_template.timeout.empty()) {
        task.timeout = expandTemplate(task_template.timeout, item_data, context);
    }
    
    // Handle when condition
    if (!task_template.when.empty()) {
        task.when = expandTemplate(task_template.when, item_data, context);
    }
    
    // Handle dependencies (expand each dependency name)
    for (const auto& dep : task_template.depends_on) {
        std::string expanded_dep = expandTemplate(dep, item_data, context);
        task.depends_on.push_back(expanded_dep);
    }
    
    // Create task specifics based on type
    if (task_template.type == "run_command") {
        RunCommandParams params;
        std::string expanded_command = expandTemplate(task_template.command, item_data, context);
        params.command = expanded_command;
        task.specifics = params;
    } else {
        throw std::runtime_error("Unsupported task type for dynamic tasks: " + task_template.type);
    }
    
    return task;
}

} // namespace Weave::Util