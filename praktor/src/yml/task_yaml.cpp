#include "task_yaml_internal.hpp"
#include "workflow_contract.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>


namespace {

WorkflowValue parse_contract_value(const TaskYamlDetail::YamlNodeRef& node) {
    if (node.is_map()) {
        WorkflowValue object = WorkflowValue::object();
        for (const auto& child : node) {
            object[child.key()] = parse_contract_value(child);
        }
        return object;
    }
    if (node.is_seq()) {
        WorkflowValue array = WorkflowValue::array();
        for (const auto& child : node) {
            array.push_back(parse_contract_value(child));
        }
        return array;
    }
    if (!node.has_val()) {
        TaskYamlDetail::throw_parse_error(node, "contract value must be a scalar, sequence, or map");
    }
    if (node.is_string_scalar()) {
        return WorkflowValue(node.scalar());
    }

    const std::string text = node.scalar();
    const std::string lower = TaskYamlDetail::to_lower_copy(text);
    if (lower == "null" || lower == "~") {
        return WorkflowValue::null();
    }
    if (lower == "true" || lower == "yes" || lower == "on") {
        return WorkflowValue(true);
    }
    if (lower == "false" || lower == "no" || lower == "off") {
        return WorkflowValue(false);
    }

    std::int64_t integer = 0;
    const auto parsed_integer =
        std::from_chars(text.data(), text.data() + text.size(), integer);
    if (parsed_integer.ec == std::errc{} &&
        parsed_integer.ptr == text.data() + text.size()) {
        return WorkflowValue(integer);
    }

    char* end = nullptr;
    const double number = std::strtod(text.c_str(), &end);
    if (end && end == text.c_str() + text.size()) {
        return WorkflowValue(number);
    }
    return WorkflowValue(text);
}

WorkflowContractField parse_contract_field(
    const TaskYamlDetail::YamlNodeRef& node,
    const std::string& name,
    bool output) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(
            node, "workflow contract field '" + name + "' must be a map");
    }

    std::unordered_set<std::string> allowed = {
        "type", "required", "default", "enum", "description"
    };
    if (output) {
        allowed.insert("value");
    }
    TaskYamlDetail::check_unknown_keys(node, allowed);

    if (!node.has_child("type")) {
        TaskYamlDetail::throw_parse_error(
            node, "workflow contract field '" + name + "' requires 'type'");
    }

    WorkflowContractField field;
    field.type = TaskYamlDetail::read_scalar_or_throw(
        node["type"], "workflow contract type must be a scalar");
    static const std::unordered_set<std::string> valid_types = {
        "string", "boolean", "integer", "number", "array", "object"
    };
    if (valid_types.find(field.type) == valid_types.end()) {
        TaskYamlDetail::throw_parse_error(
            node["type"], "unsupported workflow contract type '" + field.type + "'");
    }

    if (node.has_child("required")) {
        field.required =
            TaskYamlDetail::read_bool_or_throw(node["required"], "required");
    }
    if (node.has_child("description")) {
        field.description = TaskYamlDetail::read_scalar_or_throw(
            node["description"], "description must be a scalar");
    }
    if (node.has_child("value")) {
        field.value = TaskYamlDetail::read_scalar_or_throw(
            node["value"], "output value must be a scalar");
    }
    if (node.has_child("default")) {
        field.default_value = parse_contract_value(node["default"]);
        if (!Praktor::Contract::valueMatchesType(*field.default_value, field.type)) {
            TaskYamlDetail::throw_parse_error(
                node["default"], "default does not match declared type '" + field.type + "'");
        }
    }
    if (node.has_child("enum")) {
        const auto& values = node["enum"];
        if (!values.is_seq() || values.num_children() == 0) {
            TaskYamlDetail::throw_parse_error(
                values, "enum must be a non-empty sequence");
        }
        for (const auto& enum_node : values) {
            WorkflowValue value = parse_contract_value(enum_node);
            if (!Praktor::Contract::valueMatchesType(value, field.type)) {
                TaskYamlDetail::throw_parse_error(
                    enum_node, "enum value does not match declared type '" + field.type + "'");
            }
            field.enum_values.push_back(std::move(value));
        }
    }
    return field;
}

