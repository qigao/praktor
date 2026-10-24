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

    if (metadata.contains("each")) {
        const auto& encoded = metadata.at("each");
        Each each;
        for (const auto& value : encoded.at("items").array_range())
            each.items.push_back(value.as<std::string>());
        for (const auto& item : encoded.at("matrix").object_range()) {
            StrList values;
            for (const auto& value : item.value().array_range())
                values.push_back(value.as<std::string>());
            each.matrix[item.key()] = std::move(values);
        }
        each.as = encoded.at("as").as<std::string>();
        each.index_variable = encoded.at("index_variable").as<std::string>();
        task.each = std::move(each);
    }

    if (metadata.contains("timeout")) task.timeout = metadata.at("timeout").as<std::string>();
    if (metadata.contains("retry_count"))
        task.retry_count = metadata.at("retry_count").as<std::uint32_t>();

    if (metadata.contains("triggers")) {
        const auto& encoded = metadata.at("triggers");
        Triggers triggers;
        if (encoded.contains("on_success"))
            for (const auto& value : encoded.at("on_success").array_range())
                triggers.on_success.push_back(value.as<std::string>());
        if (encoded.contains("on_failure"))
            for (const auto& value : encoded.at("on_failure").array_range())
                triggers.on_failure.push_back(value.as<std::string>());
        if (encoded.contains("on_complete"))
            for (const auto& value : encoded.at("on_complete").array_range())
                triggers.on_complete.push_back(value.as<std::string>());
        if (!triggers.empty()) task.triggers = std::move(triggers);
    }

    if (metadata.contains("finally")) {
        const std::string finally_task = metadata.at("finally").as<std::string>();
        if (!task.triggers) task.triggers = Triggers{};
        auto& on_complete = task.triggers->on_complete;
        if (std::find(on_complete.begin(), on_complete.end(), finally_task) == on_complete.end())
            on_complete.push_back(finally_task);
    }

    if (metadata.contains("silent")) task.silent = metadata.at("silent").as<bool>();

    const int action_count = metadata.contains("runner_count") ? metadata.at("runner_count").as<int>() : 0;
    const std::string selected_runner = metadata.contains("runner_kind") ? metadata.at("runner_kind").as<std::string>() : std::string{};
    if (selected_runner == "command") {
        task.action=TaskAction::Orch; task.declared_runner="command";
        task.specifics=desugarCommandToorch(parse_run_command_params(node));
        if(node.has_child("actions")&&!node["actions"].is_map())
            TaskYamlDetail::throw_parse_error(node["actions"],"orch node must be a map");
    } else if(selected_runner=="actions") {
        const auto& bt_node=node["actions"];
        if(!bt_node.is_map())TaskYamlDetail::throw_parse_error(bt_node,"orch node must be a map");
        task.action=TaskAction::Orch;task.declared_runner="actions";
        OrchParams params;params.root=TaskYamlDetail::parse_orch_node(bt_node);task.specifics=std::move(params);
    } else if(selected_runner=="program") {
        task.action=TaskAction::Program;task.declared_runner="program";task.specifics=parse_program_params(node);
    } else if(selected_runner=="download") {
        task.action=TaskAction::Download;task.declared_runner="download";task.specifics=parse_download_params(node);
    } else if(selected_runner=="service") {
        task.action=TaskAction::Service;task.declared_runner="service";task.specifics=parse_service_params(node);
    } else if(selected_runner=="managed_process") {
        task.action=TaskAction::ManagedProcess;task.declared_runner="managed_process";task.specifics=parse_managed_process_params(node);
    } else if(selected_runner=="uses") {
        task.action=TaskAction::Uses;task.declared_runner="uses";task.specifics=parse_uses_params(node["uses"]);
    } else if(selected_runner=="dynamic_tasks") {
        task.action=TaskAction::DynamicTasks;task.declared_runner="dynamic_tasks";task.specifics=parse_dynamic_tasks_params(node["dynamic_tasks"]);
    } else if(selected_runner=="tool") {
        task.action=TaskAction::HostTool;task.declared_runner="tool";
        HostToolParams params;
        params.tool=TaskYamlDetail::read_system_action_string_or_throw(node["tool"],"'tool' must be a string stable host-tool identity");
        if(params.tool.empty())TaskYamlDetail::throw_parse_error(node["tool"],"'tool' identity cannot be empty");
        if(node.has_child("with")) {
            if(!node["with"].is_map())TaskYamlDetail::throw_parse_error(node["with"],"'with' must be a map of typed host-tool arguments");
            params.arguments=parse_contract_value(node["with"]);
        }
        task.specifics=std::move(params);
    } else if(selected_runner=="orch_root") {
        task.action=TaskAction::Orch;task.declared_runner="actions";
        task.specifics=parse_orch_params(node,metadata.at("orch_node_type").as<std::string>());
    }

    if (action_count > 1) {
        TaskYamlDetail::throw_parse_error(node, "task '" + task.name + "' declares multiple runners");
    }
    if (node.has_child("retries") && task.action != TaskAction::HostTool) {
        TaskYamlDetail::throw_parse_error(
            node["retries"], "'retries' is currently supported only with the 'tool' runner");
    }

    if (const auto script_src = TaskYamlDetail::read_task_script_or_throw(node)) {
        task.script = *script_src;
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
