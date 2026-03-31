#include "script/script_engine.hpp"
#include "dag/workflow_context.hpp"
#include "turbo_script.h"
extern "C" {
#include "exprtk/exprtk_types.h"
#include "exprtk/exprtk_module.h"
#include "exprtk/exprtk.h"
}
#include "util/logging.hpp"
#include <cctype>
#include <stdexcept>
#include <deque>
#include <string_view>

namespace Praktor::Script {

namespace {

// Replace all occurrences of `from` with `to` that are NOT inside a string literal.
void replace_outside_strings(std::string& source, std::string_view from, std::string_view to) {
    std::string result;
    result.reserve(source.size());
    size_t i = 0;
    bool in_single = false, in_double = false;
    while (i < source.size()) {
        char c = source[i];
        if (!in_single && c == '"') {
            in_double = !in_double;
            result += c;
            ++i;
        } else if (!in_double && c == '\'') {
            in_single = !in_single;
            result += c;
            ++i;
        } else if (!in_single && !in_double &&
                   source.compare(i, from.size(), from.data(), from.size()) == 0) {
            result.append(to.data(), to.size());
            i += from.size();
        } else {
            result += c;
            ++i;
        }
    }
    source = std::move(result);
}

std::string normalize_script_source(std::string source) {
    replace_outside_strings(source, "ctx.get(", "ctx_get(");
    replace_outside_strings(source, "ctx.set(", "ctx_set(");
    replace_outside_strings(source, "ctx.output(", "ctx_output(");
    replace_outside_strings(source, "log.info(", "log_info(");
    replace_outside_strings(source, "log.warn(", "log_warn(");
    replace_outside_strings(source, "log.error(", "log_error(");
    replace_outside_strings(source, "json.stringify(", "json_stringify(");
    replace_outside_strings(source, "json.parse(", "json_parse(");
    replace_outside_strings(source, "trim(", "trim_fn(");
    return source;
}

struct ScriptEvalContext {
    WorkflowContext& workflow_context;
    std::deque<std::string> string_pool;

    exprtk_value_t keep(std::string s) {
        string_pool.push_back(std::move(s));
        const auto& last = string_pool.back();
        exprtk_value_t v{};
        v.type = EXPRTK_VAL_STRING;
        v.data.string.data = const_cast<char*>(last.c_str());
        v.data.string.len = last.length();
        return v;
    }
};

} // namespace

static exprtk_value_t make_null() {
    exprtk_value_t v{};
    v.type = EXPRTK_VAL_NULL;
    return v;
}

static exprtk_value_t make_num(double val) {
    exprtk_value_t v{};
    v.type = EXPRTK_VAL_NUMBER;
    v.data.number = val;
    return v;
}

static exprtk_value_t make_str(const char* str, size_t len) {
    exprtk_value_t v{};
    v.type = EXPRTK_VAL_STRING;
    v.data.string.data = const_cast<char*>(str);
    v.data.string.len = len;
    return v;
}

static std::string trim_copy(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

static jsoncons::json parse_string_or_keep(std::string_view raw) {
    const std::string trimmed = trim_copy(raw);
    if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
        try {
            return jsoncons::json::parse(trimmed);
        } catch (const std::exception&) {
            // Fall through and preserve the original string.
        }
    }
    return std::string(raw);
}

static jsoncons::json exprtk_scalar_to_json(const exprtk_value_t& value) {
    switch (value.type) {
        case EXPRTK_VAL_NUMBER:
            return value.data.number;
        case EXPRTK_VAL_STRING:
            return parse_string_or_keep(std::string_view(value.data.string.data, value.data.string.len));
        case EXPRTK_VAL_VECTOR: {
            jsoncons::json result = jsoncons::json::array();
            for (size_t i = 0; i < value.data.vector.size; ++i) {
                result.push_back(value.data.vector.data[i]);
            }
            return result;
        }
        case EXPRTK_VAL_NULL:
            return jsoncons::json::null();
        case EXPRTK_VAL_MAP: {
            jsoncons::json obj = jsoncons::json::object();
            exprtk_map_iter_t it = exprtk_map_iter_begin(&value);
            const char* key = nullptr;
            exprtk_value_t entry;
            while (exprtk_map_iter_next(&it, &key, &entry)) {
                obj[key] = exprtk_scalar_to_json(entry);
            }
            return obj;
        }
        case EXPRTK_VAL_LIST: {
            jsoncons::json arr = jsoncons::json::array();
            for (size_t i = 0; i < value.data.list.count; ++i) {
                arr.push_back(exprtk_scalar_to_json(value.data.list.items[i]));
            }
            return arr;
        }
        case EXPRTK_VAL_FUNCTION:
            return jsoncons::json::null();
    }
    return jsoncons::json::null();
}

// Native function for json.stringify(value) — converts any TurboScript value to a JSON string
static exprtk_value_t json_stringify_fn(size_t argc, exprtk_value_t* args, void* user_data) {
    if (argc < 1 || !user_data) {
        return make_str("null", 4);
    }
    auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
    jsoncons::json j = exprtk_scalar_to_json(args[0]);
    return eval_ctx->keep(j.to_string());
}

// Native function for json.parse(string) — parses a JSON string (returns string as-is; ctx.output auto-parses)
static exprtk_value_t json_parse_fn(size_t argc, exprtk_value_t* args, void* /*user_data*/) {
    if (argc < 1 || args[0].type != EXPRTK_VAL_STRING) {
        return make_null();
    }
    return args[0];
}

// Native function for trim(string) — removes leading and trailing whitespace
static exprtk_value_t trim_fn_call(size_t argc, exprtk_value_t* args, void* user_data) {
    if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
        return make_null();
    }
    auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
    return eval_ctx->keep(trim_copy(std::string_view(args[0].data.string.data, args[0].data.string.len)));
}

