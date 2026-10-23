#include "task_yaml_internal.hpp"

#include <memory>
#include <string>
#include <unordered_set>
#include <variant>

namespace {

using namespace TaskYamlDetail;

struct ActionParamsDeleter {
    void operator()(praktor_yaml_action_params* owner) const noexcept { praktor_yaml_action_params_destroy(owner); }
};
using ActionParamsOwner = std::unique_ptr<praktor_yaml_action_params, ActionParamsDeleter>;
void require_native_params(int status) {
    if (status != SALTS_OK) throw std::logic_error("Invalid native YAML action parameter projection");
}
std::string owned_text(vstr text) { return std::string(text.data, text.len); }
StrList owned_arguments(const praktor_yaml_action_params* owner, std::size_t count) {
    StrList result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        vstr text{};
        require_native_params(praktor_yaml_action_argument(owner, index, &text));
        result.push_back(owned_text(text));
    }
    return result;
}
static_assert(static_cast<int>(CommandOutputFormat::Text) == PRAKTOR_YAML_OUTPUT_TEXT);
static_assert(static_cast<int>(CommandOutputFormat::Json) == PRAKTOR_YAML_OUTPUT_JSON);
static_assert(static_cast<int>(SystemOperation::Status) == PRAKTOR_PROCESS_STATUS);
static_assert(static_cast<int>(SystemOperation::Start) == PRAKTOR_PROCESS_START);
static_assert(static_cast<int>(SystemOperation::Stop) == PRAKTOR_PROCESS_STOP);
static_assert(static_cast<int>(SystemOperation::Restart) == PRAKTOR_PROCESS_RESTART);

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
            if (each.matrix[key].empty()) {
                TaskYamlDetail::throw_parse_error(
                    child, "each.matrix dimension '" + key + "' must not be empty");
            }
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
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_COMMAND));
    praktor_yaml_command_view view{};
    require_native_params(praktor_yaml_action_command(owner.get(), &view));
    RunCommandParams result;
    if (view.sequence) result.command = owned_arguments(owner.get(), view.command_count);
    else result.command = owned_text(view.command);
    result.output_format = static_cast<CommandOutputFormat>(view.output_format);
    switch (view.postprocessor) {
    case PRAKTOR_YAML_POST_REGEX:
        result.parse_regex = ParseRegexConfig{owned_text(view.parser.regex.pattern), view.parser.regex.capture_group, owned_text(view.parser.regex.output_key)}; break;
    case PRAKTOR_YAML_POST_JSON:
        result.parse_json = ParseJsonConfig{owned_text(view.parser.json.path), owned_text(view.parser.json.output_key)}; break;
    case PRAKTOR_YAML_POST_LINES:
        result.parse_lines = ParseLinesConfig{owned_text(view.parser.lines.filter), owned_text(view.parser.lines.output_key)}; break;
    case PRAKTOR_YAML_POST_KEYVALUE:
        result.parse_keyvalue = ParseKeyValueConfig{owned_text(view.parser.keyvalue.delimiter), owned_text(view.parser.keyvalue.line_separator), owned_text(view.parser.keyvalue.output_key)}; break;
    case PRAKTOR_YAML_POST_NONE: break;
    }
    return result;
}
ProgramParams parse_program_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_PROGRAM));
    praktor_yaml_program_view view{};
    require_native_params(praktor_yaml_action_program(owner.get(), &view));
    ProgramParams result;
    result.program = owned_text(view.program); result.input = owned_text(view.input);
    result.args = owned_arguments(owner.get(), view.argument_count);
    result.output_format = static_cast<CommandOutputFormat>(view.output_format);
    return result;
}

// Temporary STL conversion only. Validation/defaults/owned strings live in C.
DownloadParams parse_download_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_DOWNLOAD));
    praktor_yaml_download_view view{};
    require_native_params(praktor_yaml_action_download(owner.get(), &view));
    DownloadParams result;
    result.url = owned_text(view.url); result.path = owned_text(view.path); result.sha256 = owned_text(view.sha256);
    result.overwrite = view.overwrite; result.timeout_ms = view.timeout_ms;
    return result;
}
ServiceParams parse_service_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_SERVICE));
    praktor_yaml_service_view view{};
    require_native_params(praktor_yaml_action_service(owner.get(), &view));
    ServiceParams result;
    result.operation = static_cast<SystemOperation>(view.operation);
    result.name = owned_text(view.name); result.profile = owned_text(view.profile);
    result.arguments = owned_arguments(owner.get(), view.argument_count);
    result.timeout_ms = view.timeout_ms; result.poll_interval_ms = view.poll_interval_ms;
    return result;
}
ManagedProcessParams parse_managed_process_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_MANAGED_PROCESS));
    praktor_yaml_managed_process_view view{};
    require_native_params(praktor_yaml_action_managed_process(owner.get(), &view));
    ManagedProcessParams result;
    result.operation = static_cast<SystemOperation>(view.operation);
    result.identity.image_name = owned_text(view.image_name); result.executable = owned_text(view.executable);
    result.working_directory = owned_text(view.working_directory);
    result.arguments = owned_arguments(owner.get(), view.argument_count);
    result.startup_timeout_ms = view.startup_timeout_ms; result.stop_timeout_ms = view.stop_timeout_ms;
    result.force_terminate = view.force_terminate;
    return result;
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
