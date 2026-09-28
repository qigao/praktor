#include "script/script_engine.hpp"
#include "actions/shell_executor.hpp"
#include "dag/workflow_context.hpp"
#include "turbo_script.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "exprtk_module.h"
#include "exprtk_types.h"
#ifdef __cplusplus
}
#endif

#include "util/logging.hpp"
#include "util/system_info.hpp"
#include "util/turbo_script_runtime.hpp"
#include "data/structured_document_query.hpp"
#include <array>
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

bool is_identifier_char(char ch) {
  const auto value = static_cast<unsigned char>(ch);
  return std::isalnum(value) || ch == '_';
}

bool parse_script_import_literal(const std::string &source, size_t offset,
                                 size_t &path_begin, size_t &path_end, char &quote) {
  std::string_view keyword;
  if (source.compare(offset, 13, "import_module") == 0) {
    keyword = "import_module";
  } else if (source.compare(offset, 6, "import") == 0) {
    keyword = "import";
  } else {
    return false;
  }

  if ((offset > 0 && is_identifier_char(source[offset - 1])) ||
      (offset + keyword.size() < source.size() &&
       is_identifier_char(source[offset + keyword.size()]))) {
    return false;
  }

  size_t cursor = offset + keyword.size();
  while (cursor < source.size() &&
         std::isspace(static_cast<unsigned char>(source[cursor]))) {
    ++cursor;
  }
  if (cursor >= source.size() || source[cursor++] != '(') {
    return false;
  }
  while (cursor < source.size() &&
         std::isspace(static_cast<unsigned char>(source[cursor]))) {
    ++cursor;
  }
  if (cursor >= source.size() || (source[cursor] != '"' && source[cursor] != '\'')) {
    return false;
  }

  quote = source[cursor++];
  path_begin = cursor;
  while (cursor < source.size()) {
    if (source[cursor] == '\\' && cursor + 1 < source.size()) {
      cursor += 2;
      continue;
    }
    if (source[cursor] == quote) {
      path_end = cursor;
      return true;
    }
    ++cursor;
  }
  return false;
}

