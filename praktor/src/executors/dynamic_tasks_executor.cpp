#include "executors/dynamic_tasks_executor.hpp"

#include "util/logging.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_yaml.hpp"

#include <jsoncons/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace Praktor::Execution
{

namespace {

std::string taskTypeName(const Task& task)
{
    if (!task.declared_runner.empty()) {
        return task.declared_runner;
    }

    if (task.script && task.action == TaskAction::None) {
        return "script";
    }

    switch (task.action) {
        case TaskAction::Uses:
            return "uses";
        case TaskAction::DynamicTasks:
            return "dynamic_tasks";
        case TaskAction::Orch:
            return "actions";
        case TaskAction::None:
            break;
    }

    return "unknown";
}

void mergeTaskOutputs(TaskFailureContext& failure, const WorkflowContext& context,
                      const std::string& task_name)
{
    auto outputs = context.getValueByPath("tasks." + task_name + ".outputs");
    if (!outputs.is_object()) {
        return;
    }

    for (const auto& item : outputs.object_range()) {
        failure.captured_outputs[item.key()] = item.value();
    }

    if (outputs.contains("stdout") && outputs["stdout"].is_string()) {
        failure.stdout_data = outputs["stdout"].as<std::string>();
    }
    if (outputs.contains("stderr") && outputs["stderr"].is_string()) {
        failure.stderr_data = outputs["stderr"].as<std::string>();
    }
    if (outputs.contains("exit_code")) {
        try {
            failure.exit_code = outputs["exit_code"].as<int64_t>();
        } catch (const std::exception&) {
        }
    }
}

} // namespace

TaskResult DynamicTasksExecutor::execute(const Task& task, WorkflowContext& context)
{
    TLOG_DEBUG("Executing dynamic_tasks: {}", task.name);

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
            TLOG_DEBUG("Retrieved items_variable '{}': is_null={}, is_array={}, type={}",
                 items_var, items_json.is_null(), items_json.is_array(), (int)items_json.type());
        } catch (const std::exception& e) {
            return TaskResult(false, "dynamic_tasks items_variable '" + items_var + "' not found in context: " + e.what());
        }

        if (items_json.is_null()) {
            return TaskResult(false, "dynamic_tasks items_variable '" + items_var + "' resolved to null");
        }

        if (!items_json.is_array()) {
            return TaskResult(false, "dynamic_tasks items_variable '" + items_var + "' must be a JSON array, got: " + items_json.to_string());
        }

        TLOG_DEBUG("Generating {} tasks from template", items_json.size());
        jsoncons::json generated_results = jsoncons::json::array();
        context.setCurrentTaskOutput("generated_tasks", generated_results);
        context.setCurrentTaskOutput("generated_count", static_cast<int64_t>(0));
        context.setCurrentTaskOutput("success_count", static_cast<int64_t>(0));
        context.setCurrentTaskOutput("failed_count", static_cast<int64_t>(0));
        context.setCurrentTaskOutput("skipped_count", static_cast<int64_t>(0));

        // Generate and execute tasks
        size_t index = 0;
        int64_t success_count = 0;
        int64_t failed_count = 0;
        int64_t skipped_count = 0;
        std::unordered_set<std::string> generated_names;
        std::string first_failed_task;
        std::optional<TaskFailureContext> first_failure_context;
        for (const auto& item : items_json.array_range()) {
            Task generated = generateTask(task, params.task_template, item, index);
            if (generated.name.empty()) {
                return TaskResult(false, "Generated task name must not be empty");
            }
            if (generated.name == task.name) {
                return TaskResult(false, "Generated task name '" + generated.name +
                                              "' collides with parent dynamic_tasks task");
            }
            if (task_name_exists_callback_ && task_name_exists_callback_(generated.name)) {
                return TaskResult(false, "Generated task name '" + generated.name +
                                              "' collides with an existing workflow task");
            }
            if (!generated_names.insert(generated.name).second) {
                return TaskResult(false, "Generated task name collision: '" + generated.name + "'");
            }
            TLOG_DEBUG("Executing generated task: {}", generated.name);

            bool callback_success = subtask_callback_(generated, context);
            jsoncons::json generated_result =
                buildGeneratedTaskResult(generated, item, index, callback_success, context);
            const std::string status = generated_result["status"].as<std::string>();
            if (status == "success") {
                ++success_count;
            } else if (status == "skipped") {
                ++skipped_count;
            } else {
                ++failed_count;
                if (first_failed_task.empty()) {
                    first_failed_task = generated.name;
                }
                if (!first_failure_context.has_value()) {
                    first_failure_context =
                        buildGeneratedTaskFailureContext(generated, item, index, context);
                }
            }

            generated_results.push_back(std::move(generated_result));
            context.setCurrentTaskOutput("generated_tasks", generated_results);
            context.setCurrentTaskOutput("generated_count", static_cast<int64_t>(generated_results.size()));
            context.setCurrentTaskOutput("success_count", success_count);
            context.setCurrentTaskOutput("failed_count", failed_count);
            context.setCurrentTaskOutput("skipped_count", skipped_count);
            if (!callback_success) {
                TaskResult result(false, "Generated task '" + generated.name + "' failed");
                if (first_failure_context.has_value()) {
                    result.nested_failure_context = *first_failure_context;
                }
                return result;
            }

            ++index;
        }

        if (failed_count > 0) {
            std::ostringstream message;
            message << failed_count << " generated task(s) failed";
            if (!first_failed_task.empty()) {
                message << " (first: '" << first_failed_task << "')";
            }
            TaskResult result(false, message.str());
            if (first_failure_context.has_value()) {
                result.nested_failure_context = *first_failure_context;
            }
            return result;
        }

        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, "Task does not contain DynamicTasksParams: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, "DynamicTasks execution failed: " + std::string(e.what()));
    }
}

