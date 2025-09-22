#include "yml/task_yaml.hpp"

#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include <stdexcept>
#include <iostream>

#include "yml/task_parser.hpp"

// Utility function to convert ryml node to string vector
std::vector<std::string> node_to_string_vector(const ryml::ConstNodeRef& node) {
    std::vector<std::string> result;
    if (node.is_seq()) {
        for (const auto& child : node) {
            std::string value;
            child >> value;
            result.push_back(value);
        }
    }
    return result;
}

// Utility function to convert ryml node to string map
Vars node_to_string_map(const ryml::ConstNodeRef& node) {
    Vars result;
    if (node.is_map()) {
        for (const auto& child : node) {
            std::string key, value;
            // Convert ryml csubstr to std::string
            key = std::string(child.key().str, child.key().len);
            child >> value;
            result[key] = value;
        }
    }
    return result;
}

// Utility function to parse imports section
StrList parse_imports(const ryml::ConstNodeRef& node) {
    if (node.has_child("imports")) {
        return node_to_string_vector(node["imports"]);
    }
    return {}; // Return empty list if no imports section
}

// Conversion functions for our structs
Input parse_input(const ryml::ConstNodeRef& node) {
    Input input;

    if (!node.is_map() || !node.has_child("name") || !node.has_child("type")) {
        throw std::runtime_error("Input must have 'name' and 'type' fields");
    }

    node["name"] >> input.name;
    node["type"] >> input.type;
    input.default_value = get_optional<std::string>(node, "default", "");
    input.description = get_optional<std::string>(node, "description", "");

    return input;
}

RetryPolicy parse_retry_policy(const ryml::ConstNodeRef& node) {
    RetryPolicy policy;

    if (!node.is_map()) {
        throw std::runtime_error("RetryPolicy must be a map");
    }

    policy.count = get_optional<int>(node, "count", 0);
    policy.delay = get_optional<std::string>(node, "delay", "0s");

    return policy;
}

Each parse_each(const ryml::ConstNodeRef& node) {
    Each each;

    if (!node.is_map() || !node.has_child("items") || !node.has_child("as")) {
        throw std::runtime_error("Each must have 'items' and 'as' fields");
    }

    each.items = node_to_string_vector(node["items"]);
    node["as"] >> each.as;

    return each;
}

Outputs parse_outputs(const ryml::ConstNodeRef& node) {
    Outputs outputs;

    if (!node.is_map()) {
        throw std::runtime_error("Outputs must be a map");
    }

    outputs.stdout_to_variable = get_optional<std::string>(node, "stdout_to_variable", "");
    outputs.stderr_to_variable = get_optional<std::string>(node, "stderr_to_variable", "");
    outputs.exit_code_to_variable = get_optional<std::string>(node, "exit_code_to_variable", "");
    outputs.output_json_to_variable = get_optional<std::string>(node, "output_json_to_variable", "");

    return outputs;
}

// Task-specific param parsers
RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node) {
    RunCommandParams params;

    if (!node.has_child("command")) {
        throw std::runtime_error("RunCommandParams must have 'command' field");
    }

    auto command_node = node["command"];

    // Check node type with correct ryml API
    if (command_node.is_val()) {
        std::string command;
        command_node >> command;
        params.command = command;
    } else if (command_node.is_seq()) {
        params.command = node_to_string_vector(command_node);
    } else {
        // Try extracting as string regardless
        try {
            std::string command;
            command_node >> command;
            params.command = command;
        } catch (...) {
            throw std::runtime_error("Command must be a string or array of strings");
        }
    }

    params.working_directory = get_optional<std::string>(node, "working_directory", "");

    if (node.has_child("environment")) {
        params.environment = node_to_string_map(node["environment"]);
    }

    return params;
}

CopyFileParams parse_copy_file_params(const ryml::ConstNodeRef& node) {
    CopyFileParams params;

    if (!node.has_child("source") || !node.has_child("destination")) {
        throw std::runtime_error("CopyFileParams must have 'source' and 'destination' fields");
    }

    node["source"] >> params.source;
    node["destination"] >> params.destination;
    params.overwrite = get_optional<bool>(node, "overwrite", false);

    return params;
}

CreateDirectoryParams parse_create_directory_params(const ryml::ConstNodeRef& node) {
    CreateDirectoryParams params;

    if (!node.has_child("path")) {
        throw std::runtime_error("CreateDirectoryParams must have 'path' field");
    }

    node["path"] >> params.path;
    params.parents = get_optional<bool>(node, "parents", false);

    return params;
}

