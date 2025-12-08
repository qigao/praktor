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

CommandOutputFormat parse_output_format(const std::string& value) {
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
    throw std::runtime_error("Unsupported output_format value: '" + value + "'");
}

WriteFileMode parse_write_file_mode(const std::string& value) {
    std::string lowered = to_lower_copy(value);
    if (lowered == "overwrite" || lowered.empty()) {
        return WriteFileMode::Overwrite;
    }
    if (lowered == "append") {
        return WriteFileMode::Append;
    }
    throw std::runtime_error("Unsupported write_file mode: '" + value + "'");
}

HttpPostTrigger parse_http_post_trigger(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw std::runtime_error("http_post trigger must be a map");
    }

    HttpPostTrigger trigger;
    if (!node.has_child("url")) {
        throw std::runtime_error("http_post trigger requires a 'url'");
    }
    node["url"] >> trigger.url;
    if (trigger.url.empty()) {
        throw std::runtime_error("http_post trigger 'url' cannot be empty");
    }

    if (node.has_child("body")) {
        std::string body;
        node["body"] >> body;
        trigger.body = body;
    }

    if (node.has_child("headers")) {
        const auto& headers_node = node["headers"];
        if (!headers_node.is_map()) {
            throw std::runtime_error("http_post trigger 'headers' must be a map");
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
        throw std::runtime_error("write_file trigger must be a map");
    }

    WriteFileTrigger trigger;
    if (!node.has_child("path") || !node.has_child("content")) {
        throw std::runtime_error("write_file trigger requires 'path' and 'content'");
    }
    node["path"] >> trigger.path;
    node["content"] >> trigger.content;

    if (trigger.path.empty()) {
        throw std::runtime_error("write_file trigger 'path' cannot be empty");
    }

    if (node.has_child("mode")) {
        std::string mode;
        node["mode"] >> mode;
        trigger.mode = parse_write_file_mode(mode);
    }

    return trigger;
}


WeaveNotifyTrigger parse_weave_notify_trigger(const ryml::ConstNodeRef& node) {
    WeaveNotifyTrigger trigger;
    if (node.is_val()) {
        std::string value;
        node >> value;
        std::string trimmed = trim_copy(value);
        const std::string prefix = "@weave";
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
            throw std::runtime_error("weave trigger requires 'message'");
        }
        node["message"] >> trigger.message;
    } else {
        throw std::runtime_error("weave trigger must be a string or map");
    }

    if (trigger.message.empty()) {
        throw std::runtime_error("weave trigger 'message' cannot be empty");
    }
    return trigger;
}RunTaskTrigger parse_run_task_trigger(const ryml::ConstNodeRef& node) {
    RunTaskTrigger trigger;
    if (node.is_val()) {
        node >> trigger.task_name;
    } else if (node.is_map()) {
        if (!node.has_child("task_name")) {
            throw std::runtime_error("run_task trigger requires 'task_name'");
        }
        node["task_name"] >> trigger.task_name;
    } else {
        throw std::runtime_error("run_task trigger must be a string or map");
    }

    if (trigger.task_name.empty()) {
        throw std::runtime_error("run_task trigger 'task_name' cannot be empty");
    }

    return trigger;
}

TriggerAction parse_trigger_action_node(const ryml::ConstNodeRef& node) {
    if (node.is_val()) {
        std::string scalar;
        node >> scalar;
        std::string trimmed = trim_copy(scalar);
        if (trimmed.rfind("@weave", 0) == 0) {
            return TriggerAction{parse_weave_notify_trigger(node)};
        }
        return TriggerAction{parse_run_task_trigger(node)};
    }

    if (!node.is_map()) {
        throw std::runtime_error("Trigger action must be a string or single-key map");
    }

    if (node.num_children() != 1) {
        throw std::runtime_error("Trigger action map must contain exactly one action");
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
        if (key == "weave" || key == "@weave") {
            return TriggerAction{parse_weave_notify_trigger(child)};
        }
        throw std::runtime_error("Unsupported trigger action: '" + key + "'");
    }

    throw std::runtime_error("Failed to parse trigger action");
}std::vector<TriggerAction> parse_trigger_action_list(const ryml::ConstNodeRef& node) {
    if (!node.is_seq()) {
        throw std::runtime_error("Trigger action list must be a sequence");
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
    } else if (node.is_val()) {
        std::string value;
        node >> value;
        if (!value.empty()) {
            result.push_back(value);
        }
    } else {
        throw std::runtime_error("Expected sequence or scalar when converting to string list");
    }
    return result;
}

