#pragma once

#include "task.hpp"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

// Helper to get a value from a ryml node if it exists, otherwise a default
template <typename T>
T get_optional(const ryml::ConstNodeRef& node, const char* key, T default_value) {
    if (node.has_child(key)) {
        T result;
        node[key] >> result;
        return result;
    }
    return default_value;
}

StrList node_to_string_vector(const ryml::ConstNodeRef& node);
Vars node_to_string_map(const ryml::ConstNodeRef& node);

Each parse_each(const ryml::ConstNodeRef& node);
Triggers parse_triggers(const ryml::ConstNodeRef& node);
RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node);
ProgramParams parse_program_params(const ryml::ConstNodeRef& node);
UsesParams parse_uses_params(const ryml::ConstNodeRef& node);
DynamicTasksParams parse_dynamic_tasks_params(const ryml::ConstNodeRef& node);
OrchParams parse_orch_params(const ryml::ConstNodeRef& task_node, const std::string& node_type);
TaskDefaults parse_defaults(const ryml::ConstNodeRef& node);

// orch node type detection functions
bool isBtdslControlNode(const ryml::ConstNodeRef& node);
bool isBtdslLeafNode(const ryml::ConstNodeRef& node);
OrchNode parseOrchNode(const ryml::ConstNodeRef& yaml, int depth = 0);

Task parse_task(const ryml::ConstNodeRef& node, const std::string& source_path);
Workflow parse_workflow(const ryml::ConstNodeRef& node, const std::string& source_path);

// Desugars a RunCommandParams into a OrchParams tree.
// Converts command: tasks into equivalent action orchestration nodes at parse time.
OrchParams desugarCommandToorch(const RunCommandParams& params);