TaskFailureContext DynamicTasksExecutor::buildGeneratedTaskFailureContext(
    const Task& generated_task, const jsoncons::json& item, size_t index,
    const WorkflowContext& context) const
{
    TaskFailureContext failure;
    failure.task_name = generated_task.name;
    failure.task_type = taskTypeName(generated_task);

    auto failed_tasks = context.getFailedTasks();
    auto it = failed_tasks.find(generated_task.name);
    if (it != failed_tasks.end()) {
        failure.error_message = it->second;
    }

    mergeTaskOutputs(failure, context, generated_task.name);
    failure.captured_outputs["index"] = static_cast<int64_t>(index);
    failure.captured_outputs["item"] = item;
    return failure;
}

jsoncons::json DynamicTasksExecutor::buildGeneratedTaskResult(const Task& generated_task,
                                                             const jsoncons::json& item,
                                                             size_t index,
                                                             bool callback_success,
                                                             WorkflowContext& context) const
{
    jsoncons::json result = jsoncons::json::object();
    result["index"] = static_cast<int64_t>(index);
    result["item"] = item;
    result["name"] = generated_task.name;
    std::string status = context.getTaskStatus(generated_task.name);
    if (status == "pending" || status == "running") {
        status = callback_success ? "success" : "failed";
        context.setTaskStatus(generated_task.name, status);
    }
    result["status"] = status;

    auto outputs = context.getValueByPath("tasks." + generated_task.name + ".outputs");
    result["outputs"] = outputs.is_object() ? outputs : jsoncons::json::object();
    return result;
}

Task DynamicTasksExecutor::generateTask(const Task& parent_task,
                                         const DynamicTaskTemplate& tmpl,
                                         const jsoncons::json& item,
                                         size_t index)
{
    Task task;

    // Substitute item placeholders in name
    task.name = substituteItemPlaceholders(tmpl.name, item, index);

    // Set action and specifics using action substitution
    task.action = TaskAction::Orch;
    task.declared_runner = "command";
    task.source_path = parent_task.source_path;

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
    task.specifics = desugarCommandToBtdsl(cmd_params);

    // Copy optional fields
    if (tmpl.timeout) {
        task.timeout = substituteItemPlaceholders(*tmpl.timeout, item, index);
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