std::uint32_t parse_retry_count(
    const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(
            node, "'retries' must be a map with a finite count");
    }
    static const std::unordered_set<std::string> allowed = {"count"};
    TaskYamlDetail::check_unknown_keys(node, allowed);
    if (!node.has_child("count")) {
        TaskYamlDetail::throw_parse_error(
            node, "'retries' requires a non-negative integer 'count'");
    }
    const auto& count_node = node["count"];
    const std::string text = TaskYamlDetail::read_scalar_or_throw(
        count_node, "retry count must be a non-negative integer");
    std::uint64_t value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
        TaskYamlDetail::throw_parse_error(
            count_node, "retry count must be a non-negative 32-bit integer");
    }
    return static_cast<std::uint32_t>(value);
}

WorkflowContractFields parse_contract_fields(
    const TaskYamlDetail::YamlNodeRef& node,
    bool output) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(
            node, output ? "'outputs' must be a map" : "'inputs' must be a map");
    }

    WorkflowContractFields fields;
    for (const auto& child : node) {
        const std::string name = child.key();
        if (name.empty()) {
            TaskYamlDetail::throw_parse_error(child, "workflow contract field name cannot be empty");
        }
        fields.emplace(name, parse_contract_field(child, name, output));
    }
    return fields;
}

} // namespace

TaskDefaults parse_defaults(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "defaults must be a map");
    }

    static const std::unordered_set<std::string> allowed_default_keys = {"timeout"};
    TaskYamlDetail::check_unknown_keys(node, allowed_default_keys);

    TaskDefaults defaults;
    if (node.has_child("timeout")) {
        std::string timeout;
        node["timeout"] >> timeout;
        defaults.timeout = timeout;
    }
    return defaults;
}

