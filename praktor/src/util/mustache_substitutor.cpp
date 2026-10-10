#include "util/mustache_substitutor.hpp"
#include "mustache.h"
#include "util/logging.hpp"
#include <vector>
#include <memory>
#include <cstring>
#include <optional>
#include <unordered_set>

namespace Praktor::Util {

struct MustacheNode {
    WorkflowValue value;
    std::string path;
    bool lookup_context_path = true;
};

struct MustacheContext {
    const WorkflowContext& workflow_context;
    std::vector<std::unique_ptr<MustacheNode>> nodes;
    std::optional<std::unordered_set<std::string>> variable_prefixes;

    bool hasVariablePrefix(const std::string& path) {
        if (!variable_prefixes) {
            variable_prefixes.emplace();
            // Flat keys such as variables.name need intermediate nodes during
            // Mustache's component-by-component lookup. Derive them once per
            // render from the context; the context remains the value owner.
            for (const auto& [key, value] : workflow_context.getAllVisibleValues()) {
                for (auto dot = key.find('.'); dot != std::string::npos;
                     dot = key.find('.', dot + 1)) {
                    variable_prefixes->insert(key.substr(0, dot));
                }
            }
        }
        return variable_prefixes->contains(path);
    }

    MustacheNode* createNode(WorkflowValue val, std::string p = "",
                            bool lookup_context_path = true) {
        auto node = std::make_unique<MustacheNode>();
        node->value = std::move(val);
        node->path = std::move(p);
        node->lookup_context_path = lookup_context_path;
        MustacheNode* ptr = node.get();
        nodes.push_back(std::move(node));
        return ptr;
    }
};

static int bridge_dump(void* node_ptr, int (*out_fn)(const char*, size_t, void*), void* renderer_data, void* provider_data) {
    MustacheNode* node = static_cast<MustacheNode*>(node_ptr);
    if (!node) return 0;

    std::string s;
    if (node->value.is_string()) {
        s = node->value.as<std::string>();
    } else {
        s = node->value.to_string();
    }
    
    return out_fn(s.c_str(), s.length(), renderer_data);
}

static void* bridge_get_root(void* provider_data) {
    MustacheContext* ctx = static_cast<MustacheContext*>(provider_data);
    // Root represents the global context. We give it a dummy object value so checks pass.
    return ctx->createNode(WorkflowValue::object(), "");
}

static void *bridge_get_child_by_name(void *node_ptr, const char *name, size_t size, void *provider_data) {
    MustacheNode *node = (MustacheNode *)node_ptr;
    MustacheContext *ctx = (MustacheContext *)provider_data;
    std::string key(name, size);

    std::string new_path = node->path.empty() ? key : node->path + "." + key;
    logdf("bridge_get_child_by_name: key='{}', node->path='{}' -> new_path='{}'", key, node->path, new_path);

    // Prefer exact flat keys, then traverse the value carried by this node.
    // Virtual namespaces and array items need no reconstructed path lookup.
    WorkflowValue val = node->lookup_context_path &&
            (node->path.empty() || ctx->workflow_context.hasKey(new_path))
        ? ctx->workflow_context.getValueByPath(new_path) : WorkflowValue::null();
    if (!val.is_null()) {
        return ctx->createNode(std::move(val), new_path);
    }

    if (node->value.is_object() && node->value.contains(key)) {
        WorkflowValue child = node->value.at(key);
        if (!child.is_null()) {
            return ctx->createNode(std::move(child), new_path, node->lookup_context_path);
        }
    }

    if (node->lookup_context_path && ctx->hasVariablePrefix(new_path)) {
        return ctx->createNode(WorkflowValue::object(), new_path);
    }
    
    // Mustache search behavior: if not found relative to current scope, try root
    if (!node->path.empty()) {
        logdf("bridge_get_child_by_name: path '{}' returned null, trying root-relative lookup for key '{}'", new_path, key);
        WorkflowValue root_val = ctx->workflow_context.getValueByPath(key);
        if (!root_val.is_null()) {
            logdf("bridge_get_child_by_name: found root-relative value for key '{}'", key);
            return ctx->createNode(root_val, key);
        }
    }
    
    logdf("bridge_get_child_by_name: FAILED to find value for key '{}' (tried paths: '{}' and '{}')", key, new_path, key);
    return nullptr;
}

static void* bridge_get_child_by_index(void* node_ptr, unsigned index, void* provider_data) {
    MustacheContext* ctx = static_cast<MustacheContext*>(provider_data);
    MustacheNode* node = static_cast<MustacheNode*>(node_ptr);

    if (!node || node->value.is_null() ||
        (node->value.is_bool() && !node->value.as<bool>())) {
        return nullptr;
    }

    if (node->value.is_array()) {
        if (index >= node->value.size()) return nullptr;
        return ctx->createNode(node->value.at(index), node->path + "[" + std::to_string(index) + "]", false);
    }
    
    // Mustache spec: single values are iterable once
    if (index == 0) {
        return node;
    }

    return nullptr;
}

struct MustacheStringRenderer {
    MUSTACHE_RENDERER base;
    std::string result;
};

static int render_verbatim(const char* output, size_t size, void* renderer_data) {
    MustacheStringRenderer* r = static_cast<MustacheStringRenderer*>(renderer_data);
    r->result.append(output, size);
    return 0;
}

std::string substituteMustache(const std::string& templateStr, const WorkflowContext& context) {
    if (templateStr.find("{{") == std::string::npos) {
        return templateStr;
    }

    MUSTACHE_TEMPLATE* templ = mustache_compile(templateStr.c_str(), templateStr.length(), nullptr, nullptr, 0);
    if (!templ) {
        logef("Failed to compile mustache template: {}", templateStr);
        return templateStr;
    }

    MustacheContext ctx{context};
    MUSTACHE_DATAPROVIDER provider = {};
    provider.dump = bridge_dump;
    provider.get_root = bridge_get_root;
    provider.get_child_by_name = bridge_get_child_by_name;
    provider.get_child_by_index = bridge_get_child_by_index;
    provider.get_partial = nullptr;

    MustacheStringRenderer renderer = {};
    renderer.base.out_verbatim = render_verbatim;
    renderer.base.out_escaped = render_verbatim; // Don't escape for CLI/Shell

    if (mustache_process(templ, &renderer.base, &renderer, &provider, &ctx) != 0) {
        logef("Failed to process mustache template: {}", templateStr);
        mustache_release(templ);
        return templateStr;
    }

    mustache_release(templ);
    return renderer.result;
}

} // namespace Praktor::Util
