#include "task_yaml_internal.hpp"

#include <string>
#include <unordered_set>
#include <variant>

namespace {

using namespace TaskYamlDetail;

CommandOutputFormat parse_output_format(const TaskYamlDetail::YamlNodeRef& node, const std::string& value) {
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

SystemOperation parse_system_operation(const TaskYamlDetail::YamlNodeRef& node,
                                       const std::string& value,
                                       const char* field_name) {
    if (value == "status") {
        return SystemOperation::Status;
    }
    if (value == "start") {
        return SystemOperation::Start;
    }
    if (value == "stop") {
        return SystemOperation::Stop;
    }
    if (value == "restart") {
        return SystemOperation::Restart;
    }
    throw_parse_error(node, "Unsupported " + std::string(field_name) + " value: '" + value + "'");
}

void require_positive_timeout(const TaskYamlDetail::YamlNodeRef& node,
                              int value,
                              const char* field_name) {
    if (value <= 0) {
        throw_parse_error(node, std::string(field_name) + " must be positive");
    }
}

StrList parse_argument_list(const TaskYamlDetail::YamlNodeRef& node, const char* field_name) {
    if (!node.is_seq()) {
        throw_parse_error(node, std::string(field_name) + " must be a sequence");
    }

    StrList arguments;
    for (const auto& argument : node) {
        arguments.push_back(read_system_action_string_or_throw(
            argument, std::string(field_name) + " entries must be strings"));
    }
    return arguments;
}

TriggerAction parse_trigger_action_node(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.has_val()) {
        throw_parse_error(node, "Trigger action must be a task name (string)");
    }

    std::string task_name;
    node >> task_name;

    if (task_name.empty()) {
        throw_parse_error(node, "Trigger action task name cannot be empty");
    }

    return task_name;
}

std::vector<TriggerAction> parse_trigger_action_list(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_seq()) {
        throw_parse_error(node, "Trigger action list must be a sequence");
    }

    std::vector<TriggerAction> actions;
    for (const auto& item : node) {
        actions.emplace_back(parse_trigger_action_node(item));
    }
    if (actions.empty()) {
        throw_parse_error(node, "Trigger action list must contain at least one task name");
    }
    return actions;
}

} // namespace

StrList node_to_string_vector(const TaskYamlDetail::YamlNodeRef& node) {
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
        TaskYamlDetail::throw_parse_error(node, "Expected sequence or scalar when converting to string list");
    }
    return result;
}

Vars node_to_string_map(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "Expected mapping when converting to string map");
    }

    Vars result;
    for (const auto& child : node) {
        std::string key = child.key();
        std::string value;
        child >> value;
        result[key] = value;
    }
    return result;
}

Each parse_each(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "each must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {
        "items", "matrix", "as", "index_variable"
    };
    TaskYamlDetail::check_unknown_keys(node, allowed_keys);

    Each each;
    if (node.has_child("items")) {
        each.items = node_to_string_vector(node["items"]);
    }

    if (node.has_child("matrix")) {
        const auto& matrix_node = node["matrix"];
        if (!matrix_node.is_map()) {
            TaskYamlDetail::throw_parse_error(matrix_node, "each.matrix must be a map");
        }
        for (const auto& child : matrix_node) {
            std::string key = child.key();
            each.matrix[key] = node_to_string_vector(child);
        }
    }

    if (each.hasItems() && each.hasMatrix()) {
        TaskYamlDetail::throw_parse_error(node, "each cannot define both 'items' and 'matrix'");
    }
    if (!each.hasItems() && !each.hasMatrix()) {
        TaskYamlDetail::throw_parse_error(node, "each requires either 'items' or 'matrix'");
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

Triggers parse_triggers(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "triggers must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {
        "on_success", "on_failure", "on_complete"
    };
    TaskYamlDetail::check_unknown_keys(node, allowed_keys);

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
        TaskYamlDetail::throw_parse_error(node, "triggers block must contain at least one action list");
    }

    return triggers;
}

