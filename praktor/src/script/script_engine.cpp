#include "script/script_engine.hpp"
#include "actions/shell_executor.hpp"
#include "dag/workflow_context.hpp"
#include "turbo_script.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "exprtk_types.h"
#ifdef __cplusplus
}
#endif

#include "util/logging.hpp"
#include "util/system_info.hpp"
#include "util/turbo_script_runtime.hpp"
#include "data/structured_document_query.hpp"
#include <cctype>
#include <deque>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Praktor::Script {

static WorkflowValue parse_string_or_keep(std::string_view raw);
static WorkflowValue exprtk_scalar_to_json(const exprtk_value_t &value);

namespace {

// Replace all occurrences of `from` with `to` that are NOT inside a string literal.
void replace_outside_strings(std::string &source, std::string_view from, std::string_view to) {
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
  replace_outside_strings(source, "ctx.output_text(", "ctx_output_text(");
  replace_outside_strings(source, "log.info(", "log_info(");
  replace_outside_strings(source, "log.warn(", "log_warn(");
  replace_outside_strings(source, "log.error(", "log_error(");
  replace_outside_strings(source, "json.stringify(", "json_stringify(");
  replace_outside_strings(source, "json.parse(", "json_parse(");
  replace_outside_strings(source, "json.query(", "json_query(");
  replace_outside_strings(source, "shell.exec(", "shell_exec(");
  replace_outside_strings(source, "trim(", "trim_fn(");
  return source;
}

struct ScriptEvalContext {
  WorkflowContext &workflow_context;
  std::deque<std::string> string_pool;
  std::deque<std::vector<exprtk_value_t>> list_pool;
  bool failed = false;
  std::string failure_message;

  exprtk_value_t keep(std::string s) {
    string_pool.push_back(std::move(s));
    const auto &last = string_pool.back();
    exprtk_value_t v{};
    v.type = EXPRTK_VAL_STRING;
    v.data.string.data = const_cast<char *>(last.c_str());
    v.data.string.len = last.length();
    return v;
  }

