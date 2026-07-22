#include "task_yaml_internal.hpp"

#include <string>
#include <unordered_set>

namespace {

using namespace TaskYamlDetail;

void validate_required_params(const TaskYamlDetail::YamlNodeRef& node,
                              const OrchNodeSpec& spec,
                              const OrchNode& parsed_node,
                              const std::string& node_type_lower);
void validate_child_shape(const TaskYamlDetail::YamlNodeRef& node,
                          const OrchNodeSpec& spec,
                          const OrchNode& parsed_node,
                          const std::string& node_type_lower);

const std::string* find_canonical_param_key(const OrchNodeSpec& spec, const std::string& key) {
    const std::string normalized_key = normalize_lookup_key(key);

    for (const auto& candidate : spec.requiredParams) {
        if (normalize_lookup_key(candidate) == normalized_key) {
            return &candidate;
        }
    }

    for (const auto& candidate : spec.optionalParams) {
        if (normalize_lookup_key(candidate) == normalized_key) {
            return &candidate;
        }
    }

    return nullptr;
}

bool is_single_child_bt_node(const std::string& node_type) {
    return node_type == "Retry" ||
           node_type == "Inverter" ||
           node_type == "ForceSuccess" ||
           node_type == "ForceFailure" ||
           node_type == "Repeat" ||
           node_type == "Timeout" ||
           node_type == "Delay" ||
           node_type == "RunOnce" ||
           node_type == "KeepRunningUntilFailure" ||
           node_type == "ConsumeQueue" ||
           node_type == "Precondition" ||
           node_type == "EntryUpdated";
}

bool is_runtime_substituted_scalar(const std::string& value) {
    return value.find('{') != std::string::npos || value.find('}') != std::string::npos;
}

bool is_flat_if_form(const TaskYamlDetail::YamlNodeRef& node) {
    return node.is_map() && node.has_child("if") && node.has_child("then");
}

bool is_flat_while_form(const TaskYamlDetail::YamlNodeRef& node) {
    return node.is_map() && node.has_child("while") && node.has_child("do");
}

bool is_flat_switch_form(const TaskYamlDetail::YamlNodeRef& node) {
    return node.is_map() && node.has_child("switch") && node.has_child("cases");
}

bool is_flat_set_variable_form(const TaskYamlDetail::YamlNodeRef& node) {
    return node.is_map() && node.has_child("set_variable") &&
           (node.has_child("value") || node.has_child("from"));
}

bool is_flat_single_child_form(const TaskYamlDetail::YamlNodeRef& node, const char* key) {
    return node.is_map() && node.has_child(key) && node.has_child("child");
}

bool is_task_level_metadata_key(const std::string& normalized_key) {
    static const std::unordered_set<std::string> task_keys = {
        "name", "description", "depends_on", "vars", "env", "dot_env", "fallback", "selector", "when",
        "each", "timeout", "triggers",
        "workingdir", "silent", "sources", "generates", "finally",
        "command", "uses", "dynamictasks", "outputformat",
        "parseregex", "parsejson", "parselines", "parsekeyvalue",
        "script", "actions"
    };
    return task_keys.find(normalized_key) != task_keys.end();
}

void validate_integer_like_orch_param(const TaskYamlDetail::YamlNodeRef& node,
                                       const std::string& node_type_lower,
                                       const std::string& key,
                                       const std::string& value) {
    static const std::unordered_set<std::string> integer_params = {
        "capture_group", "expected", "timeout", "num_attempts",
        "num_cycles", "timeout_ms", "delay_ms", "max_iterations"
    };

    if (integer_params.find(key) == integer_params.end() || is_runtime_substituted_scalar(value)) {
        return;
    }

    size_t index = 0;
    if (!value.empty() && (value[0] == '+' || value[0] == '-')) {
        index = 1;
    }

    if (index == value.size()) {
        throw_parse_error(node, "Orchestration parameter '" + key + "' for node type '" +
                                    node_type_lower + "' must be an integer");
    }

    for (; index < value.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(value[index]))) {
            throw_parse_error(node, "Orchestration parameter '" + key + "' for node type '" +
                                        node_type_lower + "' must be an integer");
        }
    }
}

