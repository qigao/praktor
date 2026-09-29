#include "script/script_engine.hpp"
#include "actions/shell_executor.hpp"
#include "dag/workflow_context.hpp"
#include "turbo_script.h"

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

struct HoistedScriptSource {
  std::string initializer;
  std::string body;
};

HoistedScriptSource hoist_top_level_imports(std::string source) {
  HoistedScriptSource result;
  result.body = std::move(source);

  size_t cursor = 0;
  size_t brace_depth = 0;
  while (cursor < result.body.size()) {
    if (result.body.compare(cursor, 2, "//") == 0) {
      const size_t newline = result.body.find('\n', cursor + 2);
      cursor = newline == std::string::npos ? result.body.size() : newline + 1;
      continue;
    }
    if (result.body.compare(cursor, 2, "/*") == 0) {
      const size_t comment_end = result.body.find("*/", cursor + 2);
      cursor = comment_end == std::string::npos ? result.body.size() : comment_end + 2;
      continue;
    }
    if (result.body[cursor] == '"' || result.body[cursor] == '\'') {
      const char quote = result.body[cursor++];
      while (cursor < result.body.size()) {
        if (result.body[cursor] == '\\' && cursor + 1 < result.body.size()) {
          cursor += 2;
        } else if (result.body[cursor++] == quote) {
          break;
        }
      }
      continue;
    }

    if (result.body[cursor] == '{') {
      ++brace_depth;
      ++cursor;
      continue;
    }
    if (result.body[cursor] == '}') {
      if (brace_depth > 0) {
        --brace_depth;
      }
      ++cursor;
      continue;
    }

    size_t path_begin = 0;
    size_t path_end = 0;
    char quote = '\0';
    if (brace_depth == 0 &&
        parse_script_import_literal(result.body, cursor, path_begin, path_end, quote)) {
      size_t statement_end = path_end + 1;
      while (statement_end < result.body.size() &&
             std::isspace(static_cast<unsigned char>(result.body[statement_end])) &&
             result.body[statement_end] != '\n') {
        ++statement_end;
      }
      if (statement_end < result.body.size() && result.body[statement_end] == ')') {
        ++statement_end;
      } else {
        ++cursor;
        continue;
      }
      while (statement_end < result.body.size() &&
             std::isspace(static_cast<unsigned char>(result.body[statement_end])) &&
             result.body[statement_end] != '\n') {
        ++statement_end;
      }
      if (statement_end < result.body.size() && result.body[statement_end] == ';') {
        ++statement_end;
      }

      result.initializer.append(result.body, cursor, statement_end - cursor);
      result.initializer.push_back('\n');

      for (size_t i = cursor; i < statement_end; ++i) {
        if (result.body[i] != '\n' && result.body[i] != '\r') {
          result.body[i] = ' ';
        }
      }
      cursor = statement_end;
      continue;
    }

    ++cursor;
  }

  return result;
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
  bool failed = false;
  std::string failure_message;
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

std::string trim_copy(std::string_view value) {
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

WorkflowValue parse_string_or_keep(std::string_view raw) {
  const std::string trimmed = trim_copy(raw);
  if (!trimmed.empty() && (trimmed.front() == '{' || trimmed.front() == '[')) {
    try {
      return WorkflowValue::parse(trimmed);
    } catch (const std::exception &) {
      // Preserve non-JSON strings verbatim.
    }
  }
  return std::string(raw);
}

WorkflowValue workflow_value_from_host(const turbo_script_value_view_t &value) {
  switch (value.kind) {
  case TURBO_SCRIPT_VALUE_NULL:
    return WorkflowValue::null();
  case TURBO_SCRIPT_VALUE_BOOL:
    return value.as.boolean != 0;
  case TURBO_SCRIPT_VALUE_INT64:
    return value.as.integer;
  case TURBO_SCRIPT_VALUE_NUMBER:
    return value.as.number;
  case TURBO_SCRIPT_VALUE_STRING:
    return std::string(value.as.string.data ? value.as.string.data : "",
                       value.as.string.size);
  case TURBO_SCRIPT_VALUE_ARRAY: {
    WorkflowValue result = WorkflowValue::array();
    for (size_t i = 0; i < value.as.array.count; ++i) {
      result.push_back(workflow_value_from_host(value.as.array.items[i]));
    }
    return result;
  }
  case TURBO_SCRIPT_VALUE_RECORD: {
    WorkflowValue result = WorkflowValue::object();
    for (size_t i = 0; i < value.as.record.count; ++i) {
      const auto &entry = value.as.record.entries[i];
      const std::string key(entry.key.data ? entry.key.data : "", entry.key.size);
      result[key] = workflow_value_from_host(entry.value);
    }
    return result;
  }
  default:
    return WorkflowValue::null();
  }
}

WorkflowValue context_value_from_host(const turbo_script_value_view_t &value) {
  WorkflowValue result = workflow_value_from_host(value);
  if (result.is_string()) {
    return parse_string_or_keep(result.as<std::string>());
  }
  return result;
}

WorkflowValue build_shell_options(const turbo_script_value_view_t &value) {
  WorkflowValue options = workflow_value_from_host(value);
  if (options.is_object()) {
    return options;
  }
  if (options.is_string()) {
    WorkflowValue parsed = parse_string_or_keep(options.as<std::string>());
    if (parsed.is_object()) {
      return parsed;
    }
  }
  return WorkflowValue::object();
}

struct HostValueStorage {
  std::deque<std::string> strings;
  std::deque<std::vector<turbo_script_value_view_t>> arrays;
  std::deque<std::vector<turbo_script_record_entry_view_t>> records;
};

turbo_script_value_view_t host_value_from_workflow(const WorkflowValue &value,
                                                   HostValueStorage &storage) {
  turbo_script_value_view_t result{};
  if (value.is_null()) {
    result.kind = TURBO_SCRIPT_VALUE_NULL;
  } else if (value.is_bool()) {
    result.kind = TURBO_SCRIPT_VALUE_BOOL;
    result.as.boolean = value.as<bool>() ? 1 : 0;
  } else if (value.is_int64()) {
    result.kind = TURBO_SCRIPT_VALUE_INT64;
    result.as.integer = value.as<int64_t>();
  } else if (value.is_uint64()) {
    const auto integer = value.as<uint64_t>();
    if (integer <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      result.kind = TURBO_SCRIPT_VALUE_INT64;
      result.as.integer = static_cast<int64_t>(integer);
    } else {
      result.kind = TURBO_SCRIPT_VALUE_STRING;
      storage.strings.push_back(value.to_string());
      const auto &text = storage.strings.back();
      result.as.string = {text.data(), text.size()};
    }
  } else if (value.is_double()) {
    result.kind = TURBO_SCRIPT_VALUE_NUMBER;
    result.as.number = value.as<double>();
  } else if (value.is_string()) {
    result.kind = TURBO_SCRIPT_VALUE_STRING;
    storage.strings.push_back(value.as<std::string>());
    const auto &text = storage.strings.back();
    result.as.string = {text.data(), text.size()};
  } else if (value.is_array()) {
    result.kind = TURBO_SCRIPT_VALUE_ARRAY;
    std::vector<turbo_script_value_view_t> items;
    items.reserve(value.size());
    for (const auto &item : value.array_range()) {
      items.push_back(host_value_from_workflow(item, storage));
    }
    storage.arrays.push_back(std::move(items));
    const auto &stored = storage.arrays.back();
    result.as.array = {stored.empty() ? nullptr : stored.data(), stored.size()};
  } else if (value.is_object()) {
    result.kind = TURBO_SCRIPT_VALUE_RECORD;
    std::vector<turbo_script_record_entry_view_t> entries;
    entries.reserve(value.size());
    for (const auto &member : value.object_range()) {
      storage.strings.push_back(std::string(member.key()));
      const auto &key = storage.strings.back();
      turbo_script_record_entry_view_t entry{};
      entry.key = {key.data(), key.size()};
      entry.value = host_value_from_workflow(member.value(), storage);
      entries.push_back(entry);
    }
    storage.records.push_back(std::move(entries));
    const auto &stored = storage.records.back();
    result.as.record = {stored.empty() ? nullptr : stored.data(), stored.size()};
  } else {
    result.kind = TURBO_SCRIPT_VALUE_STRING;
    storage.strings.push_back(value.to_string());
    const auto &text = storage.strings.back();
    result.as.string = {text.data(), text.size()};
  }
  return result;
}

turbo_script_status_t set_host_value(turbo_script_host_result_builder_t *builder,
                                     const WorkflowValue &value) {
  HostValueStorage storage;
  const auto host_value = host_value_from_workflow(value, storage);
  return turbo_script_host_result_set_value(builder, &host_value);
}

turbo_script_status_t set_host_null(turbo_script_host_result_builder_t *builder) {
  return set_host_value(builder, WorkflowValue::null());
}

bool host_string_arg(const turbo_script_value_view_t *args, size_t arg_count,
                     size_t index, std::string &out) {
  if (!args || index >= arg_count || args[index].kind != TURBO_SCRIPT_VALUE_STRING) {
    return false;
  }
  out.assign(args[index].as.string.data ? args[index].as.string.data : "",
             args[index].as.string.size);
  return true;
}

using PraktorHostHandler = turbo_script_status_t (*)(
    ScriptEvalContext &, const turbo_script_value_view_t *, size_t,
    turbo_script_host_result_builder_t *);

struct HostBinding {
  const char *name;
  uint32_t min_arity;
  uint32_t max_arity;
  PraktorHostHandler handler;
  ScriptEvalContext *eval_ctx;
};

turbo_script_status_t host_callback_bridge(
    void *user_data, const turbo_script_value_view_t *args, size_t arg_count,
    turbo_script_host_result_builder_t *builder) {
  auto *binding = static_cast<HostBinding *>(user_data);
  if (!binding || !binding->handler || !binding->eval_ctx) {
    return TURBO_SCRIPT_STATUS_INVALID_ARGUMENT;
  }

  try {
    return binding->handler(*binding->eval_ctx, args, arg_count, builder);
  } catch (const std::exception &e) {
    const std::string message = e.what();
    return turbo_script_host_result_set_error(
        builder, -1, {message.data(), message.size()});
  } catch (...) {
    static constexpr std::string_view message = "Praktor host callback failed";
    return turbo_script_host_result_set_error(
        builder, -1, {message.data(), message.size()});
  }
}

bool register_host_binding(turbo_script_ctx_t *ctx,
                           turbo_script_result_t *host_result,
                           HostBinding &binding) {
  turbo_script_host_function_descriptor_t descriptor{};
  descriptor.struct_size = sizeof(descriptor);
  descriptor.min_arity = binding.min_arity;
  descriptor.max_arity = binding.max_arity;
  descriptor.name = {binding.name, std::char_traits<char>::length(binding.name)};
  return turbo_script_context_register_host_function(
             ctx, &descriptor, host_callback_bridge, &binding, host_result) ==
         TURBO_SCRIPT_STATUS_OK;
}

turbo_script_status_t ctx_get_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string path;
  if (!host_string_arg(args, arg_count, 0, path)) {
    return set_host_null(builder);
  }

  WorkflowValue value = eval_ctx.workflow_context.getValueByPath(path);
  if (value.is_string()) {
    const WorkflowValue parsed = parse_string_or_keep(value.as<std::string>());
    if (parsed.is_object() || parsed.is_array()) {
      value = parsed;
    }
  }
  return set_host_value(builder, value);
}

turbo_script_status_t ctx_set_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string path;
  if (!host_string_arg(args, arg_count, 0, path) || arg_count < 2) {
    return set_host_null(builder);
  }

  if (path == "tasks" || path.rfind("tasks.", 0) == 0 || path == "workflow_status" ||
      path == "failure_context" || path.rfind("failure_context.", 0) == 0) {
    eval_ctx.failed = true;
    eval_ctx.failure_message = "ctx.set cannot write reserved runtime path: " + path;
    return set_host_null(builder);
  }

  eval_ctx.workflow_context.setValue(path, context_value_from_host(args[1]));
  return set_host_null(builder);
}