RunCommandParams parse_run_command_params(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.has_child("command")) {
        TaskYamlDetail::throw_parse_error(node, "run_command task requires a 'command'");
    }

    const char* selected_parser = nullptr;
    for (const char* parser_key : {"parse_regex", "parse_json", "parse_lines", "parse_keyvalue"}) {
        if (!node.has_child(parser_key)) {
            continue;
        }
        if (selected_parser) {
            TaskYamlDetail::throw_parse_error(
                node[parser_key], "command task cannot combine '" + std::string(selected_parser) +
                                      "' and '" + parser_key + "'");
        }
        selected_parser = parser_key;
    }

    RunCommandParams params;
    const auto& command_node = node["command"];
    if (command_node.is_seq()) {
        StrList command_list = node_to_string_vector(command_node);
        if (command_list.empty()) {
            TaskYamlDetail::throw_parse_error(command_node, "command array must contain at least one entry");
        }
        for (const auto& command : command_list) {
            if (command.empty()) {
                TaskYamlDetail::throw_parse_error(
                    command_node, "command array entries cannot be empty");
            }
        }
        params.command = command_list;
    } else if (command_node.has_val()) {
        std::string command = command_node.scalar();
        if (command.empty()) {
            TaskYamlDetail::throw_parse_error(command_node, "command string cannot be empty");
        }
        params.command = command;
    } else {
        TaskYamlDetail::throw_parse_error(command_node, "command must be a scalar or sequence");
    }

    if (node.has_child("output_format")) {
        std::string format;
        node["output_format"] >> format;
        params.output_format = parse_output_format(node["output_format"], format);
    }

    if (node.has_child("parse_regex")) {
        const auto& pr = node["parse_regex"];
        if (!pr.is_map()) {
            TaskYamlDetail::throw_parse_error(pr, "parse_regex must be a map");
        }
        static const std::unordered_set<std::string> allowed_keys = {
            "pattern", "capture_group", "output_key"
        };
        TaskYamlDetail::check_unknown_keys(pr, allowed_keys);

        ParseRegexConfig config;
        if (!pr.has_child("pattern")) {
            TaskYamlDetail::throw_parse_error(pr, "parse_regex requires 'pattern' field");
        }
        pr["pattern"] >> config.pattern;

        if (pr.has_child("capture_group")) {
            pr["capture_group"] >> config.capture_group;
        }
        if (!pr.has_child("output_key")) {
            TaskYamlDetail::throw_parse_error(pr, "parse_regex requires 'output_key' field");
        }
        pr["output_key"] >> config.output_key;
        params.parse_regex = config;
    }

    if (node.has_child("parse_json")) {
        const auto& pj = node["parse_json"];
        if (!pj.is_map()) {
            TaskYamlDetail::throw_parse_error(pj, "parse_json must be a map");
        }
        static const std::unordered_set<std::string> allowed_keys = {"path", "output_key"};
        TaskYamlDetail::check_unknown_keys(pj, allowed_keys);

        ParseJsonConfig config;
        if (!pj.has_child("path")) {
            TaskYamlDetail::throw_parse_error(pj, "parse_json requires 'path' field");
        }
        pj["path"] >> config.path;

        if (!pj.has_child("output_key")) {
            TaskYamlDetail::throw_parse_error(pj, "parse_json requires 'output_key' field");
        }
        pj["output_key"] >> config.output_key;
        params.parse_json = config;
    }

    if (node.has_child("parse_lines")) {
        const auto& pl = node["parse_lines"];
        if (!pl.is_map()) {
            TaskYamlDetail::throw_parse_error(pl, "parse_lines must be a map");
        }
        static const std::unordered_set<std::string> allowed_keys = {"filter", "output_key"};
        TaskYamlDetail::check_unknown_keys(pl, allowed_keys);

        ParseLinesConfig config;
        if (pl.has_child("filter")) {
            pl["filter"] >> config.filter;
        }
        if (!pl.has_child("output_key")) {
            TaskYamlDetail::throw_parse_error(pl, "parse_lines requires 'output_key' field");
        }
        pl["output_key"] >> config.output_key;
        params.parse_lines = config;
    }

    if (node.has_child("parse_keyvalue")) {
        const auto& pkv = node["parse_keyvalue"];
        if (!pkv.is_map()) {
            TaskYamlDetail::throw_parse_error(pkv, "parse_keyvalue must be a map");
        }
        static const std::unordered_set<std::string> allowed_keys = {
            "delimiter", "line_separator", "output_key"
        };
        TaskYamlDetail::check_unknown_keys(pkv, allowed_keys);

        ParseKeyValueConfig config;
        if (pkv.has_child("delimiter")) {
            pkv["delimiter"] >> config.delimiter;
        }
        if (pkv.has_child("line_separator")) {
            pkv["line_separator"] >> config.line_separator;
        }
        if (!pkv.has_child("output_key")) {
            TaskYamlDetail::throw_parse_error(pkv, "parse_keyvalue requires 'output_key' field");
        }
        pkv["output_key"] >> config.output_key;
        params.parse_keyvalue = config;
    }

    return params;
}

