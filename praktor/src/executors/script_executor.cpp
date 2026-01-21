
#include "executors/script_executor.hpp"
#include "dag/workflow_context.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"


#include <js_module.h>
#include <quickjs.h>


#include <jsoncons/json.hpp>
#include <memory>
#include <sstream>
#include <variant>
#include <vector>


namespace {

struct JSOpaque {
  std::ostringstream oss;
  WorkflowContext *context;
  const Task *task;
};

JSValue jsonToValue(JSContext *ctx, const jsoncons::json &j) {
  if (j.is_null())
    return JS_NULL;
  if (j.is_bool())
    return JS_NewBool(ctx, j.as<bool>());
  if (j.is_int64())
    return JS_NewInt64(ctx, j.as<int64_t>());
  if (j.is_double())
    return JS_NewFloat64(ctx, j.as<double>());
  if (j.is_string())
    return JS_NewString(ctx, j.as<std::string>().c_str());

  std::string s = j.to_string();
  return JS_ParseJSON(ctx, s.c_str(), s.size(), "<json>");
}

jsoncons::json valueToJson(JSContext *ctx, JSValue val) {
  if (JS_IsNull(val) || JS_IsUndefined(val))
    return jsoncons::json::null();
  if (JS_IsBool(val))
    return jsoncons::json(JS_ToBool(ctx, val) != 0);
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
  auto res = jsoncons::json::parse(str);
  JS_FreeCString(ctx, str);
  JS_FreeValue(ctx, strVal);
  return res;
}

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSOpaque *opaque = (JSOpaque *)JS_GetContextOpaque(ctx);
  for (int i = 0; i < argc; i++) {
    const char *str = JS_ToCString(ctx, argv[i]);
    if (str) {
      if (i > 0)
        opaque->oss << " ";
      opaque->oss << str;
      JS_FreeCString(ctx, str);
    }
  }
  opaque->oss << "\n";
  return JS_UNDEFINED;
}

static JSValue js_fail(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  const char *msg = argc > 0 ? JS_ToCString(ctx, argv[0]) : "Script failure";
  JSValue err = JS_NewError(ctx);
  JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, msg));
  if (argc > 0)
    JS_FreeCString(ctx, msg);
  return JS_Throw(ctx, err);
}

static JSValue js_context_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSOpaque *opaque = (JSOpaque *)JS_GetContextOpaque(ctx);
  if (argc < 1)
    return JS_UNDEFINED;
  const char *key = JS_ToCString(ctx, argv[0]);
  if (!key)
    return JS_UNDEFINED;
  auto val = opaque->context->getValueByPath(key);
  JS_FreeCString(ctx, key);
  return jsonToValue(ctx, val);
}

static JSValue js_context_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSOpaque *opaque = (JSOpaque *)JS_GetContextOpaque(ctx);
  if (argc < 2)
    return JS_UNDEFINED;
  const char *key = JS_ToCString(ctx, argv[0]);
  if (!key)
    return JS_UNDEFINED;
  auto val = valueToJson(ctx, argv[1]);
  opaque->context->setCurrentTaskOutput(key, val);
  JS_FreeCString(ctx, key);
  return JS_TRUE;
}

static JSValue js_context_has(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSOpaque *opaque = (JSOpaque *)JS_GetContextOpaque(ctx);
  if (argc < 1)
    return JS_FALSE;
  const char *key = JS_ToCString(ctx, argv[0]);
  if (!key)
    return JS_FALSE;
  bool exists = opaque->context->hasKey(key);
  JS_FreeCString(ctx, key);
  return JS_NewBool(ctx, exists);
}

bool hasES6Import(const std::string &source) {
  // Simple heuristic: check for import statements
  // Matches: import x from, import { x }, import 'x', import "x"
  size_t pos = 0;
  while ((pos = source.find("import", pos)) != std::string::npos) {
    // Check if 'import' is at start of line or after whitespace/newline
    if (pos == 0 || source[pos - 1] == '\n' || source[pos - 1] == ' ' || source[pos - 1] == '\t') {
      size_t after = pos + 6;
      if (after < source.size() && (source[after] == ' ' || source[after] == '\t' ||
                                    source[after] == '\'' || source[after] == '"')) {
        return true;
      }
    }
    pos++;
  }
  return false;
}

} // namespace