turbo_script_status_t ctx_output_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string key;
  if (!host_string_arg(args, arg_count, 0, key) || arg_count < 2) {
    return set_host_null(builder);
  }
  eval_ctx.workflow_context.setCurrentTaskOutput(key, context_value_from_host(args[1]));
  return set_host_null(builder);
}

turbo_script_status_t ctx_output_text_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string key;
  std::string value;
  if (!host_string_arg(args, arg_count, 0, key) ||
      !host_string_arg(args, arg_count, 1, value)) {
    return set_host_null(builder);
  }
  eval_ctx.workflow_context.setCurrentTaskOutput(key, value);
  return set_host_null(builder);
}

turbo_script_status_t log_host(
    ScriptEvalContext &, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string message;
  if (host_string_arg(args, arg_count, 0, message)) {
    Praktor::Logging::printScriptMessage(message);
  }
  return set_host_null(builder);
}

turbo_script_status_t json_stringify_host(
    ScriptEvalContext &, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  if (!args || arg_count < 1) {
    return set_host_value(builder, std::string("null"));
  }

  WorkflowValue value = context_value_from_host(args[0]);
  return set_host_value(builder, value.to_string());
}

turbo_script_status_t json_parse_host(
    ScriptEvalContext &, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string raw;
  if (!host_string_arg(args, arg_count, 0, raw)) {
    return set_host_null(builder);
  }

  try {
    return set_host_value(builder, WorkflowValue::parse(trim_copy(raw)));
  } catch (const std::exception &) {
    return set_host_value(builder, raw);
  }
}

