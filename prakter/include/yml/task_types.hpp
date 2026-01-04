#ifndef __TASK_TYPES_H__
#define __TASK_TYPES_H__

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using Vars = std::unordered_map<std::string, std::string>;
using StrList = std::vector<std::string>;
using DotEnv = std::vector<std::string>;

enum class TaskAction { None, RunCommand, Script, Uses, DynamicTasks };

enum class CommandOutputFormat { Text, Json };

struct RunCommandParams {
  std::variant<std::string, StrList> command;
  CommandOutputFormat output_format = CommandOutputFormat::Text;
  std::string working_directory;
  Vars environment;
};

struct ScriptParams {
  std::string source;
  std::string language = "javascript";
  Vars environment;
  Vars globals;
  StrList modules;
};

struct UsesParams {
  std::string path;
};

struct RetryPolicy {
  int count = 0;
  std::string delay = "0s";
};

/**
 * @brief Template for generating dynamic tasks
 *
 * Contains the task properties that can be templated with {{ item }} placeholders.
 */
struct DynamicTaskTemplate {
  std::string name;
  std::variant<std::string, StrList> command;
  std::optional<std::string> timeout;
  std::optional<RetryPolicy> retries;
  std::optional<std::string> when;
  StrList depends_on;
  Vars env;
};

/**
 * @brief Parameters for dynamic task generation
 *
 * Generates and executes tasks at runtime based on data from the context.
 * Each item in the items array produces one task from the template.
 */
struct DynamicTasksParams {
  std::string items_variable;      // Context path to JSON array (e.g., "tasks.discover.outputs.data")
  DynamicTaskTemplate task_template;
};

struct Each {
  StrList items;
  std::unordered_map<std::string, StrList> matrix;
  std::string as = "item";
  std::string index_variable;

  bool hasItems() const { return !items.empty(); }
  bool hasMatrix() const { return !matrix.empty(); }
  bool enabled() const { return hasItems() || hasMatrix(); }
};

enum class WriteFileMode { Overwrite, Append };

struct HttpPostTrigger {
  std::string url;
  std::optional<std::string> body;
  std::unordered_map<std::string, std::string> headers;
};

struct WriteFileTrigger {
  std::string path;
  std::string content;
  WriteFileMode mode = WriteFileMode::Overwrite;
};

struct RunTaskTrigger {
  std::string task_name;
};

struct PrakterNotifyTrigger {
  std::string message;
};
using TriggerAction =
    std::variant<HttpPostTrigger, WriteFileTrigger, RunTaskTrigger, PrakterNotifyTrigger>;

struct Triggers {
  std::vector<TriggerAction> on_success;
  std::vector<TriggerAction> on_failure;
  std::vector<TriggerAction> on_complete;

  bool empty() const { return on_success.empty() && on_failure.empty() && on_complete.empty(); }
};

struct Outputs {
  std::string stdout_to_variable;
  std::string stderr_to_variable;
  std::string exit_code_to_variable;
  std::string output_json_to_variable;
};

struct EmbeddedModule {
  std::string language = "javascript";
  std::string source;
};

struct TaskDefaults {
  std::optional<RetryPolicy> retries;
  std::optional<std::string> timeout;
};

using TaskSpecifics = std::variant<std::monostate, RunCommandParams, ScriptParams, UsesParams, DynamicTasksParams>;

#endif // __TASK_TYPES_H__
