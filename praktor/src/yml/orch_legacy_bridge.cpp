#include "yml/task_yaml.hpp"
#include "yml/orch_tree.h"
#include <memory>
#include <new>
#include <salts/error_codes.h>
#include <stdexcept>

namespace {
OrchNodeSpec project_spec(praktor_orch_type type) {
    const auto* spec = praktor_orch_spec_for(type);
    OrchNodeSpec result;
    result.name = spec->name;
    result.isControl = spec->control;
    result.allowsChildren = spec->allows_children;
    for (const char* key : spec->required) if (key) result.requiredParams.emplace_back(key);
    for (const char* key : spec->optional) if (key) result.optionalParams.emplace_back(key);
    return result;
}
void require_orch_view(int status) {
    if (status != SALTS_OK) throw std::logic_error("Invalid native orchestration projection");
}
OrchNode project_node(const praktor_orch_node* node) {
    OrchNode result;
    const auto* spec = praktor_orch_spec_for(praktor_orch_node_type(node));
    if (!spec) throw std::logic_error("Invalid native orchestration type");
    result.type = spec->name;
    for (std::size_t index = 0; index < praktor_orch_param_count(node); ++index) {
        praktor_orch_param param{};
        require_orch_view(praktor_orch_param_at(node, index, &param));
        result.params[std::string(param.key.data, param.key.len)] = std::string(param.value.data, param.value.len);
    }
    for (std::size_t index = 0; index < praktor_orch_child_count(node); ++index) {
        const praktor_orch_node* child = nullptr;
        require_orch_view(praktor_orch_child_at(node, index, &child));
        result.children.push_back(project_node(child));
    }
    return result;
}
vstr borrowed(const std::string& text) { return vstr_from_buf(text.data(), text.size()); }
struct OrchDeleter {
    void operator()(praktor_orch_tree* tree) const noexcept { praktor_orch_tree_destroy(tree); }
};
} // namespace

// Explicit temporary STL projections. Keep the initializer-list construction
// and original declaration order so existing platform-specific registry iteration
// (including supported-type diagnostics) stays unchanged.
const std::unordered_map<std::string, OrchNodeSpec> NODE_REGISTRY = {
#define PRAKTOR_ORCH_NODE(id, name, control, children, required, optional) {name, project_spec(PRAKTOR_ORCH_##id)},
#include "yml/orch_nodes.def"
#undef PRAKTOR_ORCH_NODE
};
const OrchNodeSpec* findNodeSpec(const std::string& name) {
    praktor_orch_type type{};
    if (praktor_orch_find_type(borrowed(name), &type) != SALTS_OK) return nullptr;
    const auto* spec = praktor_orch_spec_for(type);
    const auto found = NODE_REGISTRY.find(spec->name);
    return found == NODE_REGISTRY.end() ? nullptr : &found->second;
}
bool OrchNode::isControlNode() const {
    const auto* spec = findNodeSpec(type);
    return spec && spec->isControl;
}
bool OrchNode::isLeafNode() const {
    const auto* spec = findNodeSpec(type);
    return spec && !spec->isControl;
}
OrchParams desugarCommandToorch(const RunCommandParams& params) {
    praktor_orch_command input{};
    std::vector<vstr> commands;
    input.sequence = std::holds_alternative<StrList>(params.command);
    if (input.sequence) {
        const auto& source = std::get<StrList>(params.command);
        input.command_count = source.size();
        if (source.size() <= PRAKTOR_ORCH_MAX_COMMANDS) {
            commands.reserve(source.size());
            for (const auto& command : source) commands.push_back(borrowed(command));
            input.commands = commands.data();
        }
    } else input.command = borrowed(std::get<std::string>(params.command));
    input.output_format = static_cast<praktor_yaml_output_format>(params.output_format);
    if (params.parse_regex) {
        input.regex = {true, borrowed(params.parse_regex->pattern), borrowed(params.parse_regex->output_key), params.parse_regex->capture_group};
    }
    if (params.parse_json) input.json = {true, borrowed(params.parse_json->path), borrowed(params.parse_json->output_key)};
    if (params.parse_lines) input.lines = {true, borrowed(params.parse_lines->filter), borrowed(params.parse_lines->output_key)};
    if (params.parse_keyvalue) input.keyvalue = {true, borrowed(params.parse_keyvalue->delimiter), borrowed(params.parse_keyvalue->line_separator), borrowed(params.parse_keyvalue->output_key)};
    praktor_orch_tree* raw = nullptr;
    char diagnostic[256]{};
    const int status = praktor_orch_from_command(&input, &raw, diagnostic, sizeof diagnostic);
    std::unique_ptr<praktor_orch_tree, OrchDeleter> owner(raw);
    if (status == SALTS_ENOMEM) throw std::bad_alloc();
    if (status != SALTS_OK) throw std::runtime_error(diagnostic);
    return OrchParams{project_node(praktor_orch_tree_root(owner.get()))};
}