namespace Praktor::Execution {

TaskResult ScriptExecutor::execute(const Task &task, WorkflowContext &context) {
  TLOG_INFO("Executing script (QuickJS): {}", task.name);

  const auto *params_ptr = std::get_if<ScriptParams>(&task.specifics);
  if (!params_ptr) {
    return TaskResult(false, "Task does not contain ScriptParams");
  }
  const ScriptParams &params = *params_ptr;

  if (params.language != "javascript") {
    return TaskResult(false, "Unsupported script language: " + params.language);
  }

  JSRuntime *rt = JS_NewRuntime();
  JSContext *ctx = JS_NewContext(rt);

  JSOpaque opaque;
  opaque.context = &context;
  opaque.task = &task;
  JS_SetContextOpaque(ctx, &opaque);

  logd("Initializing TurboNet module");
  js_init_turbo_module(ctx);

  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Register basic functions
  JS_SetPropertyStr(ctx, global_obj, "print", JS_NewCFunction(ctx, js_print, "print", 1));
  JS_SetPropertyStr(ctx, global_obj, "echo", JS_NewCFunction(ctx, js_print, "echo", 1));
  JS_SetPropertyStr(ctx, global_obj, "fail", JS_NewCFunction(ctx, js_fail, "fail", 1));

  // Register console.log
  JSValue console = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
  JS_SetPropertyStr(ctx, global_obj, "console", console);

  // Register context object
  JSValue context_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, context_obj, "get", JS_NewCFunction(ctx, js_context_get, "get", 1));
  JS_SetPropertyStr(ctx, context_obj, "set", JS_NewCFunction(ctx, js_context_set, "set", 2));
  JS_SetPropertyStr(ctx, context_obj, "has", JS_NewCFunction(ctx, js_context_has, "has", 1));
  JS_SetPropertyStr(ctx, global_obj, "context", context_obj);

  // Set globals
  for (const auto &[key, value] : params.globals) {
    std::string substituted = substituteVariables(value, context);
    JS_SetPropertyStr(ctx, global_obj, key.c_str(), JS_NewString(ctx, substituted.c_str()));
  }

  // Load embedded modules
  for (const auto &module_name : params.modules) {
    const EmbeddedModule *module = context.getEmbeddedModule(module_name);
    if (module && module->language == "javascript") {
      logd("Loading embedded module: {}", module_name);
      std::string source = substituteVariables(module->source, context);
      JSValue val =
          JS_Eval(ctx, source.c_str(), source.size(), module_name.c_str(), JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(val)) {
        // Handle error
        JSValue exception = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exception);
        TLOG_ERROR("Error loading module {}: {}", module_name, (msg ? msg : "unknown error"));
        JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exception);
      }
      JS_FreeValue(ctx, val);
    }
  }

  JS_FreeValue(ctx, global_obj);

  // Execute the main source
  std::string source = substituteVariables(params.source, context);

  // Detect if source uses ES6 modules
  bool is_module = hasES6Import(source);
  int eval_flags = is_module ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL;
  
  logd("Evaluating script source");
  JSValue result_val = JS_Eval(ctx, source.c_str(), source.size(), task.name.c_str(), eval_flags);
  logd("Script evaluation finished");

  // For modules, the result is a promise that resolves when the module is loaded
  // We need to process pending jobs to execute the module
  if (is_module && !JS_IsException(result_val)) {
    logd("Processing pending jobs for module");
    js_turbo_process_events(ctx);
  }

  TaskResult task_result(true);
  task_result.exit_code = 0;

  if (JS_IsException(result_val)) {
    JSValue exception = JS_GetException(ctx);
    const char *msg = JS_ToCString(ctx, exception);
    task_result.success = false;
    task_result.error_message = msg ? msg : "Unknown script error";
    JS_FreeCString(ctx, msg);

    // Also get line number if available
    JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
    if (!JS_IsUndefined(stack)) {
      const char *stack_str = JS_ToCString(ctx, stack);
      if (stack_str) {
        task_result.error_message += "\nStack trace:\n" + std::string(stack_str);
        JS_FreeCString(ctx, stack_str);
      }
    }
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
  } else {
    // Run loop to process any async tasks
    js_turbo_process_events(ctx);

    std::string stdout_data = opaque.oss.str();
    if (!JS_IsUndefined(result_val) && !JS_IsNull(result_val)) {
      const char *res_str = JS_ToCString(ctx, result_val);
      if (res_str) {
        if (!stdout_data.empty() && stdout_data.back() != '\n')
          stdout_data += "\n";
        stdout_data += res_str;
        JS_FreeCString(ctx, res_str);
      }
    }
    task_result.stdout_data = stdout_data;
  }

  JS_FreeValue(ctx, result_val);
  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (task_result.success) {
    logd("Applying script outputs for task: {}", task.name);
    applyOutputs(task, task_result, context);
  }

  logd("Script task '{}' finished with success={}", task.name, task_result.success);
  return task_result;
}

void ScriptExecutor::applyOutputs(const Task &task, const TaskResult &result,
                                  WorkflowContext &context) {
  jsoncons::json outputs = jsoncons::json::object();
  outputs["exit_code"] = result.exit_code;
  if (!result.stdout_data.empty()) {
    outputs["stdout"] = result.stdout_data;
  }
  if (!result.stderr_data.empty()) {
    outputs["stderr"] = result.stderr_data;
  }
  context.mergeTaskOutputs(task.name, outputs);
}

std::unique_ptr<TaskExecutor> createScriptExecutor() { return std::make_unique<ScriptExecutor>(); }

} // namespace Praktor::Execution