// Native function for ctx.get("path")
static exprtk_value_t ctx_get_fn(size_t argc, exprtk_value_t* args, void* user_data) {
    if (argc != 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
        return make_null();
    }
    auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
    std::string path(args[0].data.string.data, args[0].data.string.len);
    auto val = eval_ctx->workflow_context.getValueByPath(path);

    if (val.is_null()) return make_null();
    if (val.is_bool()) return make_num(val.as_bool() ? 1.0 : 0.0);
    if (val.is_double()) return make_num(val.as_double());
    if (val.is_int64()) return make_num(static_cast<double>(val.as<int64_t>()));
    if (val.is_string()) {
        return eval_ctx->keep(val.as_string());
    }

    // fallback to JSON stringification
    return eval_ctx->keep(val.to_string());
}

// Native function for ctx.set("path", value)
static exprtk_value_t ctx_set_fn(size_t argc, exprtk_value_t* args, void* user_data) {
    if (argc < 2 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
        return make_null();
    }
    auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
    std::string path(args[0].data.string.data, args[0].data.string.len);
    eval_ctx->workflow_context.setValue(path, exprtk_scalar_to_json(args[1]));
    return make_null();
}

static exprtk_value_t ctx_output_fn(size_t argc, exprtk_value_t* args, void* user_data) {
    if (argc < 2 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
        return make_null();
    }
    auto* eval_ctx = static_cast<ScriptEvalContext*>(user_data);
    std::string key(args[0].data.string.data, args[0].data.string.len);
    jsoncons::json val = exprtk_scalar_to_json(args[1]);
    eval_ctx->workflow_context.setCurrentTaskOutput(key, val);
    return make_null();
}

static exprtk_value_t log_message_fn(size_t argc, exprtk_value_t* args, void (*emitter)(std::string_view)) {
    if (argc < 1 || args[0].type != EXPRTK_VAL_STRING) {
        return make_null();
    }

    std::string message(args[0].data.string.data, args[0].data.string.len);
    emitter(message);
    return make_null();
}

static exprtk_value_t log_info_fn(size_t argc, exprtk_value_t* args, void* /*user_data*/) {
    return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

static exprtk_value_t log_warn_fn(size_t argc, exprtk_value_t* args, void* /*user_data*/) {
    return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

static exprtk_value_t log_error_fn(size_t argc, exprtk_value_t* args, void* /*user_data*/) {
    return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

ScriptResult execute(const std::string& source, WorkflowContext& context) {
    ScriptResult result;
    const std::string normalized_source = normalize_script_source(source);

    turbo_script_ctx_t* ctx = turbo_script_init(TURBO_SCRIPT_INIT_DEFAULT);
    if (!ctx) {
        result.success = false;
        result.error_message = "Failed to initialize TurboScript context";
        return result;
    }

    ScriptEvalContext eval_ctx{context, {}};

    // Bind praktor-specific functions
    ts_bind_func(ctx, "ctx_get", ctx_get_fn, &eval_ctx);
    ts_bind_func(ctx, "ctx_set", ctx_set_fn, &eval_ctx);
    ts_bind_func(ctx, "ctx_output", ctx_output_fn, &eval_ctx);
    ts_bind_func(ctx, "log_info", log_info_fn, nullptr);
    ts_bind_func(ctx, "log_warn", log_warn_fn, nullptr);
    ts_bind_func(ctx, "log_error", log_error_fn, nullptr);
    ts_bind_func(ctx, "json_stringify", json_stringify_fn, &eval_ctx);
    ts_bind_func(ctx, "json_parse", json_parse_fn, &eval_ctx);
    ts_bind_func(ctx, "trim_fn", trim_fn_call, &eval_ctx);

    if (turbo_script_run(ctx, normalized_source.c_str()) != 0) {
        result.success = false;
        result.error_message = turbo_script_get_error(ctx);

        ScriptError err;
        err.line = 0;
        err.message = result.error_message;
        result.errors.push_back(err);
    }

    // Cleanup
    turbo_script_free(ctx);

    return result;
}

} // namespace Praktor::Script
