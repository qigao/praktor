
#include "executors/script_executor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "util/logger.hpp"
#include "util/system_info.hpp"
#include "util/variable_substitution.hpp"

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

namespace
{
using WorkflowValue = jsoncons::json;

std::string trim(const std::string& value)
{
  auto begin = value.begin();
  while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  auto end = value.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(begin, end);
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs)
{
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(lhs[i]))
        != std::tolower(static_cast<unsigned char>(rhs[i]))) {
      return false;
    }
  }
  return true;
}

std::string toLowerCopy(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string unescapeString(const std::string& input)
{
  std::string output;
  output.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '\\' && i + 1 < input.size()) {
      char next = input[i + 1];
      switch (next) {
        case '\\': output.push_back('\\'); break;
        case '"': output.push_back('"'); break;
        case '\'': output.push_back('\''); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        default: output.push_back(next); break;
      }
      ++i;
    } else {
      output.push_back(c);
    }
  }
  return output;
}

std::vector<std::string> splitStatements(const std::string& source)
{
  std::vector<std::string> statements;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  for (size_t i = 0; i < source.size(); ++i) {
    char c = source[i];
    char prev = (i == 0) ? '\0' : source[i - 1];

    if (!in_single && !in_double && c == '/' && i + 1 < source.size()
        && source[i + 1] == '/') {
      while (i < source.size() && source[i] != '\n') {
        ++i;
      }
      auto trimmed = trim(current);
      if (!trimmed.empty()) {
        statements.push_back(trimmed);
      }
      current.clear();
      continue;
    }

    if (c == '\'' && !in_double && prev != '\\') {
      in_single = !in_single;
    } else if (c == '"' && !in_single && prev != '\\') {
      in_double = !in_double;
    }

    if (!in_single && !in_double && (c == ';' || c == '\n')) {
      auto trimmed = trim(current);
      if (!trimmed.empty()) {
        statements.push_back(trimmed);
      }
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  auto trimmed = trim(current);
  if (!trimmed.empty()) {
    statements.push_back(trimmed);
  }

  return statements;
}

std::vector<std::string> splitArguments(const std::string& args)
{
  std::vector<std::string> result;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  int depth = 0;

  for (size_t i = 0; i < args.size(); ++i) {
    char c = args[i];
    char prev = (i == 0) ? '\0' : args[i - 1];

    if (c == '\'' && !in_double && prev != '\\') {
      in_single = !in_single;
    } else if (c == '"' && !in_single && prev != '\\') {
      in_double = !in_double;
    } else if (!in_single && !in_double) {
      if (c == '(' || c == '[' || c == '{') {
        ++depth;
      } else if (c == ')' || c == ']' || c == '}') {
        --depth;
      }
    }

    if (c == ',' && !in_single && !in_double && depth == 0) {
      result.push_back(trim(current));
      current.clear();
      continue;
    }

    current.push_back(c);
  }

  auto trimmed = trim(current);
  if (!trimmed.empty()) {
    result.push_back(trimmed);
  }

  return result;
}

std::string valueToString(const WorkflowValue& value)
{
  if (value.is_string()) {
    return value.as<std::string>();
  }
  if (value.is_bool()) {
    return value.as<bool>() ? "true" : "false";
  }
  if (value.is_number()) {
    return value.to_string();
  }
  if (value.is_null()) {
    return std::string();
  }
  return value.to_string();
}

bool valueToBool(const WorkflowValue& value)
{
  if (value.is_bool()) {
    return value.as<bool>();
  }
  if (value.is_number()) {
    return value.as<double>() != 0.0;
  }
  if (value.is_string()) {
    auto lowered = toLowerCopy(value.as<std::string>());
    if (lowered == "true" || lowered == "yes" || lowered == "on") {
      return true;
    }
    if (lowered == "false" || lowered == "no" || lowered == "off") {
      return false;
    }
    try {
      return std::stod(lowered) != 0.0;
    } catch (...) {
      return !lowered.empty();
    }
  }
  if (value.is_array()) {
    return !value.empty();
  }
  if (value.is_object()) {
    return !value.empty();
  }
  return false;
}
#ifdef _WIN32
void setEnvironmentValue(const std::string& key, const std::string& value)
{
  if (_putenv_s(key.c_str(), value.c_str()) != 0) {
    throw std::runtime_error("Failed to set environment variable '" + key + "'");
  }
}

void unsetEnvironmentValue(const std::string& key)
{
  if (_putenv_s(key.c_str(), "") != 0) {
    throw std::runtime_error("Failed to unset environment variable '" + key + "'");
  }
}
#else
void setEnvironmentValue(const std::string& key, const std::string& value)
{
  if (setenv(key.c_str(), value.c_str(), 1) != 0) {
    throw std::runtime_error("Failed to set environment variable '" + key + "'");
  }
}

void unsetEnvironmentValue(const std::string& key)
{
  if (unsetenv(key.c_str()) != 0) {
    throw std::runtime_error("Failed to unset environment variable '" + key + "'");
  }
}
#endif
class ScopedEnvironmentOverrides
{
public:
  explicit ScopedEnvironmentOverrides(const Vars& overrides)
  {
    backups_.reserve(overrides.size());
    for (const auto& [key, value] : overrides) {
      const char* existing = std::getenv(key.c_str());
      if (existing != nullptr) {
        backups_.push_back({ key, std::string(existing) });
      } else {
        backups_.push_back({ key, std::nullopt });
      }
      setEnvironmentValue(key, value);
    }
  }

  ~ScopedEnvironmentOverrides()
  {
    for (const auto& entry : backups_) {
      if (entry.second.has_value()) {
        setEnvironmentValue(entry.first, entry.second.value());
      } else {
        unsetEnvironmentValue(entry.first);
      }
    }
  }

private:
  std::vector<std::pair<std::string, std::optional<std::string>>> backups_;
};
class ScriptInterpreter
{
public:
  ScriptInterpreter(const ScriptParams& params,
                    WorkflowContext& context,
                    const Task& task)
    : params_(params)
    , context_(context)
    , task_(task)
  {
    for (const auto& [key, value] : params_.globals) {
      std::string substituted = substituteVariables(value, context_);
      variables_[key] = WorkflowValue(substituted);
    }
  }

  struct Result
  {
    WorkflowValue last_value;
    bool has_last_value = false;
    std::string stdout_data;
  };

  Result run()
  {
    if (!equalsIgnoreCase(params_.language, "javascript")) {
      throw std::runtime_error("Unsupported script language: " + params_.language);
    }

    ScopedEnvironmentOverrides env_guard(params_.environment);
    // Load embedded modules before executing the script body
    for (const auto& module_name : params_.modules) {
      const EmbeddedModule* module = context_.getEmbeddedModule(module_name);
      if (!module) {
        throw std::runtime_error("Embedded module '" + module_name + "' is not defined in the workflow");
      }
      if (!equalsIgnoreCase(module->language, "javascript")) {
        throw std::runtime_error("Embedded module '" + module_name + "' uses unsupported language: " + module->language);
      }
      std::string module_source = substituteVariables(module->source, context_);
      auto module_statements = splitStatements(module_source);
      bool module_returned = false;
      for (const auto& module_statement : module_statements) {
        executeStatement(module_statement, module_returned);
        if (module_returned) {
          break;
        }
      }
      last_value_ = WorkflowValue();
      has_last_value_ = false;
    }


    auto statements = splitStatements(params_.source);
    bool returned = false;
    for (const auto& statement : statements) {
      executeStatement(statement, returned);
      if (returned) {
        break;
      }
    }

    Result result;
    result.last_value = last_value_;
    result.has_last_value = has_last_value_;
    result.stdout_data = stdout_buffer_.str();
    return result;
  }

private:
  void executeStatement(const std::string& statement, bool& returned)
  {
    returned = false;
    std::string working = trim(statement);
    if (working.empty()) {
      return;
    }

    if (working.rfind("return", 0) == 0) {
      std::string expr = trim(working.substr(6));
      WorkflowValue value = evaluateExpression(expr);
      last_value_ = value;
      has_last_value_ = true;
      returned = true;
      return;
    }

    if (working.rfind("let", 0) == 0) {
      working = trim(working.substr(3));
    } else if (working.rfind("const", 0) == 0) {
      working = trim(working.substr(5));
    } else if (working.rfind("var", 0) == 0) {
      working = trim(working.substr(3));
    }

    size_t assign_index = findAssignmentOperator(working);
    if (assign_index != std::string::npos) {
      std::string lhs = trim(working.substr(0, assign_index));
      std::string rhs = trim(working.substr(assign_index + 1));
      if (lhs.empty()) {
        throw std::runtime_error("Invalid assignment in script task '" + task_.name + "'");
      }
      WorkflowValue value = evaluateExpression(rhs);
      variables_[lhs] = value;
      last_value_ = value;
      has_last_value_ = true;
      return;
    }

    WorkflowValue value = evaluateExpression(working);
    last_value_ = value;
    has_last_value_ = true;
  }
  size_t findAssignmentOperator(const std::string& expression) const
  {
    bool in_single = false;
    bool in_double = false;
    int depth = 0;
    for (size_t i = 0; i < expression.size(); ++i) {
      char c = expression[i];
      char prev = (i == 0) ? '\0' : expression[i - 1];
      char next = (i + 1 < expression.size()) ? expression[i + 1] : '\0';

      if (c == '\'' && !in_double && prev != '\\') {
        in_single = !in_single;
      } else if (c == '"' && !in_single && prev != '\\') {
        in_double = !in_double;
      } else if (!in_single && !in_double) {
        if (c == '(' || c == '[' || c == '{') {
          ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
          --depth;
        }
      }

      if (!in_single && !in_double && depth == 0 && c == '=' && next != '='
          && prev != '!' && prev != '<' && prev != '>') {
        return i;
      }
    }
    return std::string::npos;
  }
  WorkflowValue evaluateExpression(const std::string& expression)
  {
    std::string trimmed = trim(expression);
    if (trimmed.empty()) {
      return WorkflowValue();
    }

    size_t paren = trimmed.find('(');
    if (paren != std::string::npos && trimmed.back() == ')') {
      std::string callable = trim(trimmed.substr(0, paren));
      std::string args = trimmed.substr(paren + 1, trimmed.size() - paren - 2);
      return evaluateCommand(callable, args);
    }

    return parseLiteral(trimmed);
  }
  WorkflowValue evaluateCommand(const std::string& callable,
                                const std::string& arguments)
  {
    std::string namespace_name = "global";
    std::string function_name = callable;
    size_t dot = callable.find('.');
    if (dot != std::string::npos) {
      namespace_name = trim(callable.substr(0, dot));
      function_name = trim(callable.substr(dot + 1));
    }

    auto raw_args = splitArguments(arguments);
    auto evaluated_args = evaluateArguments(raw_args);
    return invokeCommand(namespace_name, function_name, evaluated_args);
  }
  WorkflowValue parseLiteral(const std::string& token)
  {
    std::string trimmed = trim(token);
    if (trimmed.empty()) {
      return WorkflowValue();
    }

    if ((trimmed.front() == '"' && trimmed.back() == '"')
        || (trimmed.front() == '\'' && trimmed.back() == '\'')) {
      auto unescaped = unescapeString(trimmed.substr(1, trimmed.size() - 2));
      auto substituted = substituteVariables(unescaped, context_);
      return WorkflowValue(substituted);
    }

    if (trimmed.front() == '{' || trimmed.front() == '[') {
      try {
        return jsoncons::json::parse(trimmed);
      } catch (...) {
        return WorkflowValue(trimmed);
      }
    }

    if (equalsIgnoreCase(trimmed, "true")) {
      return WorkflowValue(true);
    }
    if (equalsIgnoreCase(trimmed, "false")) {
      return WorkflowValue(false);
    }
    if (equalsIgnoreCase(trimmed, "null")) {
      return WorkflowValue();
    }

    if (trimmed.rfind("{{", 0) == 0 && trimmed.size() > 4
        && trimmed.substr(trimmed.size() - 2) == "}}"){
      std::string variable_name = trimmed.substr(2, trimmed.size() - 4);
      variable_name = trim(variable_name);
      if (context_.hasKey(variable_name)) {
        return context_.getValue<WorkflowValue>(variable_name);
      }
      return WorkflowValue();
    }

    if (variables_.count(trimmed) != 0) {
      return variables_.at(trimmed);
    }

    if (context_.hasKey(trimmed)) {
      try {
        return context_.getValue<WorkflowValue>(trimmed);
      } catch (...) {
        return WorkflowValue(context_.getVariable(trimmed));
      }
    }

    try {
      size_t processed = 0;
      double numeric = std::stod(trimmed, &processed);
      if (processed == trimmed.size()) {
        return WorkflowValue(numeric);
      }
    } catch (...) {
      // Not numeric
    }

    return WorkflowValue(trimmed);
  }
  std::vector<WorkflowValue> evaluateArguments(const std::vector<std::string>& raw_args)
  {
    std::vector<WorkflowValue> values;
    values.reserve(raw_args.size());
    for (const auto& arg : raw_args) {
      values.push_back(parseLiteral(arg));
    }
    return values;
  }
  WorkflowValue invokeCommand(const std::string& ns,
                              const std::string& fn,
                              const std::vector<WorkflowValue>& args)
  {
    auto joinArgsAsText = [&](const std::vector<WorkflowValue>& values) {
      std::ostringstream oss;
      for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
          oss << ' ';
        }
        oss << valueToString(values[i]);
      }
      return oss.str();
    };

    if (ns == "global") {
      if (fn == "print" || fn == "echo") {
        auto text = joinArgsAsText(args);
        stdout_buffer_ << text << '\n';
        return WorkflowValue(text);
      }
      if (fn == "fail") {
        std::string message = args.empty() ? std::string("Script failure")
                                           : valueToString(args.front());
        throw std::runtime_error("Script task '" + task_.name + "' failed: " + message);
      }
    }

    if (ns == "console") {
      auto text = joinArgsAsText(args);
      stdout_buffer_ << text << '\n';
      LOG_INFO("[" + task_.name + "] " + text);
      return WorkflowValue(text);
    }

    if (ns == "log") {
      auto text = joinArgsAsText(args);
      if (fn == "info") {
        LOG_INFO(text);
      } else if (fn == "warn" || fn == "warning") {
        Logger::getInstance().warning(text);
      } else if (fn == "error") {
        Logger::getInstance().error(text);
      } else {
        throw std::runtime_error("Unknown log function '" + fn + "'");
      }
      return WorkflowValue();
    }

    if (ns == "os") {
      if (fn == "getenv") {
        if (args.empty()) {
          throw std::runtime_error("os.getenv requires a variable name");
        }
        const char* value = std::getenv(valueToString(args[0]).c_str());
        return value ? WorkflowValue(std::string(value)) : WorkflowValue();
      }
      if (fn == "setenv") {
        if (args.size() < 2) {
          throw std::runtime_error("os.setenv requires name and value");
        }
        setEnvironmentValue(valueToString(args[0]), valueToString(args[1]));
        return WorkflowValue(true);
      }
      if (fn == "unsetenv") {
        if (args.empty()) {
          throw std::runtime_error("os.unsetenv requires a variable name");
        }
        unsetEnvironmentValue(valueToString(args[0]));
        return WorkflowValue(true);
      }
      if (fn == "cwd") {
        return WorkflowValue(std::filesystem::current_path().string());
      }
      if (fn == "hostname") {
        return WorkflowValue(weave::system::getHostname());
      }
      if (fn == "platform") {
        return WorkflowValue(weave::system::getOSName());
      }
      throw std::runtime_error("Unknown os function '" + fn + "'");
    }

    if (ns == "file") {
      if (fn == "read") {
        if (args.empty()) {
          throw std::runtime_error("file.read requires a path");
        }
        std::filesystem::path path(valueToString(args[0]));
        std::ifstream in(path, std::ios::binary);
        if (!in) {
          throw std::runtime_error("Failed to read file: " + path.string());
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return WorkflowValue(buffer.str());
      }
      if (fn == "write") {
        if (args.size() < 2) {
          throw std::runtime_error("file.write requires path and content");
        }
        std::filesystem::path path(valueToString(args[0]));
        std::ofstream out(path, std::ios::binary);
        if (!out) {
          throw std::runtime_error("Failed to write file: " + path.string());
        }
        out << valueToString(args[1]);
        return WorkflowValue(true);
      }
      if (fn == "append") {
        if (args.size() < 2) {
          throw std::runtime_error("file.append requires path and content");
        }
        std::filesystem::path path(valueToString(args[0]));
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out) {
          throw std::runtime_error("Failed to append file: " + path.string());
        }
        out << valueToString(args[1]);
        return WorkflowValue(true);
      }
      if (fn == "exists") {
        if (args.empty()) {
          throw std::runtime_error("file.exists requires a path");
        }
        std::filesystem::path path(valueToString(args[0]));
        return WorkflowValue(std::filesystem::exists(path));
      }
      if (fn == "remove") {
        if (args.empty()) {
          throw std::runtime_error("file.remove requires a path");
        }
        std::filesystem::path path(valueToString(args[0]));
        return WorkflowValue(std::filesystem::remove_all(path) > 0);
      }
      if (fn == "list") {
        if (args.empty()) {
          throw std::runtime_error("file.list requires a directory");
        }
        std::filesystem::path path(valueToString(args[0]));
        bool recursive = args.size() > 1 ? valueToBool(args[1]) : true;
        WorkflowValue results = jsoncons::json::array();
        if (recursive) {
          for (auto const& entry : std::filesystem::recursive_directory_iterator(path)) {
            results.push_back(entry.path().string());
          }
        } else {
          for (auto const& entry : std::filesystem::directory_iterator(path)) {
            results.push_back(entry.path().string());
          }
        }
        return results;
      }
      if (fn == "stat") {
        if (args.empty()) {
          throw std::runtime_error("file.stat requires a path");
        }
        std::filesystem::path path(valueToString(args[0]));
        auto status = std::filesystem::status(path);
        WorkflowValue info = jsoncons::json::object();
        info["exists"] = std::filesystem::exists(status);
        info["is_directory"] = std::filesystem::is_directory(status);
        info["is_file"] = std::filesystem::is_regular_file(status);
        if (std::filesystem::exists(status) && std::filesystem::is_regular_file(status)) {
          info["size"] = static_cast<double>(std::filesystem::file_size(path));
        }
        return info;
      }
      throw std::runtime_error("Unknown file function '" + fn + "'");
    }

    if (ns == "context") {
      if (fn == "set") {
        if (args.size() < 2) {
          throw std::runtime_error("context.set requires name and value");
        }
        auto name = valueToString(args[0]);
        context_.setValue(name, args[1]);
        context_.setCurrentTaskOutput(name, args[1]);
        return WorkflowValue(true);
      }
      if (fn == "get") {
        if (args.empty()) {
          throw std::runtime_error("context.get requires a key");
        }
        std::string key = valueToString(args[0]);
        if (context_.hasKey(key)) {
          try {
            return context_.getValue<WorkflowValue>(key);
          } catch (...) {
            auto str = context_.getVariable(key);
            return str.empty() ? WorkflowValue() : WorkflowValue(str);
          }
        }
        WorkflowValue resolved = context_.getValueByPath(key);
        if (!resolved.is_null()) {
          return resolved;
        }
        return WorkflowValue();
      }
      if (fn == "has") {
        if (args.empty()) {
          throw std::runtime_error("context.has requires a key");
        }
        return WorkflowValue(context_.hasKey(valueToString(args[0])));
      }
      throw std::runtime_error("Unknown context function '" + fn + "'");
    }

    if (ns == "jmespath") {
      if (fn == "search") {
        if (args.size() < 2) {
          throw std::runtime_error("jmespath.search requires data and query");
        }
        WorkflowValue data = args[0];
        if (data.is_string()) {
          try {
            data = jsoncons::json::parse(data.as<std::string>());
          } catch (...) {
            throw std::runtime_error("jmespath.search expected JSON data");
          }
        }
        std::string query = valueToString(args[1]);
        return jsoncons::jmespath::search(data, query);
      }
      throw std::runtime_error("Unknown jmespath function '" + fn + "'");
    }

    if (ns == "fd") {
      if (fn == "find") {
        if (args.size() < 2) {
          throw std::runtime_error("fd.find requires pattern and directory");
        }
        std::string pattern = valueToString(args[0]);
        std::filesystem::path root(valueToString(args[1]));
        bool recursive = args.size() > 2 ? valueToBool(args[2]) : true;
        WorkflowValue results = jsoncons::json::array();
        auto matcher = [&pattern](const std::filesystem::path& p) {
          return p.filename().string().find(pattern) != std::string::npos;
        };
        if (recursive) {
          for (auto const& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (matcher(entry.path())) {
              results.push_back(entry.path().string());
            }
          }
        } else {
          for (auto const& entry : std::filesystem::directory_iterator(root)) {
            if (matcher(entry.path())) {
              results.push_back(entry.path().string());
            }
          }
        }
        return results;
      }
      throw std::runtime_error("Unknown fd function '" + fn + "'");
    }

    if (ns == "rg") {
      if (fn == "search") {
        if (args.size() < 2) {
          throw std::runtime_error("rg.search requires pattern and path");
        }
        std::string pattern = valueToString(args[0]);
        std::filesystem::path target(valueToString(args[1]));
        bool recursive = args.size() > 2 ? valueToBool(args[2]) : false;
        std::regex rx(pattern);
        WorkflowValue matches = jsoncons::json::array();
        auto process_file = [&](const std::filesystem::path& file) {
          std::ifstream in(file);
          if (!in) {
            return;
          }
          std::string line;
          size_t line_no = 0;
          while (std::getline(in, line)) {
            ++line_no;
            if (std::regex_search(line, rx)) {
              WorkflowValue entry = jsoncons::json::object();
              entry["file"] = file.string();
              entry["line"] = static_cast<double>(line_no);
              entry["text"] = line;
              matches.push_back(entry);
            }
          }
        };
        if (std::filesystem::is_directory(target)) {
          if (recursive) {
            for (auto const& entry : std::filesystem::recursive_directory_iterator(target)) {
              if (std::filesystem::is_regular_file(entry.path())) {
                process_file(entry.path());
              }
            }
          } else {
            for (auto const& entry : std::filesystem::directory_iterator(target)) {
              if (std::filesystem::is_regular_file(entry.path())) {
                process_file(entry.path());
              }
            }
          }
        } else {
          process_file(target);
        }
        return matches;
      }
      throw std::runtime_error("Unknown rg function '" + fn + "'");
    }

    if (ns == "json") {
      if (fn == "parse") {
        if (args.empty()) {
          throw std::runtime_error("json.parse requires content");
        }
        return jsoncons::json::parse(valueToString(args[0]));
      }
      if (fn == "stringify") {
        if (args.empty()) {
          throw std::runtime_error("json.stringify requires value");
        }
        return WorkflowValue(valueToString(args[0]));
      }
      throw std::runtime_error("Unknown json function '" + fn + "'");
    }
    if (ns == "rpc") {
      if (fn == "register") {
        if (args.size() < 2) {
          throw std::runtime_error("rpc.register requires name and value");
        }
        rpc_registry_[valueToString(args[0])] = args[1];
        return WorkflowValue(true);
      }
      if (fn == "call") {
        if (args.empty()) {
          throw std::runtime_error("rpc.call requires a name");
        }
        std::string key = valueToString(args[0]);
        if (rpc_registry_.count(key) != 0) {
          return rpc_registry_[key];
        }
        if (args.size() > 1) {
          return args[1];
        }
        throw std::runtime_error("rpc.call did not find mock for '" + key + "'");
      }
      if (fn == "echo") {
        if (args.empty()) {
          return WorkflowValue();
        }
        return args[0];
      }
      throw std::runtime_error("Unknown rpc function '" + fn + "'");
    }

    throw std::runtime_error("Unknown script namespace '" + ns + "'");
  }
  const ScriptParams& params_;
  WorkflowContext& context_;
  const Task& task_;
  std::unordered_map<std::string, WorkflowValue> variables_;
  std::unordered_map<std::string, WorkflowValue> rpc_registry_;
  std::ostringstream stdout_buffer_;
  WorkflowValue last_value_;
  bool has_last_value_ = false;
};
} // namespace

