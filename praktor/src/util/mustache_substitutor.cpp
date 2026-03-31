#include "util/mustache_substitutor.hpp"
#include "mustache.h"
#include "util/logging.hpp"
#include <vector>
#include <memory>
#include <cstring>

namespace Praktor::Util {

struct MustacheNode {
    WorkflowValue value;
    std::string path;
};

struct MustacheContext {
    const WorkflowContext& workflow_context;
    std::vector<std::unique_ptr<MustacheNode>> nodes;

    MustacheNode* createNode(WorkflowValue val, std::string p = "") {
        auto node = std::make_unique<MustacheNode>();
        node->value = std::move(val);
        node->path = std::move(p);
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
    return ctx->createNode(jsoncons::json::object(), "");
}

static void *bridge_get_child_by_name(void *node_ptr, const char *name, size_t size, void *provider_data) {
    MustacheNode *node = (MustacheNode *)node_ptr;
    MustacheContext *ctx = (MustacheContext *)provider_data;
    std::string key(name, size);

    std::string new_path = node->path.empty() ? key : node->path + "." + key;
    logd("bridge_get_child_by_name: key='{}', node->path='{}' -> new_path='{}'", key, node->path, new_path);

    WorkflowValue val = ctx->workflow_context.getValueByPath(new_path);
    if (!val.is_null()) {
        // TLOG_INFO("mustache: found path='{}', val='{}'", new_path, val.to_string());
        return ctx->createNode(val, new_path);
    }
    
    // Mustache search behavior: if not found relative to current scope, try root
    if (!node->path.empty()) {
        logd("bridge_get_child_by_name: path '{}' returned null, trying root-relative lookup for key '{}'", new_path, key);
        WorkflowValue root_val = ctx->workflow_context.getValueByPath(key);
        if (!root_val.is_null()) {
            logd("bridge_get_child_by_name: found root-relative value for key '{}'", key);
            return ctx->createNode(root_val, key);
        }
    }
    
    logd("bridge_get_child_by_name: FAILED to find value for key '{}' (tried paths: '{}' and '{}')", key, new_path, key);
    return nullptr;
}

static void* bridge_get_child_by_index(void* node_ptr, unsigned index, void* provider_data) {
    MustacheContext* ctx = static_cast<MustacheContext*>(provider_data);
    MustacheNode* node = static_cast<MustacheNode*>(node_ptr);

    if (node && node->value.is_array() && index < node->value.size()) {
        return ctx->createNode(node->value.at(index), node->path + "[" + std::to_string(index) + "]");
    }
    
    // Mustache spec: single values are iterable once
    if (node && index == 0 && !node->value.is_null()) {
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
        loge("Failed to compile mustache template: {}", templateStr);
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
        loge("Failed to process mustache template: {}", templateStr);
        mustache_release(templ);
        return templateStr;
    }

    mustache_release(templ);
    return renderer.result;
}

} // namespace Praktor::Util