Task parse_task(const TaskYamlDetail::YamlNodeRef& node, const std::string& source_path) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "task must be a map");
    }

    static const std::unordered_set<std::string> allowed_task_keys = {
        "name", "description", "depends_on", "vars", "env", "dotEnv", "when",
        "each", "timeout", "retries", "triggers",
        "working_dir", "silent", "sources", "generates", "finally",
        "command", "program", "args", "stdin", "download", "uses", "dynamic_tasks", "output_format",
        "service", "managed_process", "tool", "with",
        "script", "actions",
        "sequence", "parallel", "reactive_sequence", "pipeline_sequence",
        "inverter", "force_success", "force_failure", "repeat",
        "delay", "switch", "if", "then", "else", "while", "do", "max_iterations",
        "child", "cases",
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree",
        "fallback", "selector",
        "run_once", "keep_running_until_failure", "consume_queue", "precondition", "entry_updated"
    };
    if (node.has_child("set_variable")) {
        auto set_variable_keys = allowed_task_keys;
        set_variable_keys.insert("value");
        set_variable_keys.insert("from");
        TaskYamlDetail::check_unknown_keys(node, set_variable_keys);
    } else {
        TaskYamlDetail::check_unknown_keys(node, allowed_task_keys);
    }

    if (node.has_child("with") && !node.has_child("tool")) {
        TaskYamlDetail::throw_parse_error(
            node["with"], "'with' is valid only with the 'tool' runner");
    }

    Task task;

    if (!node.has_child("name")) {
        TaskYamlDetail::throw_parse_error(node, "task requires a 'name'");
    }
    node["name"] >> task.name;
    if (task.name.empty()) {
        TaskYamlDetail::throw_parse_error(node["name"], "task name cannot be empty");
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

    const bool is_flat_timeout_bt_root = node.has_child("timeout") && node.has_child("child");
    if (node.has_child("timeout") && node["timeout"].has_val() && !is_flat_timeout_bt_root) {
        std::string timeout;
        node["timeout"] >> timeout;
        task.timeout = timeout;
    }

    if (node.has_child("retries")) {
        task.retry_count = parse_retry_count(node["retries"]);
    }

    if (node.has_child("triggers")) {
        Triggers triggers = parse_triggers(node["triggers"]);
        if (!triggers.empty()) {
            task.triggers = triggers;
        }
    }

    if (node.has_child("working_dir")) {
        std::string working_dir;
        node["working_dir"] >> working_dir;
        if (!working_dir.empty()) {
            task.working_dir = working_dir;
        }
    }

    if (node.has_child("silent")) {
        task.silent = TaskYamlDetail::read_bool_or_throw(node["silent"], "silent");
    }

    if (node.has_child("sources")) {
        task.sources = node_to_string_vector(node["sources"]);
    }
    if (node.has_child("generates")) {
        task.generates = node_to_string_vector(node["generates"]);
    }

    if (node.has_child("finally")) {
        std::string finally_task;
        node["finally"] >> finally_task;
        if (finally_task.empty()) {
            TaskYamlDetail::throw_parse_error(node["finally"], "'finally' cannot be empty");
        }
        if (!task.triggers) {
            task.triggers = Triggers{};
        }
        auto& on_complete = task.triggers->on_complete;
        if (std::find(on_complete.begin(), on_complete.end(), finally_task) == on_complete.end()) {
            on_complete.push_back(finally_task);
        }
    }

    int action_count = 0;
    auto select_runner = [&](TaskAction action, const char* declared_runner, auto&& parser) {
        ++action_count;
        if (action_count != 1) {
            return;
        }
        task.action = action;
        task.declared_runner = declared_runner;
        task.specifics = parser();
    };

    if (node.has_child("command")) {
        select_runner(TaskAction::Orch, "command", [&] {
            return desugarCommandToorch(parse_run_command_params(node));
        });
    }

    if (node.has_child("actions")) {
        const auto& bt_node = node["actions"];
        if (!bt_node.is_map()) {
            TaskYamlDetail::throw_parse_error(bt_node, "orch node must be a map");
        }
        select_runner(TaskAction::Orch, "actions", [&] {
            OrchParams orch_params;
            orch_params.root = TaskYamlDetail::parse_orch_node(bt_node);
            return orch_params;
        });
    }

    if (node.has_child("program")) {
        select_runner(TaskAction::Program, "program", [&] { return parse_program_params(node); });
    }

    if (node.has_child("download")) {
        select_runner(TaskAction::Download, "download", [&] {
            return parse_download_params(node);
        });
    }

    if (node.has_child("service")) {
        select_runner(TaskAction::Service, "service", [&] { return parse_service_params(node); });
    }

    if (node.has_child("managed_process")) {
        select_runner(TaskAction::ManagedProcess, "managed_process", [&] {
            return parse_managed_process_params(node);
        });
    }

    if (node.has_child("uses")) {
        select_runner(TaskAction::Uses, "uses", [&] { return parse_uses_params(node["uses"]); });
    }

    if (node.has_child("dynamic_tasks")) {
        select_runner(TaskAction::DynamicTasks, "dynamic_tasks", [&] {
            return parse_dynamic_tasks_params(node["dynamic_tasks"]);
        });
    }

    if (node.has_child("tool")) {
        select_runner(TaskAction::HostTool, "tool", [&] {
            HostToolParams params;
            params.tool = TaskYamlDetail::read_system_action_string_or_throw(
                node["tool"], "'tool' must be a string stable host-tool identity");
            if (params.tool.empty()) {
                TaskYamlDetail::throw_parse_error(
                    node["tool"], "'tool' identity cannot be empty");
            }
            if (node.has_child("with")) {
                if (!node["with"].is_map()) {
                    TaskYamlDetail::throw_parse_error(
                        node["with"], "'with' must be a map of typed host-tool arguments");
                }
                params.arguments = parse_contract_value(node["with"]);
            }
            return params;
        });
    }

    static const std::vector<std::string> orch_control_nodes = {
        "sequence", "parallel", "reactive_sequence", "pipeline_sequence", "fallback", "selector"
    };
    static const std::vector<std::string> orch_leaf_nodes = {
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree"
    };
    static const std::unordered_set<std::string> command_parser_keys = {
        "parse_json", "parse_regex", "parse_lines", "parse_keyvalue"
    };
    static const std::vector<std::string> orch_decorator_nodes = {
        "inverter", "force_success", "force_failure", "repeat",
        "timeout", "delay", "run_once", "keep_running_until_failure", "consume_queue",
        "precondition", "entry_updated"
    };
    static const std::vector<std::string> orch_advanced_nodes = {"switch", "if", "while"};

    for (const auto& bt_type : orch_control_nodes) {
        if (node.has_child(bt_type.c_str())) {
            select_runner(TaskAction::Orch, "actions", [&] {
                return parse_orch_params(node, bt_type);
            });
        }
    }

    for (const auto& bt_type : orch_leaf_nodes) {
        const bool is_command_parser = node.has_child("command") &&
                                       command_parser_keys.find(bt_type) != command_parser_keys.end();
        if (node.has_child(bt_type.c_str()) && !is_command_parser) {
            select_runner(TaskAction::Orch, "actions", [&] {
                return parse_orch_params(node, bt_type);
            });
        }
    }

    for (const auto& bt_type : orch_decorator_nodes) {
        const bool has_flat_child_form = node.has_child(bt_type.c_str()) && node.has_child("child");
        const bool matches_root =
            (bt_type == "timeout" && has_flat_child_form) ||
            ((bt_type == "repeat" || bt_type == "delay") && has_flat_child_form) ||
            ((bt_type == "keep_running_until_failure" || bt_type == "precondition" ||
              bt_type == "entry_updated" || bt_type == "consume_queue") && has_flat_child_form) ||
            (bt_type == "run_once" && has_flat_child_form) ||
            ((bt_type == "inverter" || bt_type == "force_success" || bt_type == "force_failure") &&
             node.has_child(bt_type.c_str()));
        if (matches_root) {
            select_runner(TaskAction::Orch, "actions", [&] {
                return parse_orch_params(node, bt_type);
            });
        }
    }

    for (const auto& bt_type : orch_advanced_nodes) {
        const bool matches_root =
            (bt_type == "if" && node.has_child("if") && node.has_child("then")) ||
            (bt_type == "while" && node.has_child("while") && node.has_child("do")) ||
            (bt_type == "switch" && node.has_child("switch") && node.has_child("cases"));
        if (matches_root) {
            select_runner(TaskAction::Orch, "actions", [&] {
                return parse_orch_params(node, bt_type);
            });
        }
    }

    if (action_count > 1) {
        TaskYamlDetail::throw_parse_error(node, "task '" + task.name + "' declares multiple runners");
    }
    if (node.has_child("retries") && task.action != TaskAction::HostTool) {
        TaskYamlDetail::throw_parse_error(
            node["retries"], "'retries' is currently supported only with the 'tool' runner");
    }

    if (node.has_child("script")) {
        std::string script_src = TaskYamlDetail::read_scalar_or_throw(
            node["script"], "'script' must be a scalar");
        if (script_src.empty()) {
            TaskYamlDetail::throw_parse_error(node["script"], "'script' cannot be empty");
        }
        task.script = std::move(script_src);
        if (action_count == 0) {
            task.declared_runner = "script";
        }
    }

    if (action_count == 0 && !task.script) {
        TaskYamlDetail::throw_parse_error(
            node, "task '" + task.name +
                      "' must declare a runner (command/program/download/service/managed_process/uses/dynamic_tasks/tool) or script");
    }

    task.source_path = source_path;
    return task;
}

