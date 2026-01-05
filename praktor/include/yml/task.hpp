#ifndef __TASK_H__
#define __TASK_H__

#include "task_types.hpp"
#include "dag/dependency_graph.hpp"

#include <string>
#include <unordered_map>
#include <vector>

struct Task {
    std::string name;
    TaskAction action = TaskAction::None;

    StrList depends_on;
    Vars vars;
    Vars env;
    DotEnv dot_env;

    std::optional<std::string> when;
    std::optional<Each> each;
    std::optional<RetryPolicy> retries;
    std::optional<std::string> timeout;
    std::optional<Triggers> triggers;

    // Control flow
    bool continue_on_error = false;  // If true, workflow continues even if this task fails
    std::optional<std::string> finally_task;  // Task to run after this task completes (success or failure)

    // Execution context
    std::optional<std::string> working_dir;  // Working directory for command execution
    bool silent = false;  // Suppress command output

    // Incremental build
    StrList sources;    // Input files/globs - if unchanged, skip task
    StrList generates;  // Output files - checked for existence and freshness

    TaskSpecifics specifics = std::monostate{};
    Outputs outputs;

    std::string description;
    std::string source_path;

    bool operator==(const Task& other) const {
        return name == other.name;
    }

    bool operator!=(const Task& other) const {
        return !(*this == other);
    }
};

struct Workflow {
    Vars variables;
    Vars env;
    DotEnv dot_env;
    TaskDefaults defaults;
    std::vector<Task> tasks;
    std::unordered_map<std::string, EmbeddedModule> embedded;

    std::string name;
    std::string description;
    std::string source_path;
};

namespace std {
    template<>
    struct hash<Task> {
        std::size_t operator()(const Task& task) const noexcept {
            return std::hash<std::string>{}(task.name);
        }
    };
}

#endif // __TASK_H__
