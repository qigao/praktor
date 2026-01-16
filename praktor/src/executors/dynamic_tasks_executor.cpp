#include "executors/dynamic_tasks_executor.hpp"

#include "fmtlog.h"
#include "util/variable_substitution.hpp"

#include <jsoncons/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace Praktor::Execution
{

TaskResult DynamicTasksExecutor::execute(const Task& task, WorkflowContext& context)
{
    logi("Executing dynamic_tasks: {}", task.name);

    if (!subtask_callback_) {
        return TaskResult(false, "DynamicTasksExecutor requires subtask_callback to be set");
    }

    try {
        const auto& params = std::get<DynamicTasksParams>(task.specifics);

        // Get items_variable value from context
        std::string items_var = params.items_variable;

        // Remove {{ }} wrapper if present
        if (items_var.size() > 4 && items_var.rfind("{{", 0) == 0 &&
            items_var.substr(items_var.size() - 2) == "}}") {
            items_var = items_var.substr(2, items_var.size() - 4);
            // Trim whitespace
            size_t start = items_var.find_first_not_of(" \t\n\r");
            size_t end = items_var.find_last_not_of(" \t\n\r");
            if (start != std::string::npos) {
                items_var = items_var.substr(start, end - start + 1);
            }
        }

        // Retrieve the items array from context
        jsoncons::json items_json;
        try {
            items_json = context.getValueByPath(items_var);
            logi("Retrieved items_variable '{}': is_null={}, is_array={}, type={}", 
                 items_var, items_json.is_null(), items_json.is_array(), (int)items_json.type());
        } catch (const std::exception& e) {
            return TaskResult(false, fmt::format("dynamic_tasks items_variable '{}' not found in context: {}", 
                                items_var, e.what()));
        }

        if (items_json.is_null()) {
            return TaskResult(false, fmt::format("dynamic_tasks items_variable '{}' resolved to null", items_var));
        }

        if (!items_json.is_array()) {
            return TaskResult(false, fmt::format("dynamic_tasks items_variable '{}' must be a JSON array, got: {}", 
                                items_var, items_json.to_string()));
        }

        logi("Generating {} tasks from template", items_json.size());

        // Generate and execute tasks
        size_t index = 0;
        for (const auto& item : items_json.array_range()) {
            Task generated = generateTask(params.task_template, item, index, context);
            logi("Executing generated task: {}", generated.name);

            bool success = subtask_callback_(generated, context);
            if (!success) {
                return TaskResult(false, fmt::format("Generated task '{}' failed", generated.name));
            }

            ++index;
        }

        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, fmt::format("Task does not contain DynamicTasksParams: {}", e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, fmt::format("DynamicTasks execution failed: {}", e.what()));
    }
}

Task DynamicTasksExecutor::generateTask(const DynamicTaskTemplate& tmpl,
                                         const jsoncons::json& item,
                                         size_t index,
                                         WorkflowContext& context)
{
    Task task;

    // Substitute item placeholders in name
    task.name = substituteItemPlaceholders(tmpl.name, item, index);

    // Set action and specifics
    task.action = TaskAction::RunCommand;

    RunCommandParams cmd_params;
    if (std::holds_alternative<std::string>(tmpl.command)) {
        cmd_params.command = substituteItemPlaceholders(
            std::get<std::string>(tmpl.command), item, index);
    } else {
        StrList cmd_list;
        for (const auto& part : std::get<StrList>(tmpl.command)) {
            cmd_list.push_back(substituteItemPlaceholders(part, item, index));
        }
        cmd_params.command = cmd_list;
    }
    task.specifics = cmd_params;

    // Copy optional fields
    if (tmpl.timeout) {
        task.timeout = substituteItemPlaceholders(*tmpl.timeout, item, index);
    }

    if (tmpl.retries) {
        task.retries = *tmpl.retries;
    }

    if (tmpl.when) {
        task.when = substituteItemPlaceholders(*tmpl.when, item, index);
    }

    for (const auto& dep : tmpl.depends_on) {
        task.depends_on.push_back(substituteItemPlaceholders(dep, item, index));
    }

    for (const auto& [key, value] : tmpl.env) {
        task.env[key] = substituteItemPlaceholders(value, item, index);
    }

    return task;
}

std::string DynamicTasksExecutor::substituteItemPlaceholders(const std::string& input,
                                                              const jsoncons::json& item,
                                                              size_t index)
{
    std::string result = input;

    // Replace {{ item.field }} patterns first (more specific)
    std::regex field_pattern(R"(\{\{\s*item\.([a-zA-Z_][a-zA-Z0-9_]*)\s*\}\})");
    std::smatch match;
    std::string working = result;

    while (std::regex_search(working, match, field_pattern)) {
        std::string field_name = match[1].str();
        std::string replacement;

        if (item.is_object() && item.contains(field_name)) {
            const auto& field_value = item[field_name];
            if (field_value.is_string()) {
                replacement = field_value.as<std::string>();
            } else {
                replacement = field_value.to_string();
            }
        } else {
            replacement = "";  // Field not found, replace with empty
        }

        result = std::regex_replace(result, field_pattern, replacement,
                                    std::regex_constants::format_first_only);
        working = match.suffix().str();
    }

    // Replace {{ item }} with the whole item as JSON string
    std::regex item_pattern(R"(\{\{\s*item\s*\}\})");
    if (item.is_string()) {
        result = std::regex_replace(result, item_pattern, item.as<std::string>());
    } else {
        result = std::regex_replace(result, item_pattern, item.to_string());
    }

    // Replace {{ index }} with the current index
    std::regex index_pattern(R"(\{\{\s*index\s*\}\})");
    result = std::regex_replace(result, index_pattern, std::to_string(index));

    return result;
}

std::unique_ptr<TaskExecutor> createDynamicTasksExecutor()
{
    return std::make_unique<DynamicTasksExecutor>();
}

}  // namespace Praktor::Execution