turbo_script_status_t json_query_host(
    ScriptEvalContext &, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string query;
  if (!args || arg_count < 2 || !host_string_arg(args, arg_count, 1, query)) {
    return set_host_null(builder);
  }

  const WorkflowValue source = context_value_from_host(args[0]);
  try {
    if (query == "length(@)") {
      if (!source.is_array() && !source.is_object() && !source.is_string()) {
        return set_host_null(builder);
      }
      return set_host_value(builder, static_cast<double>(source.size()));
    }

    return set_host_value(
        builder, Praktor::Data::StructuredDocumentQuery::queryJson(source, query));
  } catch (const std::exception &) {
    return set_host_null(builder);
  }
}

turbo_script_status_t trim_host(
    ScriptEvalContext &, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string value;
  if (!host_string_arg(args, arg_count, 0, value)) {
    return set_host_null(builder);
  }
  return set_host_value(builder, trim_copy(value));
}

turbo_script_status_t shell_exec_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  std::string command;
  if (!host_string_arg(args, arg_count, 0, command)) {
    return set_host_null(builder);
  }

  std::string input;
  std::string working_dir;
  int timeout_ms = 30000;
  bool stream_output = false;
  std::map<std::string, std::string> environment;
  for (const auto &[key, value] : Praktor::system::getEnvironmentVariables()) {
    environment[key] = value;
  }

  if (arg_count >= 2) {
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
      resolve_script_working_dir(eval_ctx.workflow_context, working_dir);
  const auto result = actions::ShellExecutor::execute(
      command, input, resolved_working_dir, timeout_ms, environment, stream_output);

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

  // Preserve the legacy shell.exec contract: return serialized JSON text and
  // let ctx.set/ctx.output decode JSON-like strings into structured values.
  return set_host_value(builder, payload.to_string());
}

