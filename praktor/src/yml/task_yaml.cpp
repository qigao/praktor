#include "yml/task_yaml.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace {
std::string ltrim_copy(std::string s) { s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); return s; }
std::string rtrim_copy(std::string s) { s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); return s; }
std::string trim_copy(std::string s) { return rtrim_copy(ltrim_copy(std::move(s))); }

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

[[noreturn]] void throw_parse_error(const ryml::ConstNodeRef& node, const std::string& message) {
    throw std::runtime_error("Parse error: " + message);
}

void check_unknown_keys(const ryml::ConstNodeRef& node, const std::unordered_set<std::string>& allowed_keys) {
    if (!node.is_map()) return;
    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        if (allowed_keys.find(key) == allowed_keys.end()) {
            throw_parse_error(child, "Unknown key: '" + key + "'");
        }
    }
}

CommandOutputFormat parse_output_format(const ryml::ConstNodeRef& node, const std::string& value) {
    if (value.empty()) {
        return CommandOutputFormat::Text;
    }

    std::string lowered = to_lower_copy(value);
    if (lowered == "text") {
        return CommandOutputFormat::Text;
    }
    if (lowered == "json") {
        return CommandOutputFormat::Json;
    }
    throw_parse_error(node, "Unsupported output_format value: '" + value + "'");
}

WriteFileMode parse_write_file_mode(const ryml::ConstNodeRef& node, const std::string& value) {
    std::string lowered = to_lower_copy(value);
    if (lowered == "overwrite" || lowered.empty()) {
        return WriteFileMode::Overwrite;
    }
    if (lowered == "append") {
        return WriteFileMode::Append;
    }
    throw_parse_error(node, "Unsupported write_file mode: '" + value + "'");
}

HttpPostTrigger parse_http_post_trigger(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "http_post trigger must be a map");
    }

    HttpPostTrigger trigger;
    if (!node.has_child("url")) {
        throw_parse_error(node, "http_post trigger requires a 'url'");
    }
    node["url"] >> trigger.url;
    if (trigger.url.empty()) {
        throw_parse_error(node["url"], "http_post trigger 'url' cannot be empty");
    }

    if (node.has_child("body")) {
        std::string body;
        node["body"] >> body;
        trigger.body = body;
    }

    if (node.has_child("headers")) {
        const auto& headers_node = node["headers"];
        if (!headers_node.is_map()) {
            throw_parse_error(headers_node, "http_post trigger 'headers' must be a map");
        }
        for (const auto& header : headers_node) {
            std::string key(header.key().str, header.key().len);
            std::string value;
            header >> value;
            trigger.headers[key] = value;
        }
    }

    return trigger;
}

WriteFileTrigger parse_write_file_trigger(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "write_file trigger must be a map");
    }

    WriteFileTrigger trigger;
    if (!node.has_child("path") || !node.has_child("content")) {
        throw_parse_error(node, "write_file trigger requires 'path' and 'content'");
    }
    node["path"] >> trigger.path;
    node["content"] >> trigger.content;

    if (trigger.path.empty()) {
        throw_parse_error(node["path"], "write_file trigger 'path' cannot be empty");
    }

    if (node.has_child("mode")) {
        std::string mode;
        node["mode"] >> mode;
        trigger.mode = parse_write_file_mode(node["mode"], mode);
    }

    return trigger;
}