MoveFileParams parse_move_file_params(const ryml::ConstNodeRef& node) {
    MoveFileParams params;

    if (!node.has_child("source") || !node.has_child("destination")) {
        throw std::runtime_error("MoveFileParams must have 'source' and 'destination' fields");
    }

    node["source"] >> params.source;
    node["destination"] >> params.destination;
    params.overwrite = get_optional<bool>(node, "overwrite", false);

    return params;
}

ParallelParams parse_parallel_params(const ryml::ConstNodeRef& node) {
    ParallelParams params;

    if (!node.has_child("tasks") || !node["tasks"].is_seq()) {
        throw std::runtime_error("ParallelParams must have 'tasks' array");
    }

    for (const auto& task_node : node["tasks"]) {
        params.tasks.push_back(parse_task(task_node));
    }

    return params;
}

GroupParams parse_group_params(const ryml::ConstNodeRef& node) {
    GroupParams params;

    if (!node.has_child("tasks") || !node["tasks"].is_seq()) {
        throw std::runtime_error("GroupParams must have 'tasks' array");
    }

    for (const auto& task_node : node["tasks"]) {
        params.tasks.push_back(parse_task(task_node));
    }

    return params;
}

ChooseParams parse_choose_params(const ryml::ConstNodeRef& node) {
    ChooseParams params;

    if (!node.has_child("branches") || !node["branches"].is_seq()) {
        throw std::runtime_error("ChooseParams must have a 'branches' array");
    }

    for (const auto& branch_node : node["branches"]) {
        ChooseBranch branch;
        if (!branch_node.has_child("when")) {
            throw std::runtime_error("Each choose branch must have a 'when' condition");
        }
        branch_node["when"] >> branch.when;

        if (!branch_node.has_child("tasks") || !branch_node["tasks"].is_seq()) {
            throw std::runtime_error("Each choose branch must have a 'tasks' array");
        }

        for (const auto& task_node : branch_node["tasks"]) {
            branch.tasks.push_back(parse_task(task_node));
        }
        params.branches.push_back(branch);
    }

    if (node.has_child("default")) {
        if (!node["default"].is_seq()) {
            throw std::runtime_error("Choose 'default' must be a sequence of tasks");
        }
        // Note: default_task_names is still std::vector<std::string>
        // If it should also be std::vector<Task>, then this needs to be changed in task_types.hpp
        // For now, assuming it remains string names.
        for (const auto& task_node : node["default"]) {
            // This assumes default tasks are just names, not full task definitions
            // If they are full task definitions, parse_task should be used and then names extracted
            std::string task_name;
            task_node >> task_name;
            params.default_task_names.push_back(task_name);
        }
    }

    return params;
}

// Parse dynamic_tasks type
DynamicTasksParams parse_dynamic_tasks(const ryml::ConstNodeRef& node) {
    DynamicTasksParams params;

    // Parse template section
    if (node.has_child("template")) {
        const auto& template_node = node["template"];

        if (template_node.has_child("name")) {
            template_node["name"] >> params.task_template.name;
        }
        if (template_node.has_child("type")) {
            template_node["type"] >> params.task_template.type;
        }
        if (template_node.has_child("command")) {
            template_node["command"] >> params.task_template.command;
        }
        if (template_node.has_child("timeout")) {
            template_node["timeout"] >> params.task_template.timeout;
        }
        if (template_node.has_child("when")) {
            template_node["when"] >> params.task_template.when;
        }
        if (template_node.has_child("depends_on")) {
            params.task_template.depends_on = node_to_string_vector(template_node["depends_on"]);
        }
    }

    // Parse items variable reference
    if (node.has_child("items")) {
        node["items"] >> params.items_variable;
    }

    return params;
}

