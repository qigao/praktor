#ifndef __TASK_TYPES_H__
#define __TASK_TYPES_H__

#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <memory>

// Forward declaration of EnhancedGraph to avoid circular dependency
template <typename T> class EnhancedGraph;

// --- Basic/old type aliases for compatibility ---
using StrMap = std::unordered_map<std::string, std::string>;
using StrList = std::vector<std::string>;
using Vars = std::unordered_map<std::string, std::string>;
using DotEnv = std::vector<std::string>;
using Imports = std::unordered_map<std::string, std::string>;

// Forward declaration of the main Task struct to resolve circular dependency in parallel/group tasks
struct Task;

// --- Structures defined from weave_workflow_grammar.md ---

/**
 * @struct TemplateParameter
 * @brief Defines a parameter for a custom task template.
 */
struct TemplateParameter {
    std::string name;
    std::string type; // e.g., "string", "integer", "boolean"
    std::string default_value; // Stored as string, will be converted at use
    std::string description;
};


/**
 * @struct Input
 * @brief Defines a runtime parameter for the workflow.
 */
struct Input {
    std::string name;
    std::string type;
    std::string default_value;
    std::string description;
};

/**
 * @struct RetryPolicy
 * @brief Defines the retry strategy for a task.
 */
struct RetryPolicy {
    int count = 0;
    std::string delay = "0s";
};

/**
 * @struct Each
 * @brief Defines the looping mechanism for a task over a list of items.
 */
struct Each {
    StrList items;
    std::string as;
};

/**
 * @struct Outputs
 * @brief Defines how to capture task execution results into variables.
 */
struct Outputs {
    std::string stdout_to_variable;
    std::string stderr_to_variable;
    std::string exit_code_to_variable;
    std::string output_json_to_variable; // New: capture stdout/stderr as JSON
};

// --- Task-specific parameter structures ---

/**
 * @struct RunCommandParams
 * @brief Parameters for the 'run_command' task type.
 */
struct RunCommandParams {
    std::variant<std::string, StrList> command;
    std::string working_directory;
    Vars environment;
};

/**
 * @struct CopyFileParams
 * @brief Parameters for the 'copy_file' task type.
 */
struct CopyFileParams {
    std::string source;
    std::string destination;
    bool overwrite = false;
};

/**
 * @struct CreateDirectoryParams
 * @brief Parameters for the 'create_directory' task type.
 */
struct CreateDirectoryParams {
    std::string path;
    bool parents = false;
};

/**
 * @struct MoveFileParams
 * @brief Parameters for the 'move_file' task type.
 */
struct MoveFileParams {
    std::string source;
    std::string destination;
    bool overwrite = false;
};

/**
 * @struct ParallelParams
 * @brief Parameters for the 'parallel' task type.
 */
struct ParallelParams {
    std::vector<Task> tasks; // Changed from task_names to tasks

    // Default constructors for copyability
    ParallelParams() = default;
    ParallelParams(const ParallelParams&) = default;
    ParallelParams(ParallelParams&&) = default;
    ParallelParams& operator=(const ParallelParams&) = default;
    ParallelParams& operator=(ParallelParams&&) = default;
};

/**
 * @struct GroupParams
 * @brief Parameters for the 'group' task type.
 */
struct GroupParams {
    std::vector<Task> tasks; // Changed from task_names to tasks

    // Default constructors for copyability
    GroupParams() = default;
    GroupParams(const GroupParams&) = default;
    GroupParams(GroupParams&&) = default;
    GroupParams& operator=(const GroupParams&) = default;
    GroupParams& operator=(GroupParams&&) = default;
};

/**
 * @struct ChooseBranch
 * @brief Defines a single branch within a 'choose' task.
 */
struct ChooseBranch {
    std::string when;
    std::vector<Task> tasks; // Changed from task_names to tasks

    // Default constructors for copyability
    ChooseBranch() = default;
    ChooseBranch(const ChooseBranch&) = default;
    ChooseBranch(ChooseBranch&&) = default;
    ChooseBranch& operator=(const ChooseBranch&) = default;
    ChooseBranch& operator=(ChooseBranch&&) = default;
};

/**
 * @struct ChooseParams
 * @brief Parameters for the 'choose' task type.
 */
struct ChooseParams {
    std::vector<ChooseBranch> branches;
    std::vector<std::string> default_task_names;

    // Default constructors for copyability
    ChooseParams() = default;
    ChooseParams(const ChooseParams&) = default;
    ChooseParams(ChooseParams&&) = default;
    ChooseParams& operator=(const ChooseParams&) = default;
    ChooseParams& operator=(ChooseParams&&) = default;
};

/**
 * @struct TaskTemplate
 * @brief Template for custom reusable task types with parameters
 */
struct TaskTemplate {
    std::string name;
    std::vector<TemplateParameter> parameters;
    std::vector<Task> tasks;

    // Default constructors for copyability
    TaskTemplate() = default;
    TaskTemplate(const TaskTemplate&) = default;
    TaskTemplate(TaskTemplate&&) = default;
    TaskTemplate& operator=(const TaskTemplate&) = default;
    TaskTemplate& operator=(TaskTemplate&&) = default;
};

/**
 * @struct DynamicTaskTemplate
 * @brief Template for generating dynamic tasks
 */
struct DynamicTaskTemplate {
    std::string name;        // Template with {{item}} placeholders
    std::string type;        // Task type (e.g., "run_command")
    std::string command;     // Command template (for run_command type)
    std::string timeout;     // Timeout template
    std::string when;        // Condition template
    StrList depends_on;      // Dependencies (can contain templates)

    // Default constructors for copyability
    DynamicTaskTemplate() = default;
    DynamicTaskTemplate(const DynamicTaskTemplate&) = default;
    DynamicTaskTemplate(DynamicTaskTemplate&&) = default;
    DynamicTaskTemplate& operator=(const DynamicTaskTemplate&) = default;
    DynamicTaskTemplate& operator=(DynamicTaskTemplate&&) = default;
};

/**
 * @struct DynamicTasksParams
 * @brief Parameters for the 'dynamic_tasks' task type
 */
struct DynamicTasksParams {
    DynamicTaskTemplate task_template;    // Template for generating tasks
    std::string items_variable;    // Variable name containing JSON array

    // Default constructors for copyability
    DynamicTasksParams() = default;
    DynamicTasksParams(const DynamicTasksParams&) = default;
    DynamicTasksParams(DynamicTasksParams&&) = default;
    DynamicTasksParams& operator=(const DynamicTasksParams&) = default;
    DynamicTasksParams& operator=(DynamicTasksParams&&) = default;
};

/**
 * @brief A variant to hold the specific parameters for each task type.
 * This allows for a type-safe way to handle different task configurations.
 */
using TaskSpecifics = std::variant<
    RunCommandParams,
    CopyFileParams,
    CreateDirectoryParams,
    MoveFileParams,
    ParallelParams,
    GroupParams,
    ChooseParams,
    DynamicTasksParams
    // Note: 'find_and_grep' is an abstract example and would be implemented
    // as a 'run_command' task calling an external script, so it doesn't need a dedicated type here.
>;

#endif // __TASK_TYPES_H__
