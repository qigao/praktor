#include "task_yaml_internal.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

TaskDefaults parse_defaults(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "defaults must be a map");
    }

    TaskDefaults defaults;
    if (node.has_child("timeout")) {
        std::string timeout;
        node["timeout"] >> timeout;
        defaults.timeout = timeout;
    }
    return defaults;
}

Task parse_task(const ryml::ConstNodeRef& node, const std::string& source_path) {
    if (!node.is_map()) {
        TaskYamlDetail::throw_parse_error(node, "task must be a map");
    }

    std::unordered_set<std::string> allowed_task_keys = {
        "name", "description", "depends_on", "vars", "env", "dotEnv", "when",
        "each", "timeout", "triggers",
        "working_dir", "silent", "sources", "generates", "finally",
        "command", "program", "args", "stdin", "uses", "dynamic_tasks", "output_format",
        "parse_regex", "parse_json", "parse_lines", "parse_keyvalue",
        "script", "actions",
        "sequence", "parallel", "reactive_sequence", "pipeline_sequence",
        "inverter", "force_success", "force_failure", "repeat",
        "timeout", "delay", "switch", "if", "then", "else", "while", "do", "max_iterations",
        "child", "cases",
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree",
        "fallback", "selector", "continue_on_error",
        "run_once", "keep_running_until_failure", "consume_queue", "precondition", "entry_updated"
    };
    if (node.has_child("set_variable")) {
        allowed_task_keys.insert("value");
        allowed_task_keys.insert("from");
    }
    TaskYamlDetail::check_unknown_keys(node, allowed_task_keys);

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
        std::string value;
        node["silent"] >> value;
        const std::string lowered = TaskYamlDetail::to_lower_copy(value);
        task.silent = (lowered == "true" || lowered == "yes" || lowered == "1");
    }

    if (node.has_child("continue_on_error")) {
        std::string value;
        node["continue_on_error"] >> value;
        const std::string lowered = TaskYamlDetail::to_lower_copy(value);
        task.continue_on_error = (lowered == "true" || lowered == "yes" || lowered == "1");
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
        if (!finally_task.empty()) {
            if (!task.triggers) {
                task.triggers = Triggers{};
            }
            auto& on_complete = task.triggers->on_complete;
            if (std::find(on_complete.begin(), on_complete.end(), finally_task) == on_complete.end()) {
                on_complete.push_back(finally_task);
            }
        }
    }

    int action_count = 0;
    bool runner_selected = false;

    if (node.has_child("command")) {
        auto cmd_params = parse_run_command_params(node);
        if (!runner_selected) {
            task.action = TaskAction::Orch;
            task.declared_runner = "command";
            task.specifics = desugarCommandToorch(cmd_params);
            runner_selected = true;
        }
        ++action_count;
    }

    if (node.has_child("actions")) {
        const auto& bt_node = node["actions"];
        if (!bt_node.is_map()) {
            TaskYamlDetail::throw_parse_error(bt_node, "orch node must be a map");
        }
        if (!runner_selected) {
            task.action = TaskAction::Orch;
            task.declared_runner = "actions";
            OrchParams orch_params;
            orch_params.root = TaskYamlDetail::parse_orch_node(bt_node);
            task.specifics = orch_params;
            runner_selected = true;
        }
        ++action_count;
    }

    if (node.has_child("program")) {
        if (!runner_selected) {
            task.action = TaskAction::Program;
            task.declared_runner = "program";
            task.specifics = parse_program_params(node);
            runner_selected = true;
        }
        ++action_count;
    }

    if (node.has_child("uses")) {
        if (!runner_selected) {
            task.action = TaskAction::Uses;
            task.declared_runner = "uses";
            task.specifics = parse_uses_params(node["uses"]);
            runner_selected = true;
        }
        ++action_count;
    }

    if (node.has_child("dynamic_tasks")) {
        if (!runner_selected) {
            task.action = TaskAction::DynamicTasks;
            task.declared_runner = "dynamic_tasks";
            task.specifics = parse_dynamic_tasks_params(node["dynamic_tasks"]);
            runner_selected = true;
        }
        ++action_count;
    }

    const std::vector<std::string> orch_control_nodes = {
        "sequence", "parallel", "reactive_sequence", "pipeline_sequence", "fallback", "selector"
    };
    const std::vector<std::string> orch_leaf_nodes = {
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree"
    };
    const std::vector<std::string> orch_decorator_nodes = {
        "inverter", "force_success", "force_failure", "repeat",
        "timeout", "delay", "run_once", "keep_running_until_failure", "consume_queue",
        "precondition", "entry_updated"
    };
    const std::vector<std::string> orch_advanced_nodes = {"switch", "if", "while"};

    for (const auto& bt_type : orch_control_nodes) {
        if (node.has_child(bt_type.c_str())) {
            if (!runner_selected) {
                task.action = TaskAction::Orch;
                task.declared_runner = "actions";
                task.specifics = parse_orch_params(node, bt_type);
                runner_selected = true;
            }
            ++action_count;
        }
    }

    for (const auto& bt_type : orch_leaf_nodes) {
        if (node.has_child(bt_type.c_str())) {
            if (!runner_selected) {
                task.action = TaskAction::Orch;
                task.declared_runner = "actions";
                task.specifics = parse_orch_params(node, bt_type);
                runner_selected = true;
            }
            ++action_count;
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
            if (!runner_selected) {
                task.action = TaskAction::Orch;
                task.declared_runner = "actions";
                task.specifics = parse_orch_params(node, bt_type);
                runner_selected = true;
            }
            ++action_count;
        }
    }

    for (const auto& bt_type : orch_advanced_nodes) {
        const bool matches_root =
            (bt_type == "if" && node.has_child("if") && node.has_child("then")) ||
            (bt_type == "while" && node.has_child("while") && node.has_child("do")) ||
            (bt_type == "switch" && node.has_child("switch") && node.has_child("cases"));
        if (matches_root) {
            if (!runner_selected) {
                task.action = TaskAction::Orch;
                task.declared_runner = "actions";
                task.specifics = parse_orch_params(node, bt_type);
                runner_selected = true;
            }
            ++action_count;
        }
    }

    if (action_count == 0 && !node.has_child("script")) {
        TaskYamlDetail::throw_parse_error(
            node, "task '" + task.name +
                      "' must declare a runner (command/program/uses/dynamic_tasks) or script");
    }
    if (action_count > 1) {
        TaskYamlDetail::throw_parse_error(node, "task '" + task.name + "' declares multiple runners");
    }

    if (node.has_child("script")) {
        std::string script_src;
        node["script"] >> script_src;
        if (!script_src.empty()) {
            task.script = script_src;
            if (action_count == 0) {
                task.declared_runner = "script";
            }
        }
    }

    task.source_path = source_path;
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
            module.language = get_optional<std::string>(
                module_node, "language", std::string("javascript"));
            if (module_node.has_child("source")) {
                module_node["source"] >> module.source;
            }
            if (module_node.has_child("path")) {
                module_node["path"] >> module.path;
            }
            if (module.source.empty() && module.path.empty()) {
                throw std::runtime_error("Embedded module '" + module_name +
                                         "' requires either 'source' or 'path' field");
            }
            workflow.embedded[module_name] = std::move(module);
        }
    }

    if (node.has_child("imports")) {
        const auto& imports_node = node["imports"];
        if (imports_node.is_map() && imports_node.has_child("modules")) {
            workflow.imports.files = node_to_string_vector(imports_node["modules"]);
        } else if (imports_node.is_seq()) {
            workflow.imports.files = node_to_string_vector(imports_node);
        }
    }

    if (node.has_child("native_modules")) {
        const auto& native_node = node["native_modules"];
        if (!native_node.is_seq()) {
            throw std::runtime_error("'native_modules' must be a sequence");
        }
        for (const auto& mod_node : native_node) {
            if (!mod_node.is_map()) {
                throw std::runtime_error("Each native module must be a map");
            }

            NativeModule native_mod;
            if (!mod_node.has_child("name")) {
                throw std::runtime_error("Native module requires 'name' field");
            }
            mod_node["name"] >> native_mod.name;

            if (!mod_node.has_child("path")) {
                throw std::runtime_error("Native module '" + native_mod.name + "' requires 'path' field");
            }
            mod_node["path"] >> native_mod.path;

            if (mod_node.has_child("hooks")) {
                native_mod.hooks = node_to_string_map(mod_node["hooks"]);
            } else if (mod_node.has_child("init")) {
                std::string init_func;
                mod_node["init"] >> init_func;
                native_mod.hooks["init"] = init_func;
            } else {
                native_mod.hooks["init"] = "js_init_" + native_mod.name;
            }

            workflow.native_modules.push_back(std::move(native_mod));
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