  exprtk_value_t keepList(std::vector<exprtk_value_t> items) {
    list_pool.push_back(std::move(items));
    auto &last = list_pool.back();
    return turbo_script_value_list_borrowed(last.empty() ? nullptr : last.data(), last.size());
  }
};

std::string resolve_script_working_dir(const WorkflowContext &workflow_context,
                                       const std::string &requested_path) {
  if (requested_path.empty()) {
    return {};
  }

  std::filesystem::path path(requested_path);
  if (path.is_absolute()) {
    return path.lexically_normal().string();
  }

  const std::string &source_path = workflow_context.getSourcePath();
  if (source_path.empty()) {
    return std::filesystem::absolute(path).lexically_normal().string();
  }

  return (std::filesystem::path(source_path).parent_path() / path).lexically_normal().string();
}

WorkflowValue build_shell_options(const exprtk_value_t &value) {
  if (value.type == EXPRTK_VAL_MAP) {
    return exprtk_scalar_to_json(value);
  }

  if (value.type == EXPRTK_VAL_STRING) {
    const WorkflowValue parsed =
        parse_string_or_keep(std::string_view(value.data.string.data, value.data.string.len));
    if (parsed.is_object()) {
      return parsed;
    }
  }

  return WorkflowValue::object();
}

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

static exprtk_value_t make_str(const char *str, size_t len) {
  exprtk_value_t v{};
  v.type = EXPRTK_VAL_STRING;
  v.data.string.data = const_cast<char *>(str);
  v.data.string.len = len;
  return v;
}

static exprtk_value_t json_to_exprtk_value(const WorkflowValue &value, ScriptEvalContext &eval_ctx) {
  if (value.is_null()) {
    return make_null();
  }
  if (value.is_bool()) {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_BOOL;
    result.data.boolean = value.as<bool>() ? 1 : 0;
    return result;
  }
  if (value.is_int64()) {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_INTEGER;
    result.data.integer = value.as<int64_t>();
    return result;
  }
  if (value.is_uint64()) {
    const auto integer = value.as<uint64_t>();
    if (integer <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      exprtk_value_t result{};
      result.type = EXPRTK_VAL_INTEGER;
      result.data.integer = static_cast<int64_t>(integer);
      return result;
    }
    return eval_ctx.keep(value.to_string());
  }
  if (value.is_double()) {
    return make_num(value.as<double>());
  }
  if (value.is_string()) {
    std::string raw = value.as<std::string>();
    const WorkflowValue parsed = parse_string_or_keep(raw);
    if (parsed.is_object() || parsed.is_array()) {
      return json_to_exprtk_value(parsed, eval_ctx);
    }
    return eval_ctx.keep(std::move(raw));
  }
  if (value.is_array()) {
    std::vector<exprtk_value_t> items;
    items.reserve(value.size());
    for (const auto &item : value.array_range()) {
      items.push_back(json_to_exprtk_value(item, eval_ctx));
    }
    return eval_ctx.keepList(std::move(items));
  }
  if (value.is_object()) {
    exprtk_value_t map = turbo_script_value_map();
    for (const auto &member : value.object_range()) {
      const std::string key(member.key());
      turbo_script_value_map_set(&map, key.c_str(),
                                 json_to_exprtk_value(member.value(), eval_ctx));
    }
    return map;
  }
  return eval_ctx.keep(value.to_string());
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

static WorkflowValue parse_string_or_keep(std::string_view raw) {
  const std::string trimmed = trim_copy(raw);
  if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
    try {
      return WorkflowValue::parse(trimmed);
    } catch (const std::exception &) {
      // Fall through and preserve the original string.
    }
  }
  return std::string(raw);
}

static WorkflowValue exprtk_scalar_to_json(const exprtk_value_t &value) {
  switch (value.type) {
  case EXPRTK_VAL_NUMBER:
    return value.data.number;
  case EXPRTK_VAL_INTEGER:
    return value.data.integer;
  case EXPRTK_VAL_BOOL:
    return value.data.boolean != 0;
  case EXPRTK_VAL_STRING:
    return parse_string_or_keep(std::string_view(value.data.string.data, value.data.string.len));
  case EXPRTK_VAL_VECTOR: {
    WorkflowValue result = WorkflowValue::array();
    for (size_t i = 0; i < value.data.vector.size; ++i) {
      result.push_back(value.data.vector.data[i]);
    }
    return result;
  }
  case EXPRTK_VAL_NULL:
    return WorkflowValue::null();
  case EXPRTK_VAL_MAP:
  case EXPRTK_VAL_OBJECT: {
    WorkflowValue obj = WorkflowValue::object();
    turbo_script_value_map_iterator_t it = turbo_script_value_map_iter_begin(&value);
    const char *key = nullptr;
    exprtk_value_t entry;
    while (turbo_script_value_map_iter_next(&it, &key, &entry)) {
      obj[key] = exprtk_scalar_to_json(entry);
    }
    return obj;
  }
  case EXPRTK_VAL_LIST:
  case EXPRTK_VAL_SET: {
    WorkflowValue arr = WorkflowValue::array();
    for (size_t i = 0; i < value.data.list.count; ++i) {
      arr.push_back(exprtk_scalar_to_json(value.data.list.items[i]));
    }
    return arr;
  }
  case EXPRTK_VAL_FUNCTION:
    return WorkflowValue::null();
  }
  return WorkflowValue::null();
}

// Native function for json.stringify(value) — converts any TurboScript value to a JSON string
static exprtk_value_t json_stringify_fn(size_t argc, exprtk_value_t *args,
                                        exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 1 || !user_data) {
    return make_str("null", 4);
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);

  if (args[0].type == EXPRTK_VAL_STRING) {
    const std::string raw(args[0].data.string.data, args[0].data.string.len);
    const WorkflowValue parsed = parse_string_or_keep(raw);
    if (parsed.is_object() || parsed.is_array()) {
      return eval_ctx->keep(parsed.to_string());
    }
  }

  WorkflowValue j = exprtk_scalar_to_json(args[0]);
  return eval_ctx->keep(j.to_string());
}

// Native function for json.parse(string) — parses a JSON string into a structured TurboScript value.
// If parsing fails, preserve the original string for compatibility.
static exprtk_value_t json_parse_fn(size_t argc, exprtk_value_t *args,
                                    exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }

  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  const std::string raw(args[0].data.string.data, args[0].data.string.len);

  try {
    const WorkflowValue parsed = WorkflowValue::parse(trim_copy(raw));
    return json_to_exprtk_value(parsed, *eval_ctx);
  } catch (const std::exception &) {
    return eval_ctx->keep(raw);
  }
}

