#include "task_yaml_internal.hpp"
#include "workflow_contract.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>


namespace {

WorkflowValue parse_contract_value(const TaskYamlDetail::YamlNodeRef& node) {
    return WorkflowValue::adoptJson(TaskYamlDetail::read_contract_json_or_throw(node));
}

WorkflowContractFields parse_contract_fields(
    const TaskYamlDetail::YamlNodeRef& node,
    bool output) {
    WorkflowValue model = WorkflowValue::adoptJson(
        TaskYamlDetail::read_workflow_contract_fields_or_throw(node, output));
    WorkflowContractFields fields;
    for (const auto& item : model.object_range()) {
        const WorkflowValue& encoded = item.value();
        WorkflowContractField field;
        field.type = encoded.at("type").as<std::string>();
        if (encoded.contains("required")) field.required = encoded.at("required").as<bool>();
        if (encoded.contains("description")) field.description = encoded.at("description").as<std::string>();
        if (encoded.contains("value")) field.value = encoded.at("value").as<std::string>();
        if (encoded.contains("default")) field.default_value = encoded.at("default");
        if (encoded.contains("enum")) {
            for (const auto& value : encoded.at("enum").array_range())
                field.enum_values.push_back(value);
        }
        fields.emplace(item.key(), std::move(field));
    }
    return fields;
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
        value > static_cast<std::uint64_t>(kPraktorHostToolMaxRetries)) {
        TaskYamlDetail::throw_parse_error(
            count_node,
            "retry count must be between 0 and " +
                std::to_string(kPraktorHostToolMaxRetries));
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

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

    WorkflowValue metadata = WorkflowValue::adoptJson(
        TaskYamlDetail::read_workflow_metadata_or_throw(node));
    Workflow workflow;
    if (metadata.contains("name")) workflow.name = metadata.at("name").as<std::string>();
    if (metadata.contains("description")) workflow.description = metadata.at("description").as<std::string>();
    if (metadata.contains("strict_inputs")) workflow.strict_inputs = metadata.at("strict_inputs").as<bool>();
    if (metadata.contains("defaults") && metadata.at("defaults").contains("timeout"))
        workflow.defaults.timeout = metadata.at("defaults").at("timeout").as<std::string>();
    if (node.has_child("inputs")) {
        workflow.inputs = parse_contract_fields(node["inputs"], false);
    }
    if (node.has_child("outputs")) {
        workflow.outputs = parse_contract_fields(node["outputs"], true);
    }
    if (metadata.contains("variables")) {
        for (const auto& item : metadata.at("variables").object_range())
            workflow.variables[item.key()] = item.value().as<std::string>();
    }
    if (metadata.contains("env")) {
        for (const auto& item : metadata.at("env").object_range())
            workflow.env[item.key()] = item.value().as<std::string>();
    }
    if (metadata.contains("dot_env")) {
        for (const auto& item : metadata.at("dot_env").array_range())
            workflow.dot_env.push_back(item.as<std::string>());
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