void validate_boolean_like_orch_param(const TaskYamlDetail::YamlNodeRef& node,
                                       const std::string& node_type_lower,
                                       const std::string& key,
                                       const std::string& value) {
    static const std::unordered_set<std::string> boolean_params = {
        "stream_output", "fail_if_missing"
    };

    if (boolean_params.find(key) == boolean_params.end() || is_runtime_substituted_scalar(value)) {
        return;
    }

    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized == "true" || normalized == "false") {
        return;
    }

    throw_parse_error(node, "Orchestration parameter '" + key + "' for node type '" +
                                node_type_lower + "' must be a boolean");
}

void append_sequence_children(const TaskYamlDetail::YamlNodeRef& seq_node,
                              std::vector<OrchNode>& children,
                              int depth) {
    if (!seq_node.is_seq()) {
        throw_parse_error(seq_node, "Orchestration children block must be a sequence");
    }

    for (const auto& child_node : seq_node) {
        children.push_back(TaskYamlDetail::parse_orch_node(child_node, depth + 1));
    }
}

OrchNode parse_branch_node(const TaskYamlDetail::YamlNodeRef& node, int depth) {
    if (node.is_seq()) {
        OrchNode branch;
        branch.type = "Sequence";
        append_sequence_children(node, branch.children, depth);
        if (branch.children.empty()) {
            throw_parse_error(node, "Orchestration branch sequence must contain at least one child");
        }
        return branch;
    }

    return TaskYamlDetail::parse_orch_node(node, depth);
}

OrchNode parse_flat_if_node(const TaskYamlDetail::YamlNodeRef& node, int depth,
                             bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec("IfThenElse");
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != "if" && normalized_key != "then" && normalized_key != "else") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type 'if'");
        }
    }

    OrchNode parsed_node;
    parsed_node.type = spec.name;

    const auto& condition_node = node["if"];
    if (condition_node.has_val()) {
        parsed_node.params["condition"] = read_scalar_or_throw(
            condition_node, "Orchestration if condition must be a scalar, node, or sequence");
    } else {
        parsed_node.children.push_back(parse_branch_node(condition_node, depth + 1));
    }

    if (!node.has_child("then")) {
        throw_parse_error(node, "if requires a then branch");
    }
    parsed_node.children.push_back(parse_branch_node(node["then"], depth + 1));

    if (node.has_child("else")) {
        parsed_node.children.push_back(parse_branch_node(node["else"], depth + 1));
    }

    validate_child_shape(node, spec, parsed_node, "if");
    return parsed_node;
}

OrchNode parse_flat_while_node(const TaskYamlDetail::YamlNodeRef& node, int depth,
                                bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec("WhileDo");
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != "while" && normalized_key != "do" && normalized_key != "maxiterations") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type 'while'");
        }
    }

    OrchNode parsed_node;
    parsed_node.type = spec.name;

    const auto& condition_node = node["while"];
    if (condition_node.has_val()) {
        parsed_node.params["condition"] = read_scalar_or_throw(
            condition_node, "Orchestration while condition must be a scalar, node, or sequence");
    } else {
        parsed_node.children.push_back(parse_branch_node(condition_node, depth + 1));
    }

    if (node.has_child("max_iterations")) {
        std::string value = read_scalar_or_throw(
            node["max_iterations"],
            "Orchestration parameter 'max_iterations' for node type 'while' must be a scalar");
        validate_integer_like_orch_param(node["max_iterations"], "while", "max_iterations", value);
        parsed_node.params["max_iterations"] = std::move(value);
    }

    if (!node.has_child("do")) {
        throw_parse_error(node, "while requires a do branch");
    }
    parsed_node.children.push_back(parse_branch_node(node["do"], depth + 1));

    validate_child_shape(node, spec, parsed_node, "while");
    return parsed_node;
}

