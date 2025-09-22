#ifndef __TASK_YAML_HPP__
#define __TASK_YAML_HPP__

#include "task.hpp"
#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include <string>

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

// Utility function to convert ryml node to string vector
std::vector<std::string> node_to_string_vector(const ryml::ConstNodeRef& node);

// Utility function to convert ryml node to string map
Vars node_to_string_map(const ryml::ConstNodeRef& node);

// Utility function to parse imports section
StrList parse_imports(const ryml::ConstNodeRef& node);

// Conversion functions for our structs
Input parse_input(const ryml::ConstNodeRef& node);
TemplateParameter parse_template_parameter(const ryml::ConstNodeRef& node);
TaskTemplate parse_task_template(const ryml::ConstNodeRef& node);
RetryPolicy parse_retry_policy(const ryml::ConstNodeRef& node);
Each parse_each(const ryml::ConstNodeRef& node);
Outputs parse_outputs(const ryml::ConstNodeRef& node);

// Task-specific param parsers
RunCommandParams parse_run_command_params(const ryml::ConstNodeRef& node);
CopyFileParams parse_copy_file_params(const ryml::ConstNodeRef& node);
CreateDirectoryParams parse_create_directory_params(const ryml::ConstNodeRef& node);
MoveFileParams parse_move_file_params(const ryml::ConstNodeRef& node);
ParallelParams parse_parallel_params(const ryml::ConstNodeRef& node);
GroupParams parse_group_params(const ryml::ConstNodeRef& node);
ChooseParams parse_choose_params(const ryml::ConstNodeRef& node);
DynamicTasksParams parse_dynamic_tasks(const ryml::ConstNodeRef& node);

// Main parsers
Task parse_task(const ryml::ConstNodeRef& node);
Workflow parse_workflow(const ryml::ConstNodeRef& node);

#endif // __TASK_YAML_HPP__
