#include "task_yaml_internal.hpp"
#include "workflow_contract.hpp"
#include "yml/yaml_task_metadata.h"

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


} // namespace

Task parse_task(const TaskYamlDetail::YamlNodeRef& node, const std::string& source_path) {
    WorkflowValue metadata = WorkflowValue::adoptJson(
        TaskYamlDetail::read_task_metadata_or_throw(node));
    Task task;
    task.name = metadata.at("name").as<std::string>();
    if (metadata.contains("description")) task.description = metadata.at("description").as<std::string>();
    if (metadata.contains("depends_on")) {
        for (const auto& value : metadata.at("depends_on").array_range())
            task.depends_on.push_back(value.as<std::string>());
    }
    if (metadata.contains("vars")) {
        for (const auto& item : metadata.at("vars").object_range())
            task.vars[item.key()] = item.value().as<std::string>();
    }
    if (metadata.contains("env")) {
        for (const auto& item : metadata.at("env").object_range())
            task.env[item.key()] = item.value().as<std::string>();
    }
    if (metadata.contains("dot_env")) {
        for (const auto& value : metadata.at("dot_env").array_range())
            task.dot_env.push_back(value.as<std::string>());
    }
    if (metadata.contains("when")) task.when = metadata.at("when").as<std::string>();
    if (metadata.contains("working_dir")) task.working_dir = metadata.at("working_dir").as<std::string>();
    if (metadata.contains("sources")) {
        for (const auto& value : metadata.at("sources").array_range())
            task.sources.push_back(value.as<std::string>());
    }
    if (metadata.contains("generates")) {
        for (const auto& value : metadata.at("generates").array_range())
            task.generates.push_back(value.as<std::string>());
    }

    if (node.has_child("each")) {
        task.each = parse_each(node["each"]);
    }

    if (metadata.contains("timeout")) task.timeout = metadata.at("timeout").as<std::string>();
    if (metadata.contains("retry_count"))
        task.retry_count = metadata.at("retry_count").as<std::uint32_t>();

    if (node.has_child("triggers")) {
        Triggers triggers = parse_triggers(node["triggers"]);
        if (!triggers.empty()) {
            task.triggers = triggers;
        }
    }

    if (metadata.contains("finally")) {
        const std::string finally_task = metadata.at("finally").as<std::string>();
        if (!task.triggers) task.triggers = Triggers{};
        auto& on_complete = task.triggers->on_complete;
        if (std::find(on_complete.begin(), on_complete.end(), finally_task) == on_complete.end())
            on_complete.push_back(finally_task);
    }

    if (metadata.contains("silent")) task.silent = metadata.at("silent").as<bool>();

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
