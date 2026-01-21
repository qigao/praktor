#include "executors/http_executor.hpp"
#include "dag/workflow_context.hpp"
#include "executors/script_evaluator.hpp"
#include "util/logging.hpp"
#include "util/variable_substitution.hpp"

#include <algorithm>
#include <http_client.h>
#include <jsoncons/json.hpp>
#include <variant>
#include <vector>

namespace Praktor::Execution {

namespace {

struct RequestContext {
  std::string url;
  std::string method;
  std::string body;
  std::vector<std::string> headers;
};

struct ResponseContext {
  int status;
  std::string body;
  // Note: For binary responses with null bytes, response.body will be truncated.
  // Consider base64-encoding binary data in the API response itself.
};

// --- Request JS Bindings ---

static JSValue js_req_get_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  return JS_NewString(ctx, rc->url.c_str());
}

static JSValue js_req_set_url(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  const char *str = JS_ToCString(ctx, argv[0]);
  if (str) {
    rc->url = str;
    JS_FreeCString(ctx, str);
  }
  return JS_UNDEFINED;
}

static JSValue js_req_get_method(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  return JS_NewString(ctx, rc->method.c_str());
}

static JSValue js_req_set_method(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  const char *str = JS_ToCString(ctx, argv[0]);
  if (str) {
    rc->method = str;
    JS_FreeCString(ctx, str);
  }
  return JS_UNDEFINED;
}

static JSValue js_req_get_body(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  return JS_NewString(ctx, rc->body.c_str());
}

static JSValue js_req_set_body(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 1)
    return JS_UNDEFINED;
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  const char *str = JS_ToCString(ctx, argv[0]);
  if (str) {
    rc->body = str;
    JS_FreeCString(ctx, str);
  }
  return JS_UNDEFINED;
}

