#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using Vars = std::unordered_map<std::string, std::string>;
using StrList = std::vector<std::string>;
using DotEnv = std::vector<std::string>;

enum class TaskAction { None, Uses, DynamicTasks, Orch, Program, Command };

enum class CommandOutputFormat { Text, Json };

struct ParseRegexConfig {
  std::string pattern;
  int capture_group = 1;
  std::string output_key;
};

struct ParseJsonConfig {
  std::string path;
  std::string output_key;
};

struct ParseLinesConfig {
  std::string filter;  // optional regex filter
  std::string output_key;
};

struct ParseKeyValueConfig {
  std::string delimiter = "=";
  std::string line_separator = "\n";
  std::string output_key;
};

struct RunCommandParams {
  std::variant<std::string, StrList> command;
  CommandOutputFormat output_format = CommandOutputFormat::Text;
  std::string working_directory;
  Vars environment;

  // Parser options (only one can be set)
  std::optional<ParseRegexConfig> parse_regex;
  std::optional<ParseJsonConfig> parse_json;
  std::optional<ParseLinesConfig> parse_lines;
  std::optional<ParseKeyValueConfig> parse_keyvalue;
};

struct ProgramParams {
  std::string program;
  StrList args;
  std::string input;
  CommandOutputFormat output_format = CommandOutputFormat::Text;
};

struct UsesParams {
  std::string path;
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

// Trigger actions reference tasks by name
// Any task type (command, orch, uses, dynamic_tasks, script-only) can be used as a trigger
using TriggerAction = std::string;  // Task name to execute

struct Triggers {
  std::vector<TriggerAction> on_success;
  std::vector<TriggerAction> on_failure;
  std::vector<TriggerAction> on_complete;

  bool empty() const { return on_success.empty() && on_failure.empty() && on_complete.empty(); }
};

struct EmbeddedModule {
  std::string language = "javascript";
  std::string source;
  std::string path;  // External file path (alternative to inline source)

  bool isExternal() const { return !path.empty(); }
};

// Import configuration for external modules
struct ModuleImports {
  StrList files;  // List of .yml or .js files to import modules from

  bool empty() const { return files.empty(); }
};

// Native module (DLL/SO) configuration
struct NativeModule {
  std::string name;           // Module name for native call dispatch
  std::string path;           // Path to .dll/.so file
  std::unordered_map<std::string, std::string> hooks;  // hook_name -> symbol_name
};

using NativeModules = std::vector<NativeModule>;

// orch Node Type Registry
struct OrchNodeSpec {
  std::string name;
  bool isControl;
  std::vector<std::string> requiredParams;
  std::vector<std::string> optionalParams;
  bool allowsChildren;
};

struct OrchNode {
  std::string type;  // Node type: Sequence, Shell, etc.
  std::unordered_map<std::string, std::string> params;  // Node parameters
  std::vector<OrchNode> children;  // Child nodes for control flow
  
  // Helper methods for node classification
  bool isControlNode() const;
  bool isLeafNode() const;
  bool hasChildren() const { return !children.empty(); }
  bool hasParams() const { return !params.empty(); }
};

struct OrchParams {
  OrchNode root;  // Root node of the action orchestration (parsed from YAML)
};

// NODE_REGISTRY: Registry of all supported orch node types
extern const std::unordered_map<std::string, OrchNodeSpec> NODE_REGISTRY;

// Lookup node spec by name (case-insensitive)
const OrchNodeSpec* findNodeSpec(const std::string& name);

struct TaskDefaults {
  std::optional<std::string> timeout;
};

using TaskSpecifics = std::variant<std::monostate, UsesParams, DynamicTasksParams, OrchParams, ProgramParams>;