static exprtk_value_t json_query_fn(size_t argc, exprtk_value_t *args,
                                    exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 2 || args[1].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }

  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  const WorkflowValue source = exprtk_scalar_to_json(args[0]);
  const std::string query(args[1].data.string.data, args[1].data.string.len);

  try {
    // Keep the established scalar helper while path selection moves to JSONPath.
    if (query == "length(@)") {
      if (!source.is_array() && !source.is_object() && !source.is_string()) {
        return make_null();
      }
      return make_num(static_cast<double>(source.size()));
    }
    const WorkflowValue result =
        Praktor::Data::StructuredDocumentQuery::queryJson(source, query);
    return json_to_exprtk_value(result, *eval_ctx);
  } catch (const std::exception &) {
    return make_null();
  }
}

// Native function for trim(string) — removes leading and trailing whitespace
static exprtk_value_t trim_fn_call(size_t argc, exprtk_value_t *args,
                                   exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  return eval_ctx->keep(
      trim_copy(std::string_view(args[0].data.string.data, args[0].data.string.len)));
}

static exprtk_value_t shell_exec_fn(size_t argc, exprtk_value_t *args,
                                    exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }

  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  std::string command(args[0].data.string.data, args[0].data.string.len);
  std::string input;
  std::string working_dir;
  int timeout_ms = 30000;
  bool stream_output = false;
  std::map<std::string, std::string> environment;
  for (const auto &[key, value] : Praktor::system::getEnvironmentVariables()) {
    environment[key] = value;
  }

  if (argc >= 2) {
    WorkflowValue options = build_shell_options(args[1]);
    if (options.is_object()) {
      if (options.contains("input") && options["input"].is_string()) {
        input = options["input"].as<std::string>();
      }
      if (options.contains("working_dir") && options["working_dir"].is_string()) {
        working_dir = options["working_dir"].as<std::string>();
      }
      if (options.contains("timeout_ms")) {
        if (options["timeout_ms"].is_number()) {
          timeout_ms = options["timeout_ms"].as<int>();
        } else if (options["timeout_ms"].is_string()) {
          timeout_ms = std::stoi(options["timeout_ms"].as<std::string>());
        }
      }
      if (options.contains("stream_output")) {
        if (options["stream_output"].is_bool()) {
          stream_output = options["stream_output"].as<bool>();
        } else if (options["stream_output"].is_number()) {
          stream_output = options["stream_output"].as<int>() != 0;
        } else if (options["stream_output"].is_string()) {
          const std::string raw = options["stream_output"].as<std::string>();
          stream_output = (raw == "true" || raw == "1" || raw == "yes");
        }
      }
      if (options.contains("env") && options["env"].is_object()) {
        for (const auto &member : options["env"].object_range()) {
          if (member.value().is_string()) {
            environment[member.key()] = member.value().as<std::string>();
          } else {
            environment[member.key()] = member.value().to_string();
          }
        }
      }
    }
  }

  const std::string resolved_working_dir =
      resolve_script_working_dir(eval_ctx->workflow_context, working_dir);
  const auto result = actions::ShellExecutor::execute(command, input, resolved_working_dir,
                                                    timeout_ms, environment, stream_output);

  WorkflowValue payload = WorkflowValue::object();
  payload["exit_code"] = result.exit_code;
  payload["stdout"] = result.stdout_output;
  payload["stderr"] = result.stderr_output;
  payload["pid"] = result.pid;
  payload["success"] = result.success();
  payload["output_streamed_live"] = result.output_streamed_live;
  if (!resolved_working_dir.empty()) {
    payload["working_dir"] = resolved_working_dir;
  }

  return eval_ctx->keep(payload.to_string());
}

