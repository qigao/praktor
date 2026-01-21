#include "executors/script_evaluator.hpp"
#include "util/logging.hpp"
#include <js_module.h>
#include <jsoncons/json.hpp>

namespace Praktor::Execution {

namespace {
    JSValue jsonToValue(JSContext *ctx, const jsoncons::json &j) {
      if (j.is_null()) return JS_NULL;
      if (j.is_bool()) return JS_NewBool(ctx, j.as<bool>());
      if (j.is_int64()) return JS_NewInt64(ctx, j.as<int64_t>());
      if (j.is_double()) return JS_NewFloat64(ctx, j.as<double>());
      if (j.is_string()) return JS_NewString(ctx, j.as<std::string>().c_str());

      std::string s = j.to_string();
      return JS_ParseJSON(ctx, s.c_str(), s.size(), "<json>");
    }

    jsoncons::json valueToJson(JSContext *ctx, JSValue val) {
      if (JS_IsNull(val) || JS_IsUndefined(val)) return jsoncons::json::null();
      if (JS_IsBool(val)) return jsoncons::json(JS_ToBool(ctx, val) != 0);
      if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        return jsoncons::json(d);
      }
      if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        std::string s(str);
        JS_FreeCString(ctx, str);
        return jsoncons::json(s);
      }

      JSValue strVal = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
      if (JS_IsException(strVal) || JS_IsNull(strVal)) {
        JS_FreeValue(ctx, strVal);
        return jsoncons::json::null();
      }
      const char *str = JS_ToCString(ctx, strVal);
      // Handle edge case where stringify returns "undefined" as a string or empty
      if (!str) return jsoncons::json::null();
      
      try {
        auto res = jsoncons::json::parse(str);
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, strVal);
        return res;
      } catch(...) {
          JS_FreeCString(ctx, str);
          JS_FreeValue(ctx, strVal);
          return jsoncons::json::null();
      }
    }
}

ScriptEvaluator::ScriptEvaluator(WorkflowContext& context, const std::string& task_name)
    : context_(context), task_name_(task_name) {
    rt_ = JS_NewRuntime();
    ctx_ = JS_NewContext(rt_);
    opaque_ = {this, &context};
    JS_SetContextOpaque(ctx_, &opaque_);

    js_init_turbo_module(ctx_);

    JSValue global_obj = JS_GetGlobalObject(ctx_);

    // Basic functions
    JS_SetPropertyStr(ctx_, global_obj, "print", JS_NewCFunction(ctx_, js_print, "print", 1));
    JS_SetPropertyStr(ctx_, global_obj, "echo", JS_NewCFunction(ctx_, js_print, "echo", 1));
    JS_SetPropertyStr(ctx_, global_obj, "fail", JS_NewCFunction(ctx_, js_fail, "fail", 1));

    // Console
    JSValue console = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, console, "log", JS_NewCFunction(ctx_, js_print, "log", 1));
    JS_SetPropertyStr(ctx_, global_obj, "console", console);

    // Context
    JSValue context_obj = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, context_obj, "get", JS_NewCFunction(ctx_, js_context_get, "get", 1));
    JS_SetPropertyStr(ctx_, context_obj, "set", JS_NewCFunction(ctx_, js_context_set, "set", 2));
    JS_SetPropertyStr(ctx_, context_obj, "has", JS_NewCFunction(ctx_, js_context_has, "has", 1));
    JS_SetPropertyStr(ctx_, global_obj, "context", context_obj);

    JS_FreeValue(ctx_, global_obj);
}

ScriptEvaluator::~ScriptEvaluator() {
    JS_FreeContext(ctx_);
    JS_FreeRuntime(rt_);
}

void ScriptEvaluator::registerGlobalFunction(const std::string& name, JSCFunction* func, int length) {
    JSValue global_obj = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global_obj, name.c_str(), JS_NewCFunction(ctx_, func, name.c_str(), length));
    JS_FreeValue(ctx_, global_obj);
}

void ScriptEvaluator::registerGlobalObject(const std::string& name, JSValue obj) {
    JSValue global_obj = JS_GetGlobalObject(ctx_);
    JS_SetPropertyStr(ctx_, global_obj, name.c_str(), obj);
    JS_FreeValue(ctx_, global_obj);
}

JSValue ScriptEvaluator::js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    Opaque *opaque = (Opaque *)JS_GetContextOpaque(ctx);
    for (int i = 0; i < argc; i++) {
        const char *str = JS_ToCString(ctx, argv[i]);
        if (str) {
            if (i > 0) opaque->evaluator->output_stream_ << " ";
            opaque->evaluator->output_stream_ << str;
            JS_FreeCString(ctx, str);
        }
    }
    opaque->evaluator->output_stream_ << "\n";
    return JS_UNDEFINED;
}

JSValue ScriptEvaluator::js_fail(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *msg = argc > 0 ? JS_ToCString(ctx, argv[0]) : "Script failure";
    JSValue err = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg));
    if (argc > 0) JS_FreeCString(ctx, msg);
    return JS_Throw(ctx, err);
}

JSValue ScriptEvaluator::js_context_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    Opaque *opaque = (Opaque *)JS_GetContextOpaque(ctx);
    if (argc < 1) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_UNDEFINED;
    auto val = opaque->context->getValueByPath(key);
    JS_FreeCString(ctx, key);
    return jsonToValue(ctx, val);
}

JSValue ScriptEvaluator::js_context_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    Opaque *opaque = (Opaque *)JS_GetContextOpaque(ctx);
    if (argc < 2) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_UNDEFINED;
    auto val = valueToJson(ctx, argv[1]);
    opaque->context->setCurrentTaskOutput(key, val);
    JS_FreeCString(ctx, key);
    return JS_TRUE;
}

JSValue ScriptEvaluator::js_context_has(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    Opaque *opaque = (Opaque *)JS_GetContextOpaque(ctx);
    if (argc < 1) return JS_FALSE;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_FALSE;
    bool exists = opaque->context->hasKey(key);
    JS_FreeCString(ctx, key);
    return JS_NewBool(ctx, exists);
}

bool ScriptEvaluator::execute(const std::string& script, std::string& output, std::string& error) {
    // Basic heuristics for module detection could be added here if needed
    // For now we assume snippet execution is GLOBAL unless explicitly using import?
    // Let's assume GLOBAL for snippets in http hooks.
    
    JSValue result_val = JS_Eval(ctx_, script.c_str(), script.size(), task_name_.c_str(), JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result_val)) {
        JSValue exception = JS_GetException(ctx_);
        const char *msg = JS_ToCString(ctx_, exception);
        error = msg ? msg : "Unknown script error";
        JS_FreeCString(ctx_, msg);
        
        JSValue stack = JS_GetPropertyStr(ctx_, exception, "stack");
        if (!JS_IsUndefined(stack)) {
            const char *stack_str = JS_ToCString(ctx_, stack);
            if (stack_str) {
                error += "\nStack trace:\n" + std::string(stack_str);
                JS_FreeCString(ctx_, stack_str);
            }
        }
        JS_FreeValue(ctx_, stack);
        JS_FreeValue(ctx_, exception);
        JS_FreeValue(ctx_, result_val);
        return false;
    }

    // Process events
    js_turbo_process_events(ctx_);
    
    output = output_stream_.str();
    JS_FreeValue(ctx_, result_val);
    return true;
}

} // namespace Praktor::Execution
