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

[[noreturn]] void throw_parse_error(const ryml::ConstNodeRef& /*node*/, const std::string& message) {
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


TriggerAction parse_trigger_action_node(const ryml::ConstNodeRef& node) {
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

std::vector<TriggerAction> parse_trigger_action_list(const ryml::ConstNodeRef& node) {
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

    // Parse parse_regex
    if (node.has_child("parse_regex")) {
        const auto& pr = node["parse_regex"];
        if (!pr.is_map()) {
            throw_parse_error(pr, "parse_regex must be a map");
        }

        ParseRegexConfig config;

        if (!pr.has_child("pattern")) {
            throw_parse_error(pr, "parse_regex requires 'pattern' field");
        }
        pr["pattern"] >> config.pattern;

        if (pr.has_child("capture_group")) {
            pr["capture_group"] >> config.capture_group;
        }

        if (!pr.has_child("output_key")) {
            throw_parse_error(pr, "parse_regex requires 'output_key' field");
        }
        pr["output_key"] >> config.output_key;

        params.parse_regex = config;
    }

    // Parse parse_json
    if (node.has_child("parse_json")) {
        const auto& pj = node["parse_json"];
        if (!pj.is_map()) {
            throw_parse_error(pj, "parse_json must be a map");
        }

        ParseJsonConfig config;

        if (!pj.has_child("path")) {
            throw_parse_error(pj, "parse_json requires 'path' field");
        }
        pj["path"] >> config.path;

        if (!pj.has_child("output_key")) {
            throw_parse_error(pj, "parse_json requires 'output_key' field");
        }
        pj["output_key"] >> config.output_key;

        params.parse_json = config;
    }

    // Parse parse_lines
    if (node.has_child("parse_lines")) {
        const auto& pl = node["parse_lines"];
        if (!pl.is_map()) {
            throw_parse_error(pl, "parse_lines must be a map");
        }

        ParseLinesConfig config;

        if (pl.has_child("filter")) {
            pl["filter"] >> config.filter;
        }

        if (!pl.has_child("output_key")) {
            throw_parse_error(pl, "parse_lines requires 'output_key' field");
        }
        pl["output_key"] >> config.output_key;

        params.parse_lines = config;
    }

    // Parse parse_keyvalue
    if (node.has_child("parse_keyvalue")) {
        const auto& pkv = node["parse_keyvalue"];
        if (!pkv.is_map()) {
            throw_parse_error(pkv, "parse_keyvalue must be a map");
        }

        ParseKeyValueConfig config;

        if (pkv.has_child("delimiter")) {
            pkv["delimiter"] >> config.delimiter;
        }

        if (pkv.has_child("line_separator")) {
            pkv["line_separator"] >> config.line_separator;
        }

        if (!pkv.has_child("output_key")) {
            throw_parse_error(pkv, "parse_keyvalue requires 'output_key' field");
        }
        pkv["output_key"] >> config.output_key;

        params.parse_keyvalue = config;
    }

    return params;
}

// Parse BTDSL node recursively with validation
BtdslNode parse_btdsl_node(const ryml::ConstNodeRef& node, int depth = 0) {
    // Task 2.1: Enforce maximum nesting depth of 100 levels
    const int MAX_NESTING_DEPTH = 100;
    if (depth > MAX_NESTING_DEPTH) {
        throw_parse_error(node, "Maximum nesting depth of " + std::to_string(MAX_NESTING_DEPTH) + " exceeded");
    }

    BtdslNode btdsl_node;

    if (node.is_map()) {
        // Map format: { type: { params } } or { type: [ children ] } or { type: "value" }
        if (node.num_children() != 1) {
            throw_parse_error(node, "BTDSL node must have exactly one key (node type)");
        }

        auto child = node.first_child();
        std::string node_type(child.key().str, child.key().len);
        std::string node_type_lower = to_lower_copy(node_type);

        // Task 2.1: Validate node type against NODE_REGISTRY
        auto* spec_ptr = findNodeSpec(node_type);
        if (spec_ptr == nullptr) {
            std::string supported_types;
            for (const auto& [key, spec] : NODE_REGISTRY) {
                if (!supported_types.empty()) supported_types += ", ";
                supported_types += key;
            }
            throw_parse_error(node, "Unknown BTDSL node type: '" + node_type + "'. Supported types: " + supported_types);
        }

        const BtdslNodeSpec& spec = *spec_ptr;
        btdsl_node.type = spec.name;  // Use normalized name from registry

        if (child.is_map()) {
            // Task 2.2: Leaf node with parameters (map syntax): shell: {cmd: "...", output_key: "..."}
            for (const auto& param : child) {
                std::string key(param.key().str, param.key().len);
                std::string value;
                param >> value;
                
                // Task 2.3: Enforce maximum parameter length of 10KB
                const size_t MAX_PARAM_LENGTH = 10 * 1024;
                if (value.length() > MAX_PARAM_LENGTH) {
                    throw_parse_error(param, "Parameter '" + key + "' exceeds maximum length of " + std::to_string(MAX_PARAM_LENGTH) + " bytes");
                }
                
                btdsl_node.params[key] = value;
            }
            
            // Task 2.3: Validate required parameters are present
            for (const auto& required_param : spec.requiredParams) {
                if (btdsl_node.params.find(required_param) == btdsl_node.params.end()) {
                    std::string required_list;
                    for (const auto& rp : spec.requiredParams) {
                        if (!required_list.empty()) required_list += ", ";
                        required_list += rp;
                    }
                    throw_parse_error(child, "Missing required parameter '" + required_param + "' for node type '" + node_type_lower + "'. Required parameters: " + required_list);
                }
            }
            
            // Validate this is a leaf node (should not have children)
            if (spec.isControl) {
                throw_parse_error(child, "Control node '" + node_type_lower + "' cannot use map syntax. Use sequence syntax: " + node_type_lower + ": [...]");
            }
        } else if (child.is_seq()) {
            // Task 2.1: Control node with children: sequence: [...]
            if (!spec.isControl) {
                throw_parse_error(child, "Leaf node '" + node_type_lower + "' cannot have children");
            }
            
            for (const auto& child_node : child) {
                btdsl_node.children.push_back(parse_btdsl_node(child_node, depth + 1));
            }
            
            // Validate control node has at least one child
            if (btdsl_node.children.empty()) {
                throw_parse_error(child, "Control node '" + node_type_lower + "' must have at least one child");
            }
        } else if (child.has_val()) {
            // Task 2.2: Simplified syntax (single parameter): shell: "echo hello" → shell: {cmd: "echo hello"}
            std::string value;
            child >> value;
            
            // Task 2.3: Enforce maximum parameter length
            const size_t MAX_PARAM_LENGTH = 10 * 1024;
            if (value.length() > MAX_PARAM_LENGTH) {
                throw_parse_error(child, "Parameter value exceeds maximum length of " + std::to_string(MAX_PARAM_LENGTH) + " bytes");
            }
            
            // Validate this is a leaf node
            if (spec.isControl) {
                throw_parse_error(child, "Control node '" + node_type_lower + "' cannot use simple string syntax. Use sequence syntax: " + node_type_lower + ": [...]");
            }
            
            // For simplified syntax, use the first required parameter as the key
            if (spec.requiredParams.empty()) {
                throw_parse_error(child, "Node type '" + node_type_lower + "' does not support simplified syntax (no required parameters)");
            }
            
            btdsl_node.params[spec.requiredParams[0]] = value;
            
            // Validate all required parameters are satisfied (simplified syntax only works for single required param)
            if (spec.requiredParams.size() > 1) {
                std::string required_list;
                for (const auto& rp : spec.requiredParams) {
                    if (!required_list.empty()) required_list += ", ";
                    required_list += rp;
                }
                throw_parse_error(child, "Node type '" + node_type_lower + "' requires multiple parameters (" + required_list + "). Use map syntax: " + node_type_lower + ": {" + required_list + "}");
            }
        } else {
            throw_parse_error(child, "BTDSL node value must be a map (params), sequence (children), or string (simplified syntax)");
        }
    } else {
        throw_parse_error(node, "BTDSL node must be a map");
    }

    return btdsl_node;
}

BtdslParams parse_btdsl_params(const ryml::ConstNodeRef& task_node, const std::string& node_type) {
    BtdslParams params;

    // Create root node with the detected type
    params.root.type = node_type;

    // Normalize type using NODE_REGISTRY
    auto* spec = findNodeSpec(node_type);
    if (spec != nullptr) {
        params.root.type = spec->name;  // Use normalized name from registry
    } else {
        // Fallback to simple capitalization if not in registry
        if (!params.root.type.empty()) {
            params.root.type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(params.root.type[0])));
        }
    }

    // Access child node using c_str()
    const auto& node = task_node[node_type.c_str()];

    if (node.is_map()) {
        // Leaf node: shell: {cmd: "..."}
        for (const auto& param : node) {
            std::string key(param.key().str, param.key().len);
            std::string value;
            param >> value;
            params.root.params[key] = value;
        }
    } else if (node.is_seq()) {
        // Control node: sequence: [...]
        for (const auto& child_node : node) {
            params.root.children.push_back(parse_btdsl_node(child_node, 1));  // Start depth at 1
        }
    } else if (node.has_val()) {
        // Simplified syntax: use the first required parameter for scalar roots too.
        std::string value;
        node >> value;
        if (spec == nullptr || spec->requiredParams.empty()) {
            throw_parse_error(node, "BT node type '" + params.root.type + "' does not support simplified scalar syntax");
        }
        if (spec->requiredParams.size() > 1) {
            std::string required_list;
            for (const auto& rp : spec->requiredParams) {
                if (!required_list.empty()) required_list += ", ";
                required_list += rp;
            }
            throw_parse_error(node, "BT node type '" + to_lower_copy(params.root.type) +
                                   "' requires multiple parameters (" + required_list +
                                   "). Use map syntax instead");
        }
        params.root.params[spec->requiredParams[0]] = value;
    } else {
        throw_parse_error(node, "BTDSL node must be a map (params), sequence (children), or string (cmd)");
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

/**
 * @brief Desugars a RunCommandParams into a BtdslParams tree.
 *
 * Converts a `command:` task into an equivalent behavior tree:
 *   - Single command string   -> Sequence { Shell(cmd) }
 *   - Command list            -> Sequence { Shell(cmd1), Shell(cmd2), ... }
 *   - With parse_regex        -> Sequence { Shell(cmd), ParseRegex(...) }
 *   - With parse_json         -> Sequence { Shell(cmd), ParseJson(...) }
 *   - With parse_lines        -> Sequence { Shell(cmd), ParseLines(...) }
 *   - With parse_keyvalue     -> Sequence { Shell(cmd), ParseKeyValue(...) }
 *   - With output_format:json -> Sequence { Shell(cmd), ParseJson(path=$, output_key=data) }
 *
 * This unifies all command execution through the BT executor.
 */
BtdslParams desugarCommandToBtdsl(const RunCommandParams& params) {
    BtdslParams bt;
    bt.root.type = "Sequence";

    // Build Shell node(s) from command
    if (std::holds_alternative<StrList>(params.command)) {
        // Command list -> multiple Shell nodes in sequence
        const auto& cmds = std::get<StrList>(params.command);
        for (size_t i = 0; i < cmds.size(); ++i) {
            BtdslNode shell_node;
            shell_node.type = "Shell";
            shell_node.params["cmd"] = cmds[i];
            // Last command captures stdout for post-processing
            if (i == cmds.size() - 1) {
                shell_node.params["output_key"] = "stdout";
            }
            bt.root.children.push_back(std::move(shell_node));
        }
    } else {
        // Single command string -> one Shell node
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = std::get<std::string>(params.command);
        shell_node.params["output_key"] = "stdout";
        bt.root.children.push_back(std::move(shell_node));
    }

    // Append post-processing nodes (mutually exclusive in the grammar)
    if (params.parse_regex) {
        BtdslNode parse_node;
        parse_node.type = "ParseRegex";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["pattern"] = params.parse_regex->pattern;
        parse_node.params["capture_group"] = std::to_string(params.parse_regex->capture_group);
        parse_node.params["output_key"] = params.parse_regex->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_json) {
        BtdslNode parse_node;
        parse_node.type = "ParseJson";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["path"] = params.parse_json->path;
        parse_node.params["output_key"] = params.parse_json->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_lines) {
        BtdslNode parse_node;
        parse_node.type = "ParseLines";
        parse_node.params["input_key"] = "stdout";
        if (!params.parse_lines->filter.empty()) {
            parse_node.params["filter"] = params.parse_lines->filter;
        }
        parse_node.params["output_key"] = params.parse_lines->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.parse_keyvalue) {
        BtdslNode parse_node;
        parse_node.type = "ParseKeyValue";
        parse_node.params["input_key"] = "stdout";
        parse_node.params["delimiter"] = params.parse_keyvalue->delimiter;
        parse_node.params["line_separator"] = params.parse_keyvalue->line_separator;
        parse_node.params["output_key"] = params.parse_keyvalue->output_key;
        bt.root.children.push_back(std::move(parse_node));
    } else if (params.output_format == CommandOutputFormat::Json) {
        // output_format: json -> ParseJson node extracting entire object
        BtdslNode json_node;
        json_node.type = "ParseJson";
        json_node.params["input_key"] = "stdout";
        json_node.params["path"] = "$";
        json_node.params["output_key"] = "data";
        bt.root.children.push_back(std::move(json_node));
    }

    return bt;
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
        "command", "uses", "dynamic_tasks", "output_format",
        "parse_regex", "parse_json", "parse_lines", "parse_keyvalue",
        "script", "btdsl",
        // BT node shorthand keys
        "sequence", "fallback", "parallel", "reactive_sequence", "reactive_fallback",
        "retry", "inverter", "force_success", "force_failure", "repeat",
        "timeout", "delay", "switch", "while_do", "if_then_else",
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree"
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

    if (node.has_child("command")) {
        // Desugar command: into a BT tree — unified execution through BT executor
        auto cmd_params = parse_run_command_params(node);
        task.action = TaskAction::Btdsl;
        task.declared_runner = "command";
        task.specifics = desugarCommandToBtdsl(cmd_params);
        ++action_count;
    }

    if (node.has_child("btdsl")) {
        task.action = TaskAction::Btdsl;
        task.declared_runner = "btdsl";
        BtdslParams btdsl_params;
        const auto& bt_node = node["btdsl"];
        if (bt_node.is_map()) {
            // Object-based BT: btdsl: { Sequence: [...] }
            if (bt_node.num_children() != 1) {
                throw_parse_error(bt_node, "btdsl object must have exactly one root node (e.g., Sequence)");
            }
            btdsl_params.root = parse_btdsl_node(bt_node);
        } else {
            throw_parse_error(bt_node, "btdsl must be a map");
        }
        task.specifics = btdsl_params;
        ++action_count;
    }

    if (node.has_child("uses")) {
        const auto& uses_node = node["uses"];
        task.action = TaskAction::Uses;
        task.declared_runner = "uses";
        task.specifics = parse_uses_params(uses_node);
        ++action_count;
    }

    if (node.has_child("dynamic_tasks")) {
        const auto& dt_node = node["dynamic_tasks"];
        task.action = TaskAction::DynamicTasks;
        task.declared_runner = "dynamic_tasks";
        task.specifics = parse_dynamic_tasks_params(dt_node);
        ++action_count;
    }

    // Check for BTDSL node types (control flow and leaf nodes)
    const std::vector<std::string> btdsl_control_nodes = {
        "sequence", "fallback", "parallel", "reactive_sequence", "reactive_fallback"
    };
    const std::vector<std::string> btdsl_leaf_nodes = {
        "shell", "parse_json", "parse_regex", "parse_lines", "parse_keyvalue",
        "check_exit_code", "wait_event", "file_exists", "sleep", "set_variable", "subtree"
    };
    const std::vector<std::string> btdsl_decorator_nodes = {
        "retry", "inverter", "force_success", "force_failure", "repeat",
        "timeout", "delay"
    };
    const std::vector<std::string> btdsl_advanced_nodes = {
        "switch", "while_do", "if_then_else"
    };

    // Check if any BTDSL node type is present
        for (const auto& bt_type : btdsl_control_nodes) {
            if (node.has_child(bt_type.c_str())) {
                task.action = TaskAction::Btdsl;
                task.declared_runner = "btdsl";
                task.specifics = parse_btdsl_params(node, bt_type);
                ++action_count;
                break;
        }
    }
    if (task.action != TaskAction::Btdsl) {
        for (const auto& bt_type : btdsl_leaf_nodes) {
            if (node.has_child(bt_type.c_str())) {
                task.action = TaskAction::Btdsl;
                task.declared_runner = "btdsl";
                task.specifics = parse_btdsl_params(node, bt_type);
                ++action_count;
                break;
            }
        }
    }
    if (task.action != TaskAction::Btdsl) {
        for (const auto& bt_type : btdsl_decorator_nodes) {
            if (node.has_child(bt_type.c_str())) {
                task.action = TaskAction::Btdsl;
                task.declared_runner = "btdsl";
                task.specifics = parse_btdsl_params(node, bt_type);
                ++action_count;
                break;
            }
        }
    }
    if (task.action != TaskAction::Btdsl) {
        for (const auto& bt_type : btdsl_advanced_nodes) {
            if (node.has_child(bt_type.c_str())) {
                task.action = TaskAction::Btdsl;
                task.declared_runner = "btdsl";
                task.specifics = parse_btdsl_params(node, bt_type);
                ++action_count;
                break;
            }
        }
    }

    if (action_count == 0 && !node.has_child("script")) {
        throw_parse_error(node, "task '" + task.name + "' must declare a runner (command/uses/dynamic_tasks/btdsl) or script");
    }
    if (action_count > 1) {
        throw_parse_error(node, "task '" + task.name + "' declares multiple runners");
    }

    // Parse script: inline script source (does not count as an action — it's post-processing)
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
            module.language = get_optional<std::string>(module_node, "language", std::string("javascript"));

            // Support both inline source and external path
            if (module_node.has_child("source")) {
                module_node["source"] >> module.source;
            }
            if (module_node.has_child("path")) {
                module_node["path"] >> module.path;
            }

            if (module.source.empty() && module.path.empty()) {
                throw std::runtime_error("Embedded module '" + module_name + "' requires either 'source' or 'path' field");
            }
            workflow.embedded[module_name] = std::move(module);
        }
    }

    // Parse imports section
    if (node.has_child("imports")) {
        const auto& imports_node = node["imports"];
        if (imports_node.is_map() && imports_node.has_child("modules")) {
            workflow.imports.files = node_to_string_vector(imports_node["modules"]);
        } else if (imports_node.is_seq()) {
            // Allow shorthand: imports: [file1.yml, file2.js]
            workflow.imports.files = node_to_string_vector(imports_node);
        }
    }

    // Parse native_modules section
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
                // Backward compatibility: convert old init: field to hooks["init"]
                std::string init_func;
                mod_node["init"] >> init_func;
                native_mod.hooks["init"] = init_func;
            } else {
                // Default init function name: js_init_<name>
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


// BTDSL node type detection functions
bool isBtdslControlNode(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        return false;
    }
    
    // Check if any child key matches a control node type in NODE_REGISTRY
    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        auto* spec = findNodeSpec(key);
        if (spec && spec->isControl) {
            return true;
        }
    }
    
    return false;
}

bool isBtdslLeafNode(const ryml::ConstNodeRef& node) {
    if (!node.is_map()) {
        return false;
    }
    
    // Check if any child key matches a leaf node type in NODE_REGISTRY
    for (const auto& child : node) {
        std::string key(child.key().str, child.key().len);
        auto* spec = findNodeSpec(key);
        if (spec && !spec->isControl) {
            return true;
        }
    }
    
    return false;
}

BtdslNode parseBtdslNode(const ryml::ConstNodeRef& yaml, int depth) {
    return parse_btdsl_node(yaml, depth);
}