OrchNode parse_flat_switch_node(const TaskYamlDetail::YamlNodeRef& node, int depth,
                                 bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec("Switch");
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != "switch" && normalized_key != "cases") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type 'switch'");
        }
    }

    if (!node.has_child("cases")) {
        throw_parse_error(node, "switch requires a cases branch");
    }

    OrchNode parsed_node;
    parsed_node.type = spec.name;
    parsed_node.params["variable"] = read_scalar_or_throw(
        node["switch"], "Orchestration switch selector must be a scalar");

    const auto& cases_node = node["cases"];
    if (!cases_node.is_seq()) {
        throw_parse_error(cases_node, "switch cases must be a sequence");
    }
    for (const auto& case_node : cases_node) {
        parsed_node.children.push_back(parse_branch_node(case_node, depth + 1));
    }

    validate_required_params(node, spec, parsed_node, "switch");
    validate_child_shape(node, spec, parsed_node, "switch");
    return parsed_node;
}

OrchNode parse_flat_set_variable_node(const TaskYamlDetail::YamlNodeRef& node,
                                       bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec("SetVariable");
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != "setvariable" &&
            normalized_key != "value" &&
            normalized_key != "from") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key +
                                       "' for node type 'set_variable'");
        }
    }

    const bool has_value = node.has_child("value");
    const bool has_from = node.has_child("from");
    if (has_value == has_from) {
        throw_parse_error(node, "set_variable requires exactly one of 'value' or 'from'");
    }

    OrchNode parsed_node;
    parsed_node.type = spec.name;
    parsed_node.params["key"] = read_scalar_or_throw(
        node["set_variable"],
        "Orchestration parameter 'key' for node type 'set_variable' must be a scalar");

    if (has_value) {
        parsed_node.params["value"] = read_scalar_or_throw(
            node["value"],
            "Orchestration parameter 'value' for node type 'set_variable' must be a scalar");
    } else {
        parsed_node.params["from"] = read_scalar_or_throw(
            node["from"],
            "Orchestration parameter 'from' for node type 'set_variable' must be a scalar");
    }

    validate_required_params(node, spec, parsed_node, "set_variable");
    validate_child_shape(node, spec, parsed_node, "set_variable");
    return parsed_node;
}

OrchNode parse_flat_single_child_scalar_node(const TaskYamlDetail::YamlNodeRef& node,
                                              int depth,
                                              const std::string& external_name,
                                              const std::string& internal_name,
                                              const std::string& param_key,
                                              bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec(internal_name);
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != normalize_lookup_key(external_name) && normalized_key != "child") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type '" +
                                       external_name + "'");
        }
    }

    OrchNode parsed_node;
    parsed_node.type = spec.name;

    std::string value = read_scalar_or_throw(
        node[external_name.c_str()],
        "Orchestration parameter '" + param_key + "' for node type '" + external_name + "' must be a scalar");
    validate_integer_like_orch_param(node[external_name.c_str()], external_name, param_key, value);
    parsed_node.params[param_key] = std::move(value);
    parsed_node.children.push_back(parse_branch_node(node["child"], depth + 1));

    validate_child_shape(node, spec, parsed_node, external_name);
    return parsed_node;
}

OrchNode parse_flat_single_child_node(const TaskYamlDetail::YamlNodeRef& node,
                                       int depth,
                                       const std::string& external_name,
                                       const std::string& internal_name,
                                       bool allow_task_metadata = false) {
    auto* spec_ptr = findNodeSpec(internal_name);
    const OrchNodeSpec& spec = *spec_ptr;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);
        if (allow_task_metadata && is_task_level_metadata_key(normalized_key)) {
            continue;
        }
        if (normalized_key != normalize_lookup_key(external_name) && normalized_key != "child") {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type '" +
                                       external_name + "'");
        }
    }

    read_scalar_or_throw(
        node[external_name.c_str()],
        "Orchestration parameter '" + external_name + "' for node type '" + external_name +
            "' must be a scalar");

    OrchNode parsed_node;
    parsed_node.type = spec.name;
    parsed_node.children.push_back(parse_branch_node(node["child"], depth + 1));

    validate_child_shape(node, spec, parsed_node, external_name);
    return parsed_node;
}