// Main parsers
Task parse_task(const ryml::ConstNodeRef& node) {
    Task task;

    if (!node.is_map()) {
        throw std::runtime_error("Task must be a map");
    }

    // Handle defaults case: defaults may not have name or type
    bool isDefaults = !node.has_child("name") && !node.has_child("type");

    if (!isDefaults && (!node.has_child("name") || !node.has_child("type"))) {
        throw std::runtime_error("Regular tasks must have 'name' and 'type' fields");
    }

    if (node.has_child("name")) {
        node["name"] >> task.name;
    }

    if (node.has_child("type")) {
        node["type"] >> task.type;
    }

    // Common optional attributes
    if (node.has_child("depends_on")) {
        task.depends_on = node_to_string_vector(node["depends_on"]);
    }

    task.when = get_optional<std::string>(node, "when", "");
    task.timeout = get_optional<std::string>(node, "timeout", "");
    task.description = get_optional<std::string>(node, "description", "");

    if (node.has_child("vars")) {
        task.vars = node_to_string_map(node["vars"]);
    }

    // Nested structures
    if (node.has_child("retries")) {
        task.retries = parse_retry_policy(node["retries"]);
    }

    if (node.has_child("each")) {
        task.each = parse_each(node["each"]);
    }

    if (node.has_child("outputs")) {
        task.outputs = parse_outputs(node["outputs"]);
    }

    if (node.has_child("on_success")) {
        task.on_success = node_to_string_vector(node["on_success"]);
    }

    if (node.has_child("on_failure")) {
        task.on_failure = node_to_string_vector(node["on_failure"]);
    }

    // Task-specifics based on `type` (skip for defaults)
    if (!task.type.empty()) {
        if (task.type == "run_command") {
            task.specifics = parse_run_command_params(node);
        } else if (task.type == "copy_file") {
            task.specifics = parse_copy_file_params(node);
        } else if (task.type == "create_directory") {
            task.specifics = parse_create_directory_params(node);
        } else if (task.type == "move_file") {
            task.specifics = parse_move_file_params(node);
        } else if (task.type == "parallel") {
            task.specifics = parse_parallel_params(node);
        } else if (task.type == "group") {
            task.specifics = parse_group_params(node);
        } else if (task.type == "choose") {
            task.specifics = parse_choose_params(node);
        } else if (task.type == "dynamic_tasks") {
            task.specifics = parse_dynamic_tasks(node);
        } else {
            std::cerr << "Warning: Unknown task type '" << task.type << "' for task '" << task.name << "'." << std::endl;
        }
    }

    return task;
}

TemplateParameter parse_template_parameter(const ryml::ConstNodeRef& node) {
    TemplateParameter param;
    if (!node.is_map() || !node.has_child("name") || !node.has_child("type")) {
        throw std::runtime_error("Template parameter must have 'name' and 'type' fields");
    }
    node["name"] >> param.name;
    node["type"] >> param.type;
    param.default_value = get_optional<std::string>(node, "default", "");
    param.description = get_optional<std::string>(node, "description", "");
    return param;
}

TaskTemplate parse_task_template(const ryml::ConstNodeRef& node) {
    TaskTemplate tmpl;
    if (!node.is_map() || !node.has_child("name") || !node.has_child("tasks")) {
        throw std::runtime_error("Task template must have 'name' and 'tasks' fields");
    }
    node["name"] >> tmpl.name;

    if (node.has_child("parameters")) {
        if (!node["parameters"].is_seq()) {
            throw std::runtime_error("Task template 'parameters' must be a sequence");
        }
        for (const auto& param_node : node["parameters"]) {
            tmpl.parameters.push_back(parse_template_parameter(param_node));
        }
    }

    if (!node["tasks"].is_seq()) {
        throw std::runtime_error("Task template 'tasks' must be a sequence");
    }
    for (const auto& task_node : node["tasks"]) {
        tmpl.tasks.push_back(parse_task(task_node));
    }
    return tmpl;
}

Workflow parse_workflow(const ryml::ConstNodeRef& node) {
    Workflow workflow;

    if (!node.is_map()) {
        throw std::runtime_error("Workflow must be a map");
    }

    // Parse inputs
    if (node.has_child("inputs")) {
        for (const auto& input_node : node["inputs"]) {
            workflow.inputs.push_back(parse_input(input_node));
        }
    }

    // Parse variables
    if (node.has_child("variables")) {
        workflow.variables = node_to_string_map(node["variables"]);
    }

    // Parse task_templates
    if (node.has_child("task_templates")) {
        if (!node["task_templates"].is_seq()) {
            throw std::runtime_error("'task_templates' must be a sequence");
        }
        for (const auto& tmpl_node : node["task_templates"]) {
            workflow.task_templates.push_back(parse_task_template(tmpl_node));
        }
    }

    // Parse defaults
    if (node.has_child("defaults")) {
        workflow.defaults = parse_task(node["defaults"]);
    }

    // Parse tasks
    if (node.has_child("tasks")) {
        auto tasks_node = node["tasks"];
        if (tasks_node.is_seq()) {
            for (const auto& task_node : tasks_node) {
                workflow.tasks.push_back(parse_task(task_node));
            }
        } else {
            throw std::runtime_error("'tasks' must be a sequence (list) of task objects");
        }
    }

    // Parse imports
    workflow.imports = parse_imports(node);

    // For metadata/compatibility
    workflow.name = get_optional<std::string>(node, "name", "");
    workflow.description = get_optional<std::string>(node, "description", "");

    return workflow;
}