Workflow parse_workflow(const TaskYamlDetail::YamlNodeRef& node, const std::string& source_path) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "workflow must be a map");
    }

    static const std::unordered_set<std::string> allowed_workflow_keys = {
        "name", "description", "inputs", "outputs", "input_policy",
        "variables", "env", "dotEnv", "defaults", "includes", "tasks"
    };
    TaskYamlDetail::check_unknown_keys(node, allowed_workflow_keys);

    Workflow workflow;

    if (node.has_child("name")) {
        node["name"] >> workflow.name;
    }
    if (node.has_child("description")) {
        node["description"] >> workflow.description;
    }
    if (node.has_child("inputs")) {
        workflow.inputs = parse_contract_fields(node["inputs"], false);
    }
    if (node.has_child("outputs")) {
        workflow.outputs = parse_contract_fields(node["outputs"], true);
    }
    if (node.has_child("input_policy")) {
        const std::string policy = TaskYamlDetail::read_scalar_or_throw(
            node["input_policy"], "input_policy must be a scalar");
        if (policy == "strict") {
            workflow.strict_inputs = true;
        } else if (policy == "allow_extra") {
            workflow.strict_inputs = false;
        } else {
            TaskYamlDetail::throw_parse_error(
                node["input_policy"],
                "input_policy must be 'strict' or 'allow_extra'");
        }
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

    if (node.has_child("defaults")) {
        workflow.defaults = parse_defaults(node["defaults"]);
    }

    if (!node.has_child("tasks")) {
        TaskYamlDetail::throw_parse_error(node, "workflow must define a 'tasks' list");
    }

    const auto& tasks_node = node["tasks"];
    if (!tasks_node.is_seq()) {
        TaskYamlDetail::throw_parse_error(tasks_node, "'tasks' must be a sequence");
    }

    for (const auto& task_node : tasks_node) {
        workflow.tasks.push_back(parse_task(task_node, source_path));
    }

    if (workflow.tasks.empty()) {
        TaskYamlDetail::throw_parse_error(tasks_node, "workflow must define at least one task");
    }

    return workflow;
}
