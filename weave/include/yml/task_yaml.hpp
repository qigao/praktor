#ifndef __TASK_YAML_HPP__
#define __TASK_YAML_HPP__

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

RetryPolicy parse_retry_policy(const ryml::ConstNodeRef& node);
Each parse_each(const ryml::ConstNodeRef& node);
Triggers parse_triggers(const ryml::ConstNodeRef& node);
RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node);
ScriptParams parse_script_params(const ryml::ConstNodeRef& node);
UsesParams parse_uses_params(const ryml::ConstNodeRef& node);
TaskDefaults parse_defaults(const ryml::ConstNodeRef& node);

Task parse_task(const ryml::ConstNodeRef& node, const std::string& source_path);
Workflow parse_workflow(const ryml::ConstNodeRef& node, const std::string& source_path);

#endif // __TASK_YAML_HPP__