std::string escape_script_path(std::string_view path, char quote) {
  std::string escaped;
  escaped.reserve(path.size());
  for (const char ch : path) {
    if (ch == '\\' || ch == quote) {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string resolve_script_import_paths(std::string source,
                                        const WorkflowContext &workflow_context,
                                        const std::string &script_source_path) {
  const std::string &source_path =
      script_source_path.empty() ? workflow_context.getSourcePath() : script_source_path;
  if (source_path.empty()) {
    return source;
  }

  const auto source_dir = std::filesystem::absolute(source_path).parent_path();
  size_t cursor = 0;
  while (cursor < source.size()) {
    if (source.compare(cursor, 2, "//") == 0) {
      const size_t newline = source.find('\n', cursor + 2);
      cursor = newline == std::string::npos ? source.size() : newline + 1;
      continue;
    }
    if (source.compare(cursor, 2, "/*") == 0) {
      const size_t comment_end = source.find("*/", cursor + 2);
      cursor = comment_end == std::string::npos ? source.size() : comment_end + 2;
      continue;
    }
    if (source[cursor] == '"' || source[cursor] == '\'') {
      const char quote = source[cursor++];
      while (cursor < source.size()) {
        if (source[cursor] == '\\' && cursor + 1 < source.size()) {
          cursor += 2;
        } else if (source[cursor++] == quote) {
          break;
        }
      }
      continue;
    }

    size_t path_begin = 0;
    size_t path_end = 0;
    char quote = '\0';
    if (!parse_script_import_literal(source, cursor, path_begin, path_end, quote)) {
      ++cursor;
      continue;
    }

    const std::string import_path = source.substr(path_begin, path_end - path_begin);
    const std::filesystem::path path(import_path);
    if (path.extension() != ".tbs" || path.is_absolute()) {
      cursor = path_end + 1;
      continue;
    }

    const auto resolved_path = (source_dir / path).lexically_normal().generic_string();
    const std::string escaped_path = escape_script_path(resolved_path, quote);
    source.replace(path_begin, path_end - path_begin, escaped_path);
    cursor = path_begin + escaped_path.size() + 1;
  }
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

struct LegacyHostBinding {
  const char *name;
  uint32_t min_arity;
  uint32_t max_arity;
  turbo_script_func_t callback;
  void *legacy_user_data;
  ScriptEvalContext *eval_ctx;
};

struct HostValueStorage {
  std::deque<std::string> strings;
  std::deque<std::vector<turbo_script_value_view_t>> arrays;
  std::deque<std::vector<turbo_script_record_entry_view_t>> records;
};

exprtk_value_t exprtk_from_host_value(const turbo_script_value_view_t &value,
                                      ScriptEvalContext &eval_ctx) {
  switch (value.kind) {
  case TURBO_SCRIPT_VALUE_NULL: {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_NULL;
    return result;
  }
  case TURBO_SCRIPT_VALUE_BOOL: {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_BOOL;
    result.data.boolean = value.as.boolean != 0;
    return result;
  }
  case TURBO_SCRIPT_VALUE_INT64: {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_INTEGER;
    result.data.integer = value.as.integer;
    return result;
  }
  case TURBO_SCRIPT_VALUE_NUMBER: {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_NUMBER;
    result.data.number = value.as.number;
    return result;
  }
  case TURBO_SCRIPT_VALUE_STRING:
    return eval_ctx.keep(std::string(value.as.string.data ? value.as.string.data : "",
                                     value.as.string.size));
  case TURBO_SCRIPT_VALUE_ARRAY: {
    std::vector<exprtk_value_t> items;
    items.reserve(value.as.array.count);
    for (size_t i = 0; i < value.as.array.count; ++i) {
      items.push_back(exprtk_from_host_value(value.as.array.items[i], eval_ctx));
    }
    return eval_ctx.keepList(std::move(items));
  }
  case TURBO_SCRIPT_VALUE_RECORD: {
    exprtk_value_t map = turbo_script_value_map();
    for (size_t i = 0; i < value.as.record.count; ++i) {
      const auto &entry = value.as.record.entries[i];
      const std::string key(entry.key.data ? entry.key.data : "", entry.key.size);
      exprtk_value_t child = exprtk_from_host_value(entry.value, eval_ctx);
      turbo_script_value_map_set(&map, key.c_str(), child);
      exprtk_value_destroy(&child);
    }
    return map;
  }
  default: {
    exprtk_value_t result{};
    result.type = EXPRTK_VAL_NULL;
    return result;
  }
  }
}

turbo_script_value_view_t host_value_from_exprtk(const exprtk_value_t &value,
                                                 HostValueStorage &storage) {
  turbo_script_value_view_t result{};
  switch (value.type) {
  case EXPRTK_VAL_NULL:
    result.kind = TURBO_SCRIPT_VALUE_NULL;
    break;
  case EXPRTK_VAL_BOOL:
    result.kind = TURBO_SCRIPT_VALUE_BOOL;
    result.as.boolean = value.data.boolean != 0;
    break;
  case EXPRTK_VAL_INTEGER:
    result.kind = TURBO_SCRIPT_VALUE_INT64;
    result.as.integer = value.data.integer;
    break;
  case EXPRTK_VAL_NUMBER:
    result.kind = TURBO_SCRIPT_VALUE_NUMBER;
    result.as.number = value.data.number;
    break;
  case EXPRTK_VAL_STRING: {
    result.kind = TURBO_SCRIPT_VALUE_STRING;
    storage.strings.emplace_back(value.data.string.data ? value.data.string.data : "",
                                 value.data.string.len);
    const auto &text = storage.strings.back();
    result.as.string = {text.data(), text.size()};
    break;
  }
  case EXPRTK_VAL_VECTOR: {
    result.kind = TURBO_SCRIPT_VALUE_ARRAY;
    std::vector<turbo_script_value_view_t> items;
    items.reserve(value.data.vector.size);
    for (size_t i = 0; i < value.data.vector.size; ++i) {
      turbo_script_value_view_t item{};
      item.kind = TURBO_SCRIPT_VALUE_NUMBER;
      item.as.number = value.data.vector.data[i];
      items.push_back(item);
    }
    storage.arrays.push_back(std::move(items));
    const auto &stored = storage.arrays.back();
    result.as.array = {stored.empty() ? nullptr : stored.data(), stored.size()};
    break;
  }
  case EXPRTK_VAL_LIST:
  case EXPRTK_VAL_SET: {
    result.kind = TURBO_SCRIPT_VALUE_ARRAY;
    std::vector<turbo_script_value_view_t> items;
    items.reserve(value.data.list.count);
    for (size_t i = 0; i < value.data.list.count; ++i) {
      items.push_back(host_value_from_exprtk(value.data.list.items[i], storage));
    }
    storage.arrays.push_back(std::move(items));
    const auto &stored = storage.arrays.back();
    result.as.array = {stored.empty() ? nullptr : stored.data(), stored.size()};
    break;
  }
  case EXPRTK_VAL_MAP:
  case EXPRTK_VAL_OBJECT: {
    result.kind = TURBO_SCRIPT_VALUE_RECORD;
    std::vector<turbo_script_record_entry_view_t> entries;
    turbo_script_value_map_iterator_t it = turbo_script_value_map_iter_begin(&value);
    const char *key = nullptr;
    exprtk_value_t entry{};
    while (turbo_script_value_map_iter_next(&it, &key, &entry)) {
      storage.strings.emplace_back(key ? key : "");
      const auto &stored_key = storage.strings.back();
      turbo_script_record_entry_view_t converted{};
      converted.key = {stored_key.data(), stored_key.size()};
      converted.value = host_value_from_exprtk(entry, storage);
      entries.push_back(converted);
    }
    storage.records.push_back(std::move(entries));
    const auto &stored = storage.records.back();
    result.as.record = {stored.empty() ? nullptr : stored.data(), stored.size()};
    break;
  }
  case EXPRTK_VAL_FUNCTION:
    result.kind = TURBO_SCRIPT_VALUE_NULL;
    break;
  }
  return result;
}

turbo_script_status_t legacy_host_callback(
    void *user_data, const turbo_script_value_view_t *args, size_t arg_count,
    turbo_script_host_result_builder_t *builder) {
  auto *binding = static_cast<LegacyHostBinding *>(user_data);
  if (!binding || !binding->callback || !binding->eval_ctx || (!args && arg_count != 0)) {
    return TURBO_SCRIPT_STATUS_INVALID_ARGUMENT;
  }

  std::vector<exprtk_value_t> converted_args;
  converted_args.reserve(arg_count);
  exprtk_value_t output{};
  bool has_output = false;

  const auto cleanup_values = [&]() {
    if (has_output) {
      exprtk_value_destroy(&output);
      has_output = false;
    }
    for (auto &value : converted_args) {
      exprtk_value_destroy(&value);
    }
  };

  try {
    for (size_t i = 0; i < arg_count; ++i) {
      converted_args.push_back(exprtk_from_host_value(args[i], *binding->eval_ctx));
    }

    output = binding->callback(arg_count,
                               converted_args.empty() ? nullptr : converted_args.data(),
                               nullptr,
                               binding->legacy_user_data);
    has_output = true;
    HostValueStorage storage;
    const turbo_script_value_view_t host_output = host_value_from_exprtk(output, storage);
    const auto status = turbo_script_host_result_set_value(builder, &host_output);
    cleanup_values();
    return status;
  } catch (const std::exception &e) {
    cleanup_values();
    const std::string message = e.what();
    return turbo_script_host_result_set_error(
        builder, -1, {message.data(), message.size()});
  } catch (...) {
    cleanup_values();
    static constexpr std::string_view message = "Praktor host callback failed";
    return turbo_script_host_result_set_error(
        builder, -1, {message.data(), message.size()});
  }
}

bool register_legacy_host_binding(turbo_script_ctx_t *ctx,
                                  turbo_script_result_t *host_result,
                                  LegacyHostBinding &binding) {
  turbo_script_host_function_descriptor_t descriptor{};
  descriptor.struct_size = sizeof(descriptor);
  descriptor.min_arity = binding.min_arity;
  descriptor.max_arity = binding.max_arity;
  descriptor.name = {binding.name, std::char_traits<char>::length(binding.name)};
  return turbo_script_context_register_host_function(
             ctx, &descriptor, legacy_host_callback, &binding, host_result) ==
         TURBO_SCRIPT_STATUS_OK;
}

std::string build_host_module_source(std::string_view source) {
  std::string wrapped;
  wrapped.reserve(source.size() + 96);
  wrapped += "func __praktor_entry(){\n";
  wrapped.append(source.data(), source.size());
  wrapped += "\nreturn 0;\n};\nexport(\"__praktor_entry\");\n";
  return wrapped;
}

int execution_control_interrupt(void *user_data) {
  const auto *control =
      static_cast<const Praktor::Execution::ExecutionControl *>(user_data);
  return control && control->stopRequested() ? 1 : 0;
}

std::string host_result_error_message(turbo_script_result_t *host_result,
                                      std::string_view fallback,
                                      ScriptError *out_error = nullptr) {
  turbo_script_error_info_t error{};
  error.struct_size = sizeof(error);
  if (host_result &&
      turbo_script_result_get_error(host_result, &error) == TURBO_SCRIPT_STATUS_OK) {
    if (out_error) {
      out_error->line = static_cast<int>(error.line);
    }
    if (error.message.data && error.message.size) {
      if (out_error) {
        out_error->message.assign(error.message.data, error.message.size);
      }
      return std::string(error.message.data, error.message.size);
    }
  }
  if (out_error) {
    out_error->message = std::string(fallback);
  }
  return std::string(fallback);
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

ScriptResult execute(const std::string &source, WorkflowContext &context,
                     const std::string &script_source_path) {
  ScriptResult result;
  const std::string normalized_source =
      normalize_script_source(resolve_script_import_paths(source, context, script_source_path));
  const std::string host_source = build_host_module_source(normalized_source);
  const Praktor::util::TurboScriptRuntimeGuard runtime_guard;

  turbo_script_ctx_t *ctx = turbo_script_init(TURBO_SCRIPT_INIT_DEFAULT);
  if (!ctx) {
    result.success = false;
    result.error_message = "Failed to initialize TurboScript context";
    return result;
  }

  turbo_script_result_t *host_result = nullptr;
  turbo_script_module_t *module = nullptr;
  turbo_script_instance_t *instance = nullptr;

  auto cleanup = [&]() {
    if (instance && host_result) {
      (void)turbo_script_instance_destroy(instance, host_result);
      instance = nullptr;
    }
    if (module && host_result) {
      (void)turbo_script_module_destroy(module, host_result);
      module = nullptr;
    }
    if (host_result) {
      turbo_script_result_destroy(host_result);
      host_result = nullptr;
    }
    turbo_script_free(ctx);
    ctx = nullptr;
  };

  if (turbo_script_result_create(ctx, &host_result) != TURBO_SCRIPT_STATUS_OK ||
      !host_result) {
    result.success = false;
    result.error_message = "Failed to create TurboScript Host result";
    cleanup();
    return result;
  }

  ScriptEvalContext eval_ctx{context, {}};
  std::array<LegacyHostBinding, 13> bindings{{
      {"ctx_get", 1, 1, ctx_get_fn, &eval_ctx, &eval_ctx},
      {"ctx_set", 2, 16, ctx_set_fn, &eval_ctx, &eval_ctx},
      {"ctx_output", 2, 16, ctx_output_fn, &eval_ctx, &eval_ctx},
      {"ctx_output_text", 2, 16, ctx_output_text_fn, &eval_ctx, &eval_ctx},
      {"log_info", 1, 16, log_info_fn, nullptr, &eval_ctx},
      {"log_warn", 1, 16, log_warn_fn, nullptr, &eval_ctx},
      {"log_error", 1, 16, log_error_fn, nullptr, &eval_ctx},
      {"json_stringify", 1, 16, json_stringify_fn, &eval_ctx, &eval_ctx},
      {"json_parse", 1, 16, json_parse_fn, &eval_ctx, &eval_ctx},
      {"json_query", 2, 16, json_query_fn, &eval_ctx, &eval_ctx},
      {"shell_exec", 1, 16, shell_exec_fn, &eval_ctx, &eval_ctx},
      {"trim_fn", 1, 16, trim_fn_call, &eval_ctx, &eval_ctx},
      {"fail", 0, 16, fail_fn, &eval_ctx, &eval_ctx},
  }};

  for (auto &binding : bindings) {
    if (!register_legacy_host_binding(ctx, host_result, binding)) {
      result.success = false;
      ScriptError error{};
      result.error_message =
          host_result_error_message(host_result, "Failed to register TurboScript host callback",
                                    &error);
      result.errors.push_back(std::move(error));
      cleanup();
      return result;
    }
  }

  turbo_script_module_options_t module_options{};
  turbo_script_module_options_init(&module_options);
  static constexpr std::string_view module_name = "praktor-script";
  module_options.module_name = {module_name.data(), module_name.size()};

  auto status = turbo_script_module_compile(
      ctx, {host_source.data(), host_source.size()}, &module_options, host_result, &module);
  if (status != TURBO_SCRIPT_STATUS_OK) {
    result.success = false;
    ScriptError error{};
    result.error_message =
        host_result_error_message(host_result, "TurboScript module compile failed", &error);
    result.errors.push_back(std::move(error));
    cleanup();
    return result;
  }

  turbo_script_instance_options_t instance_options{};
  turbo_script_instance_options_init(&instance_options);
  instance_options.mode = TURBO_SCRIPT_EXEC_JIT;
  status = turbo_script_instance_create(module, &instance_options, host_result, &instance);
  if (status != TURBO_SCRIPT_STATUS_OK) {
    result.success = false;
    ScriptError error{};
    result.error_message =
        host_result_error_message(host_result, "TurboScript instance creation failed", &error);
    result.errors.push_back(std::move(error));
    cleanup();
    return result;
  }

  turbo_script_export_handle_t entry = 0;
  static constexpr std::string_view entry_name = "__praktor_entry";
  status = turbo_script_instance_resolve_export(
      instance, {entry_name.data(), entry_name.size()}, host_result, &entry);
  if (status != TURBO_SCRIPT_STATUS_OK) {
    result.success = false;
    ScriptError error{};
    result.error_message =
        host_result_error_message(host_result, "TurboScript entry resolution failed", &error);
    result.errors.push_back(std::move(error));
    cleanup();
    return result;
  }

  turbo_script_call_options_t call_options{};
  turbo_script_call_options_init(&call_options);
  if (const auto control = context.getExecutionControl()) {
    call_options.interrupt = execution_control_interrupt;
    call_options.interrupt_user_data = control.get();
  }

  status =
      turbo_script_instance_call(instance, entry, nullptr, 0, &call_options, host_result);
  if (status != TURBO_SCRIPT_STATUS_OK) {
    result.success = false;
    ScriptError error{};
    if (status == TURBO_SCRIPT_STATUS_INTERRUPTED) {
      const auto control = context.getExecutionControl();
      if (control && control->stopReason() ==
                         Praktor::Execution::ExecutionControl::StopReason::DeadlineExceeded) {
        result.error_message = "Script execution timed out";
      } else {
        result.error_message = "Script execution cancelled";
      }
      error.message = result.error_message;
    } else {
      result.error_message =
          host_result_error_message(host_result, "TurboScript execution failed", &error);
    }
    result.errors.push_back(std::move(error));
  }

  if (result.success && eval_ctx.failed) {
    result.success = false;
    result.error_message =
        eval_ctx.failure_message.empty() ? std::string("Script failed") : eval_ctx.failure_message;

    ScriptError error{};
    error.line = 0;
    error.message = result.error_message;
    result.errors.push_back(std::move(error));
  }

  cleanup();
  return result;
}

} // namespace Praktor::Script