namespace Weave::Execution
{
TaskResult ScriptExecutor::execute(const Task& task,
                                   WorkflowContext& context)
{
  LOG_INFO("Executing script: " + task.name);

  try {
    const auto& params = std::get<ScriptParams>(task.specifics);
    ScriptInterpreter interpreter(params, context, task);
    auto script_result = interpreter.run();

    TaskResult result(true);
    result.exit_code = 0;

    std::string stdout_data = script_result.stdout_data;
    if (stdout_data.empty() && script_result.has_last_value) {
      stdout_data = valueToString(script_result.last_value);
    } else if (!stdout_data.empty() && script_result.has_last_value) {
      if (!stdout_data.empty() && stdout_data.back() != '\n') {
        stdout_data.push_back('\n');
      }
      stdout_data += valueToString(script_result.last_value);
    }

    result.stdout_data = stdout_data;
    result.stderr_data.clear();

    applyOutputs(task, result, context);
    return result;
  } catch (const std::bad_variant_access& e) {
    return TaskResult(false,
                      "Task does not contain ScriptParams: "
                        + std::string(e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false,
                      "Script execution failed: " + std::string(e.what()));
  }
}
void ScriptExecutor::applyOutputs(const Task& task,
                                    const TaskResult& result,
                                    WorkflowContext& context)
  {
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

std::unique_ptr<TaskExecutor> createScriptExecutor()
{
  return std::make_unique<ScriptExecutor>();
}

}  // namespace Weave::Execution