void validate_required_params(const TaskYamlDetail::YamlNodeRef& node,
                              const OrchNodeSpec& spec,
                              const OrchNode& parsed_node,
                              const std::string& node_type_lower) {
    for (const auto& required_param : spec.requiredParams) {
        if (parsed_node.params.find(required_param) == parsed_node.params.end()) {
            std::string required_list;
            for (const auto& rp : spec.requiredParams) {
                if (!required_list.empty()) {
                    required_list += ", ";
                }
                required_list += rp;
            }
            throw_parse_error(node, "Missing required parameter '" + required_param +
                                        "' for node type '" + node_type_lower +
                                        "'. Required parameters: " + required_list);
        }
    }
}

void validate_child_shape(const TaskYamlDetail::YamlNodeRef& node,
                          const OrchNodeSpec& spec,
                          const OrchNode& parsed_node,
                          const std::string& node_type_lower) {
    const size_t child_count = parsed_node.children.size();
    const bool has_condition_param = parsed_node.params.find("condition") != parsed_node.params.end();

    if (!spec.allowsChildren && child_count > 0) {
        throw_parse_error(node, "Leaf node '" + node_type_lower + "' cannot have children");
    }

    if (spec.name == "IfThenElse") {
        const size_t min_children = has_condition_param ? 1 : 2;
        const size_t max_children = has_condition_param ? 2 : 3;
        if (child_count < min_children || child_count > max_children) {
            throw_parse_error(node, "if requires a condition plus then/else branches");
        }
        return;
    }

    if (spec.name == "WhileDo") {
        const size_t expected_children = has_condition_param ? 1 : 2;
        if (child_count != expected_children) {
            throw_parse_error(node, "while requires a condition and a do branch");
        }
        return;
    }

    if (spec.name == "Switch") {
        if (child_count == 0) {
            throw_parse_error(node, "switch requires at least one case branch");
        }
        return;
    }

    if (is_single_child_bt_node(spec.name)) {
        if (child_count != 1) {
            throw_parse_error(node, to_lower_copy(spec.name) + " must have exactly one child");
        }
        return;
    }

    if (spec.isControl && child_count == 0) {
        throw_parse_error(node, "Control node '" + node_type_lower + "' must have at least one child");
    }
}

void parse_map_style_btdsl_node(const TaskYamlDetail::YamlNodeRef& node,
                                const OrchNodeSpec& spec,
                                OrchNode& parsed_node,
                                int depth,
                                const std::string& node_type_lower) {
    bool has_children_key = false;
    bool has_child_key = false;

    for (const auto& entry : node) {
        std::string key = entry.key();
        const std::string normalized_key = normalize_lookup_key(key);

        if (normalized_key == "children") {
            has_children_key = true;
            continue;
        }
        if (normalized_key == "child") {
            has_child_key = true;
            continue;
        }
        if (spec.name == "Switch" && normalized_key == "cases") {
            continue;
        }

        const std::string* canonical_key = find_canonical_param_key(spec, key);
        if (canonical_key == nullptr) {
            throw_parse_error(entry, "Unknown orchestration parameter '" + key + "' for node type '" +
                                       node_type_lower + "'");
        }

        std::string value = read_scalar_or_throw(
            entry, "Orchestration parameter '" + key + "' for node type '" + node_type_lower + "' must be a scalar");
        validate_integer_like_orch_param(entry, node_type_lower, *canonical_key, value);
        validate_boolean_like_orch_param(entry, node_type_lower, *canonical_key, value);
        parsed_node.params[*canonical_key] = std::move(value);
    }

    if (has_children_key && has_child_key) {
        throw_parse_error(node, "Orchestration node cannot define both 'child' and 'children'");
    }

    if (has_children_key) {
        if (!spec.allowsChildren) {
            throw_parse_error(node["children"], "Leaf node '" + node_type_lower + "' cannot have children");
        }
        append_sequence_children(node["children"], parsed_node.children, depth);
    }

    if (has_child_key) {
        if (!spec.allowsChildren) {
            throw_parse_error(node["child"], "Leaf node '" + node_type_lower + "' cannot have children");
        }
        parsed_node.children.push_back(parse_branch_node(node["child"], depth + 1));
    }

    if (spec.name == "Switch" && node.has_child("cases")) {
        const auto& cases_node = node["cases"];
        if (!cases_node.is_seq()) {
            throw_parse_error(cases_node, "switch cases must be a sequence");
        }
        for (const auto& case_node : cases_node) {
            parsed_node.children.push_back(parse_branch_node(case_node, depth + 1));
        }
    }
}