PraktorNotifyTrigger parse_praktor_notify_trigger(const ryml::ConstNodeRef& node) {
    PraktorNotifyTrigger trigger;
    if (node.is_val()) {
        std::string value;
        node >> value;
        std::string trimmed = trim_copy(value);
        const std::string prefix = "@praktor";
        if (trimmed.rfind(prefix, 0) == 0) {
            std::string msg = trim_copy(trimmed.substr(prefix.size()));
            if (!msg.empty() && (msg[0] == ':' || msg[0] == '-')) {
                msg = trim_copy(msg.substr(1));
            }
            trigger.message = std::move(msg);
        } else {
            trigger.message = std::move(trimmed);
        }
    } else if (node.is_map()) {
        if (!node.has_child("message")) {
            throw_parse_error(node, "praktor trigger requires 'message'");
        }
        node["message"] >> trigger.message;
    } else {
        throw_parse_error(node, "praktor trigger must be a string or map");
    }

    if (trigger.message.empty()) {
        throw_parse_error(node, "praktor trigger 'message' cannot be empty");
    }
    return trigger;
}RunTaskTrigger parse_run_task_trigger(const ryml::ConstNodeRef& node) {
    RunTaskTrigger trigger;
    if (node.is_val()) {
        node >> trigger.task_name;
    } else if (node.is_map()) {
        if (!node.has_child("task_name")) {
            throw_parse_error(node, "run_task trigger requires 'task_name'");
        }
        node["task_name"] >> trigger.task_name;
    } else {
        throw_parse_error(node, "run_task trigger must be a string or map");
    }

    if (trigger.task_name.empty()) {
        throw_parse_error(node, "run_task trigger 'task_name' cannot be empty");
    }

    return trigger;
}

TriggerAction parse_trigger_action_node(const ryml::ConstNodeRef& node) {
    if (node.is_val()) {
        std::string scalar;
        node >> scalar;
        std::string trimmed = trim_copy(scalar);
        if (trimmed.rfind("@praktor", 0) == 0) {
            return TriggerAction{parse_praktor_notify_trigger(node)};
        }
        return TriggerAction{parse_run_task_trigger(node)};
    }

    if (!node.is_map()) {
        throw_parse_error(node, "Trigger action must be a string or single-key map");
    }

    if (node.num_children() != 1) {
        throw_parse_error(node, "Trigger action map must contain exactly one action");
    }

    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        if (key == "http_post") {
            return TriggerAction{parse_http_post_trigger(child)};
        }
        if (key == "write_file") {
            return TriggerAction{parse_write_file_trigger(child)};
        }
        if (key == "run_task") {
            return TriggerAction{parse_run_task_trigger(child)};
        }
        if (key == "praktor" || key == "@praktor") {
            return TriggerAction{parse_praktor_notify_trigger(child)};
        }
        throw_parse_error(child, "Unsupported trigger action: '" + key + "'");
    }

    throw_parse_error(node, "Failed to parse trigger action");
}std::vector<TriggerAction> parse_trigger_action_list(const ryml::ConstNodeRef& node) {
    if (!node.is_seq()) {
        throw_parse_error(node, "Trigger action list must be a sequence");
    }

    std::vector<TriggerAction> actions;
    for (const auto& item : node) {
        actions.emplace_back(parse_trigger_action_node(item));
    }
    return actions;
}

} // namespace

StrList node_to_string_vector(const ryml::ConstNodeRef& node) {
    StrList result;
    if (node.is_seq()) {
        for (const auto& child : node) {
            std::string value;
            child >> value;
            result.push_back(value);
        }
    } else if (node.has_val()) {
        std::string value;
        node >> value;
        if (!value.empty()) {
            result.push_back(value);
        }
    } else {
        throw_parse_error(node, "Expected sequence or scalar when converting to string list");
    }
    return result;
}

Vars node_to_string_map(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "Expected mapping when converting to string map");
    }

    Vars result;
    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        std::string value;
        child >> value;
        result[key] = value;
    }
    return result;
}

RetryPolicy parse_retry_policy(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "retries must be a map");
    }

    RetryPolicy policy;
    if (node.has_child("count")) {
        node["count"] >> policy.count;
        if (policy.count < 0) {
            throw_parse_error(node["count"], "retry count cannot be negative");
        }
    }

    if (node.has_child("delay")) {
        node["delay"] >> policy.delay;
    }

    return policy;
}