// Native function for ctx.get("path")
static exprtk_value_t ctx_get_fn(size_t argc, exprtk_value_t *args,
                                 exprtk_env_t * /*env*/, void *user_data) {
  if (argc != 1 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  std::string path(args[0].data.string.data, args[0].data.string.len);
  auto val = eval_ctx->workflow_context.getValueByPath(path);
  return json_to_exprtk_value(val, *eval_ctx);
}

// Native function for ctx.set("path", value)
static exprtk_value_t ctx_set_fn(size_t argc, exprtk_value_t *args,
                                 exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 2 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  std::string path(args[0].data.string.data, args[0].data.string.len);
  if (path == "tasks" || path.rfind("tasks.", 0) == 0 || path == "workflow_status" ||
      path == "failure_context" || path.rfind("failure_context.", 0) == 0) {
    eval_ctx->failed = true;
    eval_ctx->failure_message = "ctx.set cannot write reserved runtime path: " + path;
    return make_null();
  }
  eval_ctx->workflow_context.setValue(path, exprtk_scalar_to_json(args[1]));
  return make_null();
}

static exprtk_value_t ctx_output_fn(size_t argc, exprtk_value_t *args,
                                    exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 2 || args[0].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  std::string key(args[0].data.string.data, args[0].data.string.len);
  WorkflowValue val = exprtk_scalar_to_json(args[1]);
  eval_ctx->workflow_context.setCurrentTaskOutput(key, val);
  return make_null();
}

static exprtk_value_t ctx_output_text_fn(size_t argc, exprtk_value_t *args,
                                         exprtk_env_t * /*env*/, void *user_data) {
  if (argc < 2 || args[0].type != EXPRTK_VAL_STRING ||
      args[1].type != EXPRTK_VAL_STRING || !user_data) {
    return make_null();
  }
  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  std::string key(args[0].data.string.data, args[0].data.string.len);
  std::string value(args[1].data.string.data, args[1].data.string.len);
  eval_ctx->workflow_context.setCurrentTaskOutput(key, value);
  return make_null();
}

static exprtk_value_t log_message_fn(size_t argc, exprtk_value_t *args,
                                     void (*emitter)(std::string_view)) {
  if (argc < 1 || args[0].type != EXPRTK_VAL_STRING) {
    return make_null();
  }

  std::string message(args[0].data.string.data, args[0].data.string.len);
  emitter(message);
  return make_null();
}

static exprtk_value_t log_info_fn(size_t argc, exprtk_value_t *args,
                                  exprtk_env_t * /*env*/, void * /*user_data*/) {
  return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

static exprtk_value_t log_warn_fn(size_t argc, exprtk_value_t *args,
                                  exprtk_env_t * /*env*/, void * /*user_data*/) {
  return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

static exprtk_value_t log_error_fn(size_t argc, exprtk_value_t *args,
                                   exprtk_env_t * /*env*/, void * /*user_data*/) {
  return log_message_fn(argc, args, Praktor::Logging::printScriptMessage);
}

static exprtk_value_t fail_fn(size_t argc, exprtk_value_t *args,
                              exprtk_env_t * /*env*/, void *user_data) {
  if (!user_data) {
    return make_null();
  }

  auto *eval_ctx = static_cast<ScriptEvalContext *>(user_data);
  eval_ctx->failed = true;
  if (argc >= 1 && args[0].type == EXPRTK_VAL_STRING) {
    eval_ctx->failure_message.assign(args[0].data.string.data, args[0].data.string.len);
  } else {
    eval_ctx->failure_message = "Script failed";
  }
  return make_null();
}

ScriptResult execute(const std::string &source, WorkflowContext &context) {
  ScriptResult result;
  const std::string normalized_source = normalize_script_source(source);
  const Praktor::util::TurboScriptRuntimeGuard runtime_guard;

  turbo_script_ctx_t *ctx = turbo_script_init(TURBO_SCRIPT_INIT_DEFAULT);
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
  ts_bind_func(ctx, "ctx_output_text", ctx_output_text_fn, &eval_ctx);
  ts_bind_func(ctx, "log_info", log_info_fn, nullptr);
  ts_bind_func(ctx, "log_warn", log_warn_fn, nullptr);
  ts_bind_func(ctx, "log_error", log_error_fn, nullptr);
  ts_bind_func(ctx, "json_stringify", json_stringify_fn, &eval_ctx);
  ts_bind_func(ctx, "json_parse", json_parse_fn, &eval_ctx);
  ts_bind_func(ctx, "json_query", json_query_fn, &eval_ctx);
  ts_bind_func(ctx, "shell_exec", shell_exec_fn, &eval_ctx);
  ts_bind_func(ctx, "trim_fn", trim_fn_call, &eval_ctx);
  ts_bind_func(ctx, "fail", fail_fn, &eval_ctx);

  if (turbo_script_run_jit(ctx, normalized_source.c_str()) != 0) {
    result.success = false;
    result.error_message = turbo_script_get_error(ctx);

    ScriptError err;
    err.line = 0;
    err.message = result.error_message;
    result.errors.push_back(err);
  }
  if (result.success && eval_ctx.failed) {
    result.success = false;
    result.error_message =
        eval_ctx.failure_message.empty() ? std::string("Script failed") : eval_ctx.failure_message;

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
