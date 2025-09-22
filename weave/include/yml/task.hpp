#ifndef __TASK_H__
#define __TASK_H__

#include "task_types.hpp"
#include "dag/enhanced_graph.hpp"

#include <string>
#include <unordered_map>
#include <vector>

/**
 * @struct Task
 * @brief Represents a single task in a workflow, aligned with weave_workflow_grammar.md.
 */
struct Task {
    // Core attributes
    std::string name;
    std::string type; // e.g., "run_command", "copy_file"

    // Dependency and conditional execution
    StrList depends_on;
    std::string when;

    // Execution control
    RetryPolicy retries;
    Each each;
    std::string timeout;

    // Input/Output
    Outputs outputs;

    // Post-execution hooks
    StrList on_success;
    StrList on_failure;

    // Task-specific parameters
    TaskSpecifics specifics;

    // For backward compatibility or other potential uses
    std::string description; // from old 'desc' field
    Vars vars; // task-local variables

    bool operator==(const Task& other) const {
        return name == other.name; // Simple comparison for now
    }

    bool operator!=(const Task& other) const {
        return !(*this == other);
    }
};

// Custom hash specialization for Task
namespace std {
    template<>
    struct hash<Task> {
        std::size_t operator()(const Task& task) const noexcept {
            return std::hash<std::string>{}(task.name);
        }
    };
}

// Custom hash for Task for use in unordered_map/set if needed


/**
 * @struct Workflow
 * @brief Represents a parsed workflow file, containing all tasks and configurations.
 *        This replaces the old FlowFile struct.
 */
struct Workflow {
    std::vector<Input> inputs;
    Vars variables;
    Task defaults; // Task struct can hold default values
    std::vector<Task> tasks;
    std::vector<TaskTemplate> task_templates; // New: Custom task templates
    StrList imports; // List of workflow files to import

    // For backward compatibility or metadata
    std::string name;
    std::string description;
};

#endif // __TASK_H__