Each parse_each(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "each must be a map");
    }

    Each each;
    if (node.has_child("items")) {
        each.items = node_to_string_vector(node["items"]);
    }

    if (node.has_child("matrix")) {
        const auto& matrix_node = node["matrix"];
        if (!matrix_node.is_map()) {
            throw_parse_error(matrix_node, "each.matrix must be a map");
        }
        for (const auto& child : matrix_node) {
            std::string key(child.key().str, child.key().len);
            each.matrix[key] = node_to_string_vector(child);
        }
    }

    if (each.hasItems() && each.hasMatrix()) {
        throw_parse_error(node, "each cannot define both 'items' and 'matrix'");
    }
    if (!each.hasItems() && !each.hasMatrix()) {
        throw_parse_error(node, "each requires either 'items' or 'matrix'");
    }

    if (node.has_child("as")) {
        node["as"] >> each.as;
        if (each.as.empty()) {
            each.as = "item";
        }
    }

    if (node.has_child("index_variable")) {
        node["index_variable"] >> each.index_variable;
    }

    return each;
}

Triggers parse_triggers(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "triggers must be a map");
    }

    Triggers triggers;

    if (node.has_child("on_success")) {
        triggers.on_success = parse_trigger_action_list(node["on_success"]);
    }
    if (node.has_child("on_failure")) {
        triggers.on_failure = parse_trigger_action_list(node["on_failure"]);
    }
    if (node.has_child("on_complete")) {
        triggers.on_complete = parse_trigger_action_list(node["on_complete"]);
    }

    if (triggers.empty()) {
        throw_parse_error(node, "triggers block must contain at least one action list");
    }

    return triggers;
}

RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node) {
    if (!node.has_child("command")) {
        throw_parse_error(node, "run_command task requires a 'command'");
    }

    RunCommandParams params;
    const auto& command_node = node["command"];
    if (command_node.is_seq()) {
        StrList command_list = node_to_string_vector(command_node);
        if (command_list.empty()) {
            throw_parse_error(command_node, "command array must contain at least one entry");
        }
        params.command = command_list;
    } else if (command_node.has_val()) {
        auto value = command_node.val();
        std::string command(value.str, value.len);
        if (command.empty()) {
            throw_parse_error(command_node, "command string cannot be empty");
        }
        params.command = command;
    } else {
        std::string command;
        command_node >> command;
        if (command.empty()) {
            throw_parse_error(command_node, "command string cannot be empty");
        }
        params.command = command;
    }

    if (node.has_child("output_format")) {
        std::string format;
        node["output_format"] >> format;
        params.output_format = parse_output_format(node["output_format"], format);
    }

    return params;
}


ScriptParams parse_script_params(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "script must be a map");
    }
    if (!node.has_child("source")) {
        throw_parse_error(node, "script requires a 'source'");
    }

    ScriptParams params;
    node["source"] >> params.source;
    if (params.source.empty()) {
        throw_parse_error(node["source"], "script source cannot be empty");
    }

    return params;
}

UsesParams parse_uses_params(const ryml::ConstNodeRef& node) {
    UsesParams params;
    if (node.has_val()) {
        auto value = node.val();
        params.path.assign(value.str, value.len);
    } else {
        node >> params.path;
    }
    if (params.path.empty()) {
        throw_parse_error(node, "uses value cannot be empty");
    }
    return params;
}