Vars node_to_string_map(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw std::runtime_error("Expected mapping when converting to string map");
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
        throw std::runtime_error("retries must be a map");
    }

    RetryPolicy policy;
    if (node.has_child("count")) {
        node["count"] >> policy.count;
        if (policy.count < 0) {
            throw std::runtime_error("retry count cannot be negative");
        }
    }

    if (node.has_child("delay")) {
        node["delay"] >> policy.delay;
    }

    return policy;
}

Each parse_each(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw std::runtime_error("each must be a map");
    }

    Each each;
    if (node.has_child("items")) {
        each.items = node_to_string_vector(node["items"]);
    }

    if (node.has_child("matrix")) {
        const auto& matrix_node = node["matrix"];
        if (!matrix_node.is_map()) {
            throw std::runtime_error("each.matrix must be a map");
        }
        for (const auto& child : matrix_node) {
            std::string key(child.key().str, child.key().len);
            each.matrix[key] = node_to_string_vector(child);
        }
    }

    if (each.hasItems() && each.hasMatrix()) {
        throw std::runtime_error("each cannot define both 'items' and 'matrix'");
    }
    if (!each.hasItems() && !each.hasMatrix()) {
        throw std::runtime_error("each requires either 'items' or 'matrix'");
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
        throw std::runtime_error("triggers must be a map");
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
        throw std::runtime_error("triggers block must contain at least one action list");
    }

    return triggers;
}

RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node) {
    if (!node.has_child("command")) {
        throw std::runtime_error("run_command task requires a 'command'");
    }

    RunCommandParams params;
    const auto& command_node = node["command"];
    if (command_node.is_seq()) {
        StrList command_list = node_to_string_vector(command_node);
        if (command_list.empty()) {
            throw std::runtime_error("command array must contain at least one entry");
        }
        params.command = command_list;
    } else if (command_node.has_val()) {
        auto value = command_node.val();
        std::string command(value.str, value.len);
        if (command.empty()) {
            throw std::runtime_error("command string cannot be empty");
        }
        params.command = command;
    } else {
        std::string command;
        command_node >> command;
        if (command.empty()) {
            throw std::runtime_error("command string cannot be empty");
        }
        params.command = command;
    }

    if (node.has_child("output_format")) {
        std::string format;
        node["output_format"] >> format;
        params.output_format = parse_output_format(format);
    }

    return params;
}


ScriptParams parse_script_params(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw std::runtime_error("script must be a map");
    }
    if (!node.has_child("source")) {
        throw std::runtime_error("script requires a 'source'");
    }

    ScriptParams params;
    node["source"] >> params.source;
    if (params.source.empty()) {
        throw std::runtime_error("script source cannot be empty");
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
        throw std::runtime_error("uses value cannot be empty");
    }
    return params;
}


TaskDefaults parse_defaults(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        throw std::runtime_error("defaults must be a map");
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
        throw std::runtime_error("task must be a map");
    }

    Task task;

    if (!node.has_child("name")) {
        throw std::runtime_error("task requires a 'name'");
    }
    node["name"] >> task.name;
    if (task.name.empty()) {
        throw std::runtime_error("task name cannot be empty");
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

    if (action_count == 0) {
        throw std::runtime_error("task '" + task.name + "' must declare exactly one runner (command/script/uses)");
    }
    if (action_count > 1) {
        throw std::runtime_error("task '" + task.name + "' declares multiple runners");
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

