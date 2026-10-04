#pragma once

#include "task_types.hpp"
#include "dag/dependency_graph.hpp"
#include "data/workflow_value.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

inline constexpr std::uint32_t kPraktorHostToolMaxRetries = 1024u;

struct Task {
    std::string name;
    TaskAction action = TaskAction::None;
    std::string declared_runner;  // Preserves the user-declared runner after internal desugaring.

    StrList depends_on;
    Vars vars;
    Vars env;
    DotEnv dot_env;

    std::optional<std::string> when;
    std::optional<Each> each;
    std::optional<std::string> timeout;
    /** Additional HostTool attempts after the first invocation. */
    std::uint32_t retry_count = 0;
    std::optional<Triggers> triggers;

    // Execution context
    std::optional<std::string> working_dir;  // Working directory for command execution
    bool silent = false;  // Suppress command output

    // Incremental build
    StrList sources;    // Input files/globs - if unchanged, skip task
    StrList generates;  // Output files - checked for existence and freshness

    TaskSpecifics specifics = std::monostate{};
    std::optional<std::string> script;  // Inline script source (replaces old ProcessDsl)

    std::string description;
    std::string source_path;

    bool operator==(const Task& other) const {
        return name == other.name;
    }

    bool operator!=(const Task& other) const {
        return !(*this == other);
    }
};

struct WorkflowContractField {
    std::string type;
    bool required = false;
    std::optional<WorkflowValue> default_value;
    std::vector<WorkflowValue> enum_values;
    std::string description;
    std::optional<std::string> value;
};

using WorkflowContractFields = std::map<std::string, WorkflowContractField>;

struct Workflow {
    WorkflowContractFields inputs;
    WorkflowContractFields outputs;
    bool strict_inputs = false;
    Vars variables;
    Vars env;
    DotEnv dot_env;
    TaskDefaults defaults;
    std::vector<Task> tasks;
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