DynamicTasksParams parse_dynamic_tasks_params(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "dynamic_tasks must be a map");
    }

    DynamicTasksParams params;

    // Parse items_variable (required)
    if (!node.has_child("items_variable")) {
        throw_parse_error(node, "dynamic_tasks requires 'items_variable'");
    }
    node["items_variable"] >> params.items_variable;
    if (params.items_variable.empty()) {
        throw_parse_error(node["items_variable"], "dynamic_tasks 'items_variable' cannot be empty");
    }

    // Parse template (required)
    if (!node.has_child("template")) {
        throw_parse_error(node, "dynamic_tasks requires 'template'");
    }
    const auto& tmpl_node = node["template"];
    if (!tmpl_node.is_map()) {
        throw_parse_error(tmpl_node, "dynamic_tasks 'template' must be a map");
    }

    // Template name (required)
    if (!tmpl_node.has_child("name")) {
        throw_parse_error(tmpl_node, "dynamic_tasks template requires 'name'");
    }
    tmpl_node["name"] >> params.task_template.name;

    // Template command (required)
    if (!tmpl_node.has_child("command")) {
        throw_parse_error(tmpl_node, "dynamic_tasks template requires 'command'");
    }
    const auto& cmd_node = tmpl_node["command"];
    if (cmd_node.is_seq()) {
        params.task_template.command = node_to_string_vector(cmd_node);
    } else {
        std::string cmd;
        cmd_node >> cmd;
        params.task_template.command = cmd;
    }

    // Optional template fields
    if (tmpl_node.has_child("timeout")) {
        std::string timeout;
        tmpl_node["timeout"] >> timeout;
        params.task_template.timeout = timeout;
    }

    if (tmpl_node.has_child("retries")) {
        params.task_template.retries = parse_retry_policy(tmpl_node["retries"]);
    }

    if (tmpl_node.has_child("when")) {
        std::string when;
        tmpl_node["when"] >> when;
        params.task_template.when = when;
    }

    if (tmpl_node.has_child("depends_on")) {
        params.task_template.depends_on = node_to_string_vector(tmpl_node["depends_on"]);
    }

    if (tmpl_node.has_child("env")) {
        params.task_template.env = node_to_string_map(tmpl_node["env"]);
    }

    return params;
}


TaskDefaults parse_defaults(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw_parse_error(node, "defaults must be a map");
    }

    TaskDefaults defaults;
    if (node.has_child("retries")) {
        defaults.retries = parse_retry_policy(node["retries"]);
    }
    if (node.has_child("timeout")) {
        std::string timeout;
        node["timeout"] >> timeout;
        defaults.timeout = timeout;
    }
    return defaults;
}

Task parse_task(const ryml::ConstNodeRef& node, const std::string& source_path) {
    if (!node.is_map()) {
        throw_parse_error(node, "task must be a map");
    }

    static const std::unordered_set<std::string> allowed_task_keys = {
        "name", "description", "depends_on", "vars", "env", "dotEnv", "when",
        "each", "retries", "timeout", "triggers", "continue_on_error",
        "working_dir", "silent", "sources", "generates", "finally",
        "command", "script", "uses", "dynamic_tasks", "output_format"
    };
    check_unknown_keys(node, allowed_task_keys);

    Task task;

    if (!node.has_child("name")) {
        throw_parse_error(node, "task requires a 'name'");
    }
    node["name"] >> task.name;
    if (task.name.empty()) {
        throw_parse_error(node["name"], "task name cannot be empty");
    }

    if (node.has_child("description")) {
        node["description"] >> task.description;
    }

    if (node.has_child("depends_on")) {
        task.depends_on = node_to_string_vector(node["depends_on"]);
    }

    if (node.has_child("vars")) {
        task.vars = node_to_string_map(node["vars"]);
    }

    if (node.has_child("env")) {
        task.env = node_to_string_map(node["env"]);
    }

    if (node.has_child("dotEnv")) {
        task.dot_env = node_to_string_vector(node["dotEnv"]);
    }

    if (node.has_child("when")) {
        std::string when;
        node["when"] >> when;
        task.when = when;
    }

    if (node.has_child("each")) {
        task.each = parse_each(node["each"]);
    }

    if (node.has_child("retries")) {
        task.retries = parse_retry_policy(node["retries"]);
    }

    if (node.has_child("timeout")) {
        std::string timeout;
        node["timeout"] >> timeout;
        task.timeout = timeout;
    }

    if (node.has_child("triggers")) {
        Triggers triggers = parse_triggers(node["triggers"]);
        if (!triggers.empty()) {
            task.triggers = triggers;
        }
    }

    // Control flow
    if (node.has_child("continue_on_error")) {
        std::string value;
        node["continue_on_error"] >> value;
        std::string lowered = to_lower_copy(value);
        task.continue_on_error = (lowered == "true" || lowered == "yes" || lowered == "1");
    }

    // Execution context
    if (node.has_child("working_dir")) {
        std::string working_dir;
        node["working_dir"] >> working_dir;
        if (!working_dir.empty()) {
            task.working_dir = working_dir;
        }
    }

    if (node.has_child("silent")) {
        std::string value;
        node["silent"] >> value;
        std::string lowered = to_lower_copy(value);
        task.silent = (lowered == "true" || lowered == "yes" || lowered == "1");
    }

    // Incremental build
    if (node.has_child("sources")) {
        task.sources = node_to_string_vector(node["sources"]);
    }

    if (node.has_child("generates")) {
        task.generates = node_to_string_vector(node["generates"]);
    }

    // Cleanup task
    if (node.has_child("finally")) {
        std::string finally_task;
        node["finally"] >> finally_task;
        if (!finally_task.empty()) {
            task.finally_task = finally_task;
        }
    }

    int action_count = 0;

    if (node.has_child("command")) {
        task.action = TaskAction::RunCommand;
        task.specifics = parse_run_command_params(node);
        ++action_count;
    }

    if (node.has_child("script")) {
        const auto& script_node = node["script"];
        task.action = TaskAction::Script;
        task.specifics = parse_script_params(script_node);
        ++action_count;
    }

    if (node.has_child("uses")) {
        const auto& uses_node = node["uses"];
        task.action = TaskAction::Uses;
        task.specifics = parse_uses_params(uses_node);
        ++action_count;
    }

    if (node.has_child("dynamic_tasks")) {
        const auto& dt_node = node["dynamic_tasks"];
        task.action = TaskAction::DynamicTasks;
        task.specifics = parse_dynamic_tasks_params(dt_node);
        ++action_count;
    }

    if (action_count == 0) {
        throw_parse_error(node, "task '" + task.name + "' must declare exactly one runner (command/script/uses/dynamic_tasks)");
    }
    if (action_count > 1) {
        throw_parse_error(node, "task '" + task.name + "' declares multiple runners");
    }

    return task;
}