OrchNode parse_named_btdsl_node(const std::string& node_type,
                                 const TaskYamlDetail::YamlNodeRef& node,
                                 int depth) {
    if (depth > MAX_BTDSL_NESTING_DEPTH) {
        throw_parse_error(node, "Maximum nesting depth of " +
                                std::to_string(MAX_BTDSL_NESTING_DEPTH) + " exceeded");
    }

    const std::string normalized_type = normalize_lookup_key(node_type);
    if (normalized_type == "ifthenelse") {
        throw_parse_error(node, "Legacy orchestration node type '" + node_type +
                                   "' has been removed. Use flat 'if' / 'then' / 'else' syntax");
    }
    if (normalized_type == "whiledo") {
        throw_parse_error(node, "Legacy orchestration node type '" + node_type +
                                   "' has been removed. Use flat 'while' / 'do' syntax");
    }
    if (normalized_type == "switch") {
        throw_parse_error(node, "Legacy orchestration node type '" + node_type +
                                   "' has been removed. Use flat 'switch' with sibling 'cases' syntax");
    }
    if (normalized_type == "timeout" ||
        normalized_type == "repeat" || normalized_type == "delay") {
        throw_parse_error(node, "Legacy orchestration node type '" + node_type +
                                   "' has been removed. Use flat '" + to_lower_copy(node_type) +
                                   "' plus sibling 'child' syntax");
    }
    if (normalized_type == "setvariable") {
        throw_parse_error(node, "Legacy orchestration node type '" + node_type +
                                   "' has been removed. Use flat 'set_variable' plus sibling 'value' or 'from' syntax");
    }

    auto* spec_ptr = findNodeSpec(node_type);
    if (spec_ptr == nullptr) {
        std::string supported_types;
        for (const auto& [key, spec] : NODE_REGISTRY) {
            if (!supported_types.empty()) {
                supported_types += ", ";
            }
            supported_types += key;
        }
        throw_parse_error(node, "Unknown orchestration node type: '" + node_type +
                                   "'. Supported types: " + supported_types);
    }

    const OrchNodeSpec& spec = *spec_ptr;
    const std::string node_type_lower = to_lower_copy(node_type);

    OrchNode parsed_node;
    parsed_node.type = spec.name;

    if (node.is_seq()) {
        if (!spec.allowsChildren) {
            throw_parse_error(node, "Leaf node '" + node_type_lower + "' cannot have children");
        }
        append_sequence_children(node, parsed_node.children, depth);
    } else if (node.has_val()) {
        if (spec.requiredParams.empty()) {
            throw_parse_error(node, "Node type '" + node_type_lower +
                                     "' does not support simplified syntax (no required parameters)");
        }
        if (spec.requiredParams.size() > 1) {
            std::string required_list;
            for (const auto& rp : spec.requiredParams) {
                if (!required_list.empty()) {
                    required_list += ", ";
                }
                required_list += rp;
            }
            throw_parse_error(node, "Node type '" + node_type_lower +
                                     "' requires multiple parameters (" + required_list +
                                     "). Use map syntax");
        }

        std::string value = read_scalar_or_throw(
            node, "Orchestration simplified syntax requires a scalar value");
        validate_integer_like_orch_param(node, node_type_lower, spec.requiredParams[0], value);
        validate_boolean_like_orch_param(node, node_type_lower, spec.requiredParams[0], value);
        parsed_node.params[spec.requiredParams[0]] = std::move(value);
    } else if (node.is_map()) {
        parse_map_style_btdsl_node(node, spec, parsed_node, depth, node_type_lower);
    } else {
        throw_parse_error(node, "Orchestration node value must be a map, sequence, or scalar");
    }

    validate_required_params(node, spec, parsed_node, node_type_lower);
    validate_child_shape(node, spec, parsed_node, node_type_lower);
    return parsed_node;
}

} // namespace