ProgramParams parse_program_params(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.has_child("program")) {
        TaskYamlDetail::throw_parse_error(node, "program task requires a 'program'");
    }

    ProgramParams params;
    const auto& program_node = node["program"];
    if (!program_node.has_val()) {
        TaskYamlDetail::throw_parse_error(program_node, "program must be a scalar");
    }
    program_node >> params.program;
    if (params.program.empty()) {
        TaskYamlDetail::throw_parse_error(program_node, "program cannot be empty");
    }

    if (node.has_child("args")) {
        params.args = node_to_string_vector(node["args"]);
    }

    if (node.has_child("stdin")) {
        node["stdin"] >> params.input;
    }

    if (node.has_child("output_format")) {
        std::string format;
        node["output_format"] >> format;
        params.output_format = parse_output_format(node["output_format"], format);
    }

    return params;
}

DownloadParams parse_download_params(const TaskYamlDetail::YamlNodeRef& node) {
    const auto& download = node["download"];
    if (!download.is_map()) {
        TaskYamlDetail::throw_parse_error(download, "download must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {
        "url", "path", "sha256", "overwrite", "timeout_ms"
    };
    TaskYamlDetail::check_unknown_keys(download, allowed_keys);
    if (!download.has_child("url")) {
        TaskYamlDetail::throw_parse_error(download, "download requires 'url'");
    }
    if (!download.has_child("path")) {
        TaskYamlDetail::throw_parse_error(download, "download requires 'path'");
    }

    DownloadParams params;
    params.url = TaskYamlDetail::read_scalar_or_throw(
        download["url"], "download.url must be a scalar");
    params.path = TaskYamlDetail::read_scalar_or_throw(
        download["path"], "download.path must be a scalar");
    if (params.url.empty()) {
        TaskYamlDetail::throw_parse_error(download["url"], "download.url cannot be empty");
    }
    if (params.path.empty()) {
        TaskYamlDetail::throw_parse_error(download["path"], "download.path cannot be empty");
    }
    if (download.has_child("sha256")) {
        params.sha256 = TaskYamlDetail::read_scalar_or_throw(
            download["sha256"], "download.sha256 must be a scalar");
    }
    if (download.has_child("overwrite")) {
        params.overwrite =
            TaskYamlDetail::read_bool_or_throw(download["overwrite"], "download.overwrite");
    }
    if (download.has_child("timeout_ms")) {
        download["timeout_ms"] >> params.timeout_ms;
        if (params.timeout_ms <= 0) {
            TaskYamlDetail::throw_parse_error(download["timeout_ms"],
                                               "download.timeout_ms must be positive");
        }
    }
    return params;
}

ServiceParams parse_service_params(const TaskYamlDetail::YamlNodeRef& node) {
    const auto& service = node["service"];
    if (!service.is_map()) {
        TaskYamlDetail::throw_parse_error(service, "service must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {
        "operation", "name", "profile", "arguments", "timeout_ms", "poll_interval_ms"
    };
    TaskYamlDetail::check_unknown_keys(service, allowed_keys);
    if (!service.has_child("name")) {
        TaskYamlDetail::throw_parse_error(service, "service requires 'name'");
    }

    ServiceParams params;
    params.name = TaskYamlDetail::read_system_action_string_or_throw(
        service["name"], "service.name must be a string");
    if (params.name.empty()) {
        TaskYamlDetail::throw_parse_error(service["name"], "service.name cannot be empty");
    }
    if (service.has_child("operation")) {
        const std::string operation = TaskYamlDetail::read_system_action_string_or_throw(
            service["operation"], "service.operation must be a string");
        params.operation = parse_system_operation(service["operation"], operation, "service.operation");
    }
    if (service.has_child("profile")) {
        params.profile = TaskYamlDetail::read_system_action_string_or_throw(
            service["profile"], "service.profile must be a string");
        if (params.profile.empty()) {
            TaskYamlDetail::throw_parse_error(service["profile"], "service.profile cannot be empty");
        }
    }
    if (service.has_child("arguments")) {
        params.arguments = parse_argument_list(service["arguments"], "service.arguments");
    }
    if (service.has_child("timeout_ms")) {
        service["timeout_ms"] >> params.timeout_ms;
        require_positive_timeout(service["timeout_ms"], params.timeout_ms, "service.timeout_ms");
    }
    if (service.has_child("poll_interval_ms")) {
        service["poll_interval_ms"] >> params.poll_interval_ms;
        require_positive_timeout(service["poll_interval_ms"], params.poll_interval_ms,
                                 "service.poll_interval_ms");
    }
    return params;
}

ManagedProcessParams parse_managed_process_params(const TaskYamlDetail::YamlNodeRef& node) {
    const auto& process = node["managed_process"];
    if (!process.is_map()) {
        TaskYamlDetail::throw_parse_error(process, "managed_process must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {
        "operation", "executable", "arguments", "working_directory", "identity",
        "startup_timeout_ms", "stop_timeout_ms", "force_terminate"
    };
    static const std::unordered_set<std::string> allowed_identity_keys = {"image_name"};
    TaskYamlDetail::check_unknown_keys(process, allowed_keys);
    if (!process.has_child("identity")) {
        TaskYamlDetail::throw_parse_error(process, "managed_process requires 'identity'");
    }

    const auto& identity = process["identity"];
    if (!identity.is_map()) {
        TaskYamlDetail::throw_parse_error(identity, "managed_process.identity must be a map");
    }
    TaskYamlDetail::check_unknown_keys(identity, allowed_identity_keys);
    if (!identity.has_child("image_name")) {
        TaskYamlDetail::throw_parse_error(identity, "managed_process.identity requires 'image_name'");
    }

    ManagedProcessParams params;
    params.identity.image_name = TaskYamlDetail::read_system_action_string_or_throw(
        identity["image_name"], "managed_process.identity.image_name must be a string");
    if (params.identity.image_name.empty()) {
        TaskYamlDetail::throw_parse_error(identity["image_name"],
                                           "managed_process.identity.image_name cannot be empty");
    }
    if (process.has_child("operation")) {
        const std::string operation = TaskYamlDetail::read_system_action_string_or_throw(
            process["operation"], "managed_process.operation must be a string");
        params.operation = parse_system_operation(process["operation"], operation,
                                                  "managed_process.operation");
    }
    if (process.has_child("executable")) {
        params.executable = TaskYamlDetail::read_system_action_string_or_throw(
            process["executable"], "managed_process.executable must be a string");
    }
    if (params.operation == SystemOperation::Start && params.executable.empty()) {
        TaskYamlDetail::throw_parse_error(process, "managed_process.start requires 'executable'");
    }
    if (params.operation == SystemOperation::Restart && params.executable.empty()) {
        TaskYamlDetail::throw_parse_error(process,
                                          "managed_process.restart requires 'executable'");
    }
    if (process.has_child("arguments")) {
        params.arguments = parse_argument_list(process["arguments"], "managed_process.arguments");
    }
    if (process.has_child("working_directory")) {
        params.working_directory = TaskYamlDetail::read_system_action_string_or_throw(
            process["working_directory"], "managed_process.working_directory must be a string");
        if (params.working_directory.empty()) {
            TaskYamlDetail::throw_parse_error(process["working_directory"],
                                               "managed_process.working_directory cannot be empty");
        }
    }
    if (process.has_child("startup_timeout_ms")) {
        process["startup_timeout_ms"] >> params.startup_timeout_ms;
        require_positive_timeout(process["startup_timeout_ms"], params.startup_timeout_ms,
                                 "managed_process.startup_timeout_ms");
    }
    if (process.has_child("stop_timeout_ms")) {
        process["stop_timeout_ms"] >> params.stop_timeout_ms;
        require_positive_timeout(process["stop_timeout_ms"], params.stop_timeout_ms,
                                 "managed_process.stop_timeout_ms");
    }
    if (process.has_child("force_terminate")) {
        params.force_terminate = TaskYamlDetail::read_force_terminate_or_throw(
            process["force_terminate"]);
    }
    return params;
}

UsesParams parse_uses_params(const TaskYamlDetail::YamlNodeRef& node) {
    UsesParams params;
    if (node.has_val()) {
        params.path = node.scalar();
    } else {
        node >> params.path;
    }
    if (params.path.empty()) {
        TaskYamlDetail::throw_parse_error(node, "uses value cannot be empty");
    }
    return params;
}

DynamicTasksParams parse_dynamic_tasks_params(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "dynamic_tasks must be a map");
    }

    static const std::unordered_set<std::string> allowed_keys = {"items_variable", "template"};
    TaskYamlDetail::check_unknown_keys(node, allowed_keys);

    DynamicTasksParams params;

    if (!node.has_child("items_variable")) {
        TaskYamlDetail::throw_parse_error(node, "dynamic_tasks requires 'items_variable'");
    }
    node["items_variable"] >> params.items_variable;
    if (params.items_variable.empty()) {
        TaskYamlDetail::throw_parse_error(node["items_variable"],
                                          "dynamic_tasks 'items_variable' cannot be empty");
    }

    if (!node.has_child("template")) {
        TaskYamlDetail::throw_parse_error(node, "dynamic_tasks requires 'template'");
    }
    const auto& tmpl_node = node["template"];
    if (!tmpl_node.is_map()) {
        TaskYamlDetail::throw_parse_error(tmpl_node, "dynamic_tasks 'template' must be a map");
    }
    static const std::unordered_set<std::string> allowed_template_keys = {
        "name", "command", "timeout", "when", "depends_on", "env"
    };
    TaskYamlDetail::check_unknown_keys(tmpl_node, allowed_template_keys);

    if (!tmpl_node.has_child("name")) {
        TaskYamlDetail::throw_parse_error(tmpl_node, "dynamic_tasks template requires 'name'");
    }
    tmpl_node["name"] >> params.task_template.name;
    if (params.task_template.name.empty()) {
        TaskYamlDetail::throw_parse_error(tmpl_node["name"],
                                          "dynamic_tasks template 'name' cannot be empty");
    }

    if (!tmpl_node.has_child("command")) {
        TaskYamlDetail::throw_parse_error(tmpl_node, "dynamic_tasks template requires 'command'");
    }
    const auto& cmd_node = tmpl_node["command"];
    if (cmd_node.is_seq()) {
        params.task_template.command = node_to_string_vector(cmd_node);
        const auto& command_list = std::get<StrList>(params.task_template.command);
        if (command_list.empty()) {
            TaskYamlDetail::throw_parse_error(cmd_node,
                                              "dynamic_tasks template 'command' cannot be empty");
        }
        for (const auto& command : command_list) {
            if (command.empty()) {
                TaskYamlDetail::throw_parse_error(
                    cmd_node, "dynamic_tasks template command entries cannot be empty");
            }
        }
    } else {
        std::string cmd;
        cmd_node >> cmd;
        if (cmd.empty()) {
            TaskYamlDetail::throw_parse_error(cmd_node,
                                              "dynamic_tasks template 'command' cannot be empty");
        }
        params.task_template.command = cmd;
    }

    if (tmpl_node.has_child("timeout")) {
        std::string timeout;
        tmpl_node["timeout"] >> timeout;
        params.task_template.timeout = timeout;
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

OrchParams desugarCommandToorch(const RunCommandParams& params) {
    OrchParams bt;
    bt.root.type = "Sequence";

    if (std::holds_alternative<StrList>(params.command)) {
        const auto& cmds = std::get<StrList>(params.command);
        for (size_t i = 0; i < cmds.size(); ++i) {
            OrchNode shell_node;
            shell_node.type = "Shell";
            shell_node.params["cmd"] = cmds[i];
            if (i == cmds.size() - 1) {
                shell_node.params["output_key"] = "stdout";
            }
            bt.root.children.push_back(std::move(shell_node));
        }
    } else {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = std::get<std::string>(params.command);
        shell_node.params["output_key"] = "stdout";
        bt.root.children.push_back(std::move(shell_node));
    }

    if (params.parse_regex) {
        OrchNode parse_node;
        parse_node.type = "ParseRegex";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["pattern"] = params.parse_regex->pattern;
        parse_node.params["capture_group"] = std::to_string(params.parse_regex->capture_group);
        parse_node.params["output_key"] = params.parse_regex->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_json) {
        OrchNode parse_node;
        parse_node.type = "ParseJson";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["path"] = params.parse_json->path;
        parse_node.params["output_key"] = params.parse_json->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_lines) {
        OrchNode parse_node;
        parse_node.type = "ParseLines";
        parse_node.params["input_key"] = "stdout";
        if (!params.parse_lines->filter.empty()) {
            parse_node.params["filter"] = params.parse_lines->filter;
        }
        parse_node.params["output_key"] = params.parse_lines->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_keyvalue) {
        OrchNode parse_node;
        parse_node.type = "ParseKeyValue";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["delimiter"] = params.parse_keyvalue->delimiter;
        parse_node.params["line_separator"] = params.parse_keyvalue->line_separator;
        parse_node.params["output_key"] = params.parse_keyvalue->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.output_format == CommandOutputFormat::Json) {
        OrchNode json_node;
        json_node.type = "ParseJson";
        json_node.params["input_key"] = "stdout";
        json_node.params["path"] = "$";
        json_node.params["output_key"] = "data";
        bt.root.children.push_back(std::move(json_node));
    }

    return bt;
}