Workflow parse_workflow(const ryml::ConstNodeRef& node, const std::string& source_path) {
    if (!node.is_map()) {
        throw std::runtime_error("workflow must be a map");
    }

    Workflow workflow;

    if (node.has_child("name")) {
        node["name"] >> workflow.name;
    }
    if (node.has_child("description")) {
        node["description"] >> workflow.description;
    }

    if (node.has_child("variables")) {
        workflow.variables = node_to_string_map(node["variables"]);
    }

    if (node.has_child("env")) {
        workflow.env = node_to_string_map(node["env"]);
    }

    if (node.has_child("dotEnv")) {
        workflow.dot_env = node_to_string_vector(node["dotEnv"]);
    }

    if (node.has_child("embedded")) {
        const auto& embedded_node = node["embedded"];
        if (!embedded_node.is_map()) {
            throw std::runtime_error("workflow 'embedded' section must be a map of modules");
        }
        for (const auto& module_node : embedded_node) {
            std::string module_name(module_node.key().str, module_node.key().len);
            if (!module_node.is_map()) {
                throw std::runtime_error("Embedded module '" + module_name + "' must be a map");
            }
            EmbeddedModule module;
            module.language = get_optional<std::string>(module_node, "language", std::string("javascript"));
            if (!module_node.has_child("source")) {
                throw std::runtime_error("Embedded module '" + module_name + "' requires a 'source' field");
            }
            module_node["source"] >> module.source;
            if (module.source.empty()) {
                throw std::runtime_error("Embedded module '" + module_name + "' cannot have an empty source");
            }
            workflow.embedded[module_name] = std::move(module);
        }
    }

    if (node.has_child("defaults")) {
        workflow.defaults = parse_defaults(node["defaults"]);
    }

    if (!node.has_child("tasks")) {
        throw std::runtime_error("workflow must define a 'tasks' list");
    }

    const auto& tasks_node = node["tasks"];
    if (!tasks_node.is_seq()) {
        throw std::runtime_error("'tasks' must be a sequence");
    }

    for (const auto& task_node : tasks_node) {
        workflow.tasks.push_back(parse_task(task_node, source_path));
    }

    if (workflow.tasks.empty()) {
        throw std::runtime_error("workflow must define at least one task");
    }

    return workflow;
}