namespace TaskYamlDetail {

OrchNode parse_orch_node(const TaskYamlDetail::YamlNodeRef& node, int depth) {
    if (!node.is_map()) {
        throw_parse_error(node, "Orchestration node must be a map");
    }

    if (is_flat_if_form(node)) {
        return parse_flat_if_node(node, depth);
    }
    if (is_flat_while_form(node)) {
        return parse_flat_while_node(node, depth);
    }
    if (is_flat_switch_form(node)) {
        return parse_flat_switch_node(node, depth);
    }
    if (is_flat_set_variable_form(node)) {
        return parse_flat_set_variable_node(node);
    }
    if (is_flat_single_child_form(node, "timeout")) {
        return parse_flat_single_child_scalar_node(node, depth, "timeout", "Timeout", "timeout_ms");
    }
    if (is_flat_single_child_form(node, "repeat")) {
        return parse_flat_single_child_scalar_node(node, depth, "repeat", "Repeat", "num_cycles");
    }
    if (is_flat_single_child_form(node, "delay")) {
        return parse_flat_single_child_scalar_node(node, depth, "delay", "Delay", "delay_ms");
    }

    if (node.num_children() != 1) {
        throw_parse_error(node, "Orchestration node must have exactly one key (node type)");
    }

    auto child = node.first_child();
    std::string node_type = child.key();
    return parse_named_btdsl_node(node_type, child, depth);
}

} // namespace TaskYamlDetail

OrchParams parse_orch_params(const TaskYamlDetail::YamlNodeRef& task_node, const std::string& node_type) {
    OrchParams params;
    if (node_type == "if") {
        params.root = parse_flat_if_node(task_node, 0, true);
    } else if (node_type == "while") {
        params.root = parse_flat_while_node(task_node, 0, true);
    } else if (node_type == "switch") {
        params.root = parse_flat_switch_node(task_node, 0, true);
    } else if (node_type == "set_variable") {
        params.root = parse_flat_set_variable_node(task_node, true);
    } else if (node_type == "timeout") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "timeout", "Timeout", "timeout_ms", true);
    } else if (node_type == "repeat") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "repeat", "Repeat", "num_cycles", true);
    } else if (node_type == "delay") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "delay", "Delay", "delay_ms", true);
    } else if (node_type == "keep_running_until_failure") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "keep_running_until_failure", "KeepRunningUntilFailure", "max_iterations", true);
    } else if (node_type == "precondition") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "precondition", "Precondition", "condition", true);
    } else if (node_type == "entry_updated") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "entry_updated", "EntryUpdated", "watch_key", true);
    } else if (node_type == "consume_queue") {
        params.root = parse_flat_single_child_scalar_node(
            task_node, 0, "consume_queue", "ConsumeQueue", "queue_key", true);
    } else if (node_type == "run_once" && task_node.has_child("child")) {
        params.root = parse_flat_single_child_node(
            task_node, 0, "run_once", "RunOnce", true);
    } else {
        params.root = parse_named_btdsl_node(node_type, task_node[node_type.c_str()], 0);
    }
    return params;
}

bool isBtdslControlNode(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        return false;
    }

    for (const auto& child : node) {
        std::string key = child.key();
        auto* spec = findNodeSpec(key);
        if (spec && spec->isControl) {
            return true;
        }
    }

    return false;
}

bool isBtdslLeafNode(const TaskYamlDetail::YamlNodeRef& node) {
    if (!node.is_map()) {
        return false;
    }

    for (const auto& child : node) {
        std::string key = child.key();
        auto* spec = findNodeSpec(key);
        if (spec && !spec->isControl) {
            return true;
        }
    }

    return false;
}

OrchNode parseOrchNode(const TaskYamlDetail::YamlNodeRef& yaml, int depth) {
    return TaskYamlDetail::parse_orch_node(yaml, depth);
}