turbo_script_status_t fail_host(
    ScriptEvalContext &eval_ctx, const turbo_script_value_view_t *args,
    size_t arg_count, turbo_script_host_result_builder_t *builder) {
  eval_ctx.failed = true;
  std::string message;
  if (host_string_arg(args, arg_count, 0, message)) {
    eval_ctx.failure_message = std::move(message);
  } else {
    eval_ctx.failure_message = "Script failed";
  }
  return set_host_null(builder);
}

std::string build_host_module_source(std::string source) {
  auto hoisted = hoist_top_level_imports(std::move(source));
  std::string wrapped;
  wrapped.reserve(hoisted.initializer.size() + hoisted.body.size() + 96);
  wrapped += hoisted.initializer;
  wrapped += "func __praktor_entry(){\n";
  wrapped += hoisted.body;
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

  ScriptEvalContext eval_ctx{context};
  std::array<HostBinding, 13> bindings{{
      {"ctx_get", 1, 1, ctx_get_host, &eval_ctx},
      {"ctx_set", 2, 16, ctx_set_host, &eval_ctx},
      {"ctx_output", 2, 16, ctx_output_host, &eval_ctx},
      {"ctx_output_text", 2, 16, ctx_output_text_host, &eval_ctx},
      {"log_info", 1, 16, log_host, &eval_ctx},
      {"log_warn", 1, 16, log_host, &eval_ctx},
      {"log_error", 1, 16, log_host, &eval_ctx},
      {"json_stringify", 1, 16, json_stringify_host, &eval_ctx},
      {"json_parse", 1, 16, json_parse_host, &eval_ctx},
      {"json_query", 2, 16, json_query_host, &eval_ctx},
      {"shell_exec", 1, 16, shell_exec_host, &eval_ctx},
      {"trim_fn", 1, 16, trim_host, &eval_ctx},
      {"fail", 0, 16, fail_host, &eval_ctx},
  }};

  for (auto &binding : bindings) {
    if (!register_host_binding(ctx, host_result, binding)) {
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