static JSValue js_req_add_header(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  RequestContext *rc = (RequestContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  if (argc < 2)
    return JS_UNDEFINED;
  const char *key = JS_ToCString(ctx, argv[0]);
  const char *val = JS_ToCString(ctx, argv[1]);
  if (key && val) {
    rc->headers.push_back(std::string(key) + ": " + std::string(val));
  }
  JS_FreeCString(ctx, key);
  JS_FreeCString(ctx, val);
  return JS_UNDEFINED;
}

// --- Response JS Bindings ---

static JSValue js_res_get_status(JSContext *ctx, JSValueConst this_val, int argc,
                                 JSValueConst *argv) {
  ResponseContext *rc = (ResponseContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  return JS_NewInt32(ctx, rc->status);
}

static JSValue js_res_get_body(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  ResponseContext *rc = (ResponseContext *)JS_GetOpaque(this_val, JS_GetClassID(this_val));
  if (!rc)
    return JS_UNDEFINED;
  return JS_NewString(ctx, rc->body.c_str());
}

static JSClassDef req_class_def = {"Request", nullptr, nullptr, nullptr, nullptr};
static JSClassDef res_class_def = {"Response", nullptr, nullptr, nullptr, nullptr};

} // namespace

TaskResult HttpExecutor::execute(const Task &task, WorkflowContext &context) {
  TLOG_INFO("Executing http task: {}", task.name);

  auto *params_ptr = std::get_if<HttpParams>(&task.specifics);
  if (!params_ptr) {
    return TaskResult(false, "Task does not contain HttpParams");
  }
  const HttpParams &params = *params_ptr;

  RequestContext req_ctx;
  req_ctx.url = substituteVariables(params.url, context);
  req_ctx.method = substituteVariables(params.method, context);

  for (const auto &[key, value] : params.headers) {
    std::string sub_val = substituteVariables(value, context);
    req_ctx.headers.push_back(key + ": " + sub_val);
  }

  req_ctx.body = substituteVariables(params.body, context);

  std::unique_ptr<ScriptEvaluator> evaluator;
  auto get_evaluator = [&]() -> ScriptEvaluator & {
    if (!evaluator) {
      evaluator = std::make_unique<ScriptEvaluator>(context, task.name);
    }
    return *evaluator;
  };

  // 2. Pre-Request Script
  if (params.script && !params.script->empty()) {
    ScriptEvaluator &eval = get_evaluator();
    JSContext *ctx = eval.ctx();

    static JSClassID req_class_id = 0;
    JS_NewClassID(JS_GetRuntime(ctx), &req_class_id);

    if (!JS_IsRegisteredClass(JS_GetRuntime(ctx), req_class_id)) {
      if (JS_NewClass(JS_GetRuntime(ctx), req_class_id, &req_class_def) < 0) {
        TLOG_ERROR("Failed to register JS class Request (ID={})", req_class_id);
        return TaskResult(false, "Failed to register JS class Request");
      }
    }

    JSValue req_proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, req_proto, "addHeader",
                      JS_NewCFunction(ctx, js_req_add_header, "addHeader", 2));

    JSAtom url_atom = JS_NewAtom(ctx, "url");
    JS_DefinePropertyGetSet(ctx, req_proto, url_atom,
                            JS_NewCFunction(ctx, js_req_get_url, "get_url", 0),
                            JS_NewCFunction(ctx, js_req_set_url, "set_url", 1),
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, url_atom);

    JSAtom method_atom = JS_NewAtom(ctx, "method");
    JS_DefinePropertyGetSet(ctx, req_proto, method_atom,
                            JS_NewCFunction(ctx, js_req_get_method, "get_method", 0),
                            JS_NewCFunction(ctx, js_req_set_method, "set_method", 1),
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, method_atom);

    JSAtom body_atom = JS_NewAtom(ctx, "body");
    JS_DefinePropertyGetSet(ctx, req_proto, body_atom,
                            JS_NewCFunction(ctx, js_req_get_body, "get_body", 0),
                            JS_NewCFunction(ctx, js_req_set_body, "set_body", 1),
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, body_atom);

    JSValue req_obj = JS_NewObjectClass(ctx, req_class_id);
    JS_SetOpaque(req_obj, &req_ctx);
    JS_SetPrototype(ctx, req_obj, req_proto);
    JS_FreeValue(ctx, req_proto);

    eval.registerGlobalObject("request", req_obj);

    TLOG_INFO("Executing pre-request script");
    std::string output, error;
    if (!eval.execute(*params.script, output, error)) {
      TLOG_ERROR("Pre-request script failed: {}", error);
      return TaskResult(false, "Pre-request script failed: " + error);
    }
    TLOG_INFO("Pre-request script passed");
  }

  http_client_t *client = http_client_create();
  if (!client)
    return TaskResult(false, "Failed to create http_client");

  http_client_set_timeout(client, params.timeout_ms);
  http_client_follow_redirects(client, params.follow_redirects ? 1 : 0);

  if (params.auth_user && params.auth_pass) {
    std::string user = substituteVariables(*params.auth_user, context);
    std::string pass = substituteVariables(*params.auth_pass, context);
    http_client_set_basic_auth(client, user.c_str(), pass.c_str());
  }
  if (params.bearer_token) {
    std::string token = substituteVariables(*params.bearer_token, context);
    http_client_set_bearer_token(client, token.c_str());
  }

  std::vector<const char *> headers_ptr;
  for (const auto &h : req_ctx.headers)
    headers_ptr.push_back(h.c_str());

  // Convert method to uppercase for case-insensitive comparison
  std::string method_upper = req_ctx.method;
  std::transform(method_upper.begin(), method_upper.end(), method_upper.begin(), ::toupper);

  http_method_t method = HTTP_GET;
  if (method_upper == "POST")
    method = HTTP_POST;
  else if (method_upper == "PUT")
    method = HTTP_PUT;
  else if (method_upper == "DELETE")
    method = HTTP_DELETE;
  else if (method_upper == "PATCH")
    method = HTTP_PATCH;
  else if (method_upper == "HEAD")
    method = HTTP_HEAD;
  else if (method_upper == "OPTIONS")
    method = HTTP_OPTIONS;

  TLOG_DEBUG("Requesting {} {}", req_ctx.method, req_ctx.url);

  http_response_t *response = http_request(
      client, method, req_ctx.url.c_str(), headers_ptr.empty() ? nullptr : headers_ptr.data(),
      static_cast<int>(headers_ptr.size()), req_ctx.body.empty() ? nullptr : req_ctx.body.c_str(),
      req_ctx.body.size());

  if (!response) {
    http_client_destroy(client);
    return TaskResult(false, "No response from server");
  }

  if (response->error) {
    TaskResult res(false, response->error);
    res.exit_code = response->status_code;
    http_response_free(response);
    http_client_destroy(client);
    return res;
  }

  ResponseContext res_ctx;
  res_ctx.status = response->status_code;
  res_ctx.body = response->body ? std::string(response->body, response->body_len) : "";

  bool success = (response->status_code >= 200 && response->status_code < 300);

  if (params.test && !params.test->empty()) {
    ScriptEvaluator &eval = get_evaluator();
    JSContext *ctx = eval.ctx();

    static JSClassID res_class_id = 0;
    JS_NewClassID(JS_GetRuntime(ctx), &res_class_id);

    if (!JS_IsRegisteredClass(JS_GetRuntime(ctx), res_class_id)) {
      if (JS_NewClass(JS_GetRuntime(ctx), res_class_id, &res_class_def) < 0) {
        http_response_free(response);
        http_client_destroy(client);
        TLOG_ERROR("Failed to register JS class Response (ID={})", res_class_id);
        return TaskResult(false, "Failed to register JS class Response");
      }
    }

    JSValue res_proto = JS_NewObject(ctx);

    JSAtom status_atom = JS_NewAtom(ctx, "status");
    JS_DefinePropertyGetSet(ctx, res_proto, status_atom,
                            JS_NewCFunction(ctx, js_res_get_status, "get_status", 0), JS_UNDEFINED,
                            JS_PROP_CONFIGURABLE); // Non-enumerable to avoid GC issues
    JS_FreeAtom(ctx, status_atom);

    JSAtom body_atom = JS_NewAtom(ctx, "body");
    JS_DefinePropertyGetSet(ctx, res_proto, body_atom,
                            JS_NewCFunction(ctx, js_res_get_body, "get_body", 0), JS_UNDEFINED,
                            JS_PROP_CONFIGURABLE); // Non-enumerable to avoid GC issues
    JS_FreeAtom(ctx, body_atom);

    JSValue res_obj = JS_NewObjectClass(ctx, res_class_id);
    JS_SetOpaque(res_obj, &res_ctx);
    JS_SetPrototype(ctx, res_obj, res_proto);
    JS_FreeValue(ctx, res_proto);

    eval.registerGlobalObject("response", res_obj);

    std::string output, error;
    if (!eval.execute(*params.test, output, error)) {
      http_response_free(response);
      http_client_destroy(client);
      TLOG_ERROR("Test script failed: {}", error);
      return TaskResult(false, "Test script failed: " + error);
    }

    // If test script passes, override success regardless of HTTP status code.
    // This allows testing error conditions (e.g., validating a 404 response is correct).
    success = true;
  }

  TaskResult result(success);
  result.exit_code = response->status_code;
  result.stdout_data = res_ctx.body;

  applyOutputs(task, result, context);

  http_response_free(response);
  http_client_destroy(client);

  // Reset evaluator to ensure JS objects using stack contexts (req_ctx, res_ctx) are freed
  // before the contexts themselves are destroyed.
  if (evaluator) {
    evaluator.reset();
  }

  TLOG_INFO("Http task '{}' finished with status {}", task.name, result.exit_code);
  return result;
}

void HttpExecutor::applyOutputs(const Task &task, const TaskResult &result,
                                WorkflowContext &context) {
  jsoncons::json outputs = jsoncons::json::object();
  outputs["status"] = result.exit_code;
  outputs["body"] = result.stdout_data;

  if (!result.stdout_data.empty()) {
    try {
      outputs["data"] = jsoncons::json::parse(result.stdout_data);
    } catch (const std::exception &e) {
      TLOG_WARN("Failed to parse JSON body for task '{}': {}", task.name, e.what());
    }
  } else {
    TLOG_WARN("Empty response body for task '{}'", task.name);
  }

  outputs["success"] = result.success;

  for (const auto &item : outputs.object_range()) {
    context.setCurrentTaskOutput(item.key(), item.value());
  }
}

std::unique_ptr<TaskExecutor> createHttpExecutor() { return std::make_unique<HttpExecutor>(); }

} // namespace Praktor::Execution
