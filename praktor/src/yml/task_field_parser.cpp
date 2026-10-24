#include "task_yaml_internal.hpp"

#include <memory>
#include <string>
#include <variant>

namespace {

using namespace TaskYamlDetail;

struct ActionParamsDeleter {
    void operator()(praktor_yaml_action_params* owner) const noexcept { praktor_yaml_action_params_destroy(owner); }
};
using ActionParamsOwner = std::unique_ptr<praktor_yaml_action_params, ActionParamsDeleter>;
void require_native_params(int status) {
    if (status != SALTS_OK) throw std::logic_error("Invalid native YAML action parameter projection");
}
std::string owned_text(vstr text) { return std::string(text.data, text.len); }
StrList owned_arguments(const praktor_yaml_action_params* owner, std::size_t count) {
    StrList result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        vstr text{};
        require_native_params(praktor_yaml_action_argument(owner, index, &text));
        result.push_back(owned_text(text));
    }
    return result;
}
static_assert(static_cast<int>(CommandOutputFormat::Text) == PRAKTOR_YAML_OUTPUT_TEXT);
static_assert(static_cast<int>(CommandOutputFormat::Json) == PRAKTOR_YAML_OUTPUT_JSON);
static_assert(static_cast<int>(SystemOperation::Status) == PRAKTOR_PROCESS_STATUS);
static_assert(static_cast<int>(SystemOperation::Start) == PRAKTOR_PROCESS_START);
static_assert(static_cast<int>(SystemOperation::Stop) == PRAKTOR_PROCESS_STOP);
static_assert(static_cast<int>(SystemOperation::Restart) == PRAKTOR_PROCESS_RESTART);

struct TaskFieldsDeleter {
    void operator()(praktor_yaml_task_fields* owner) const noexcept { praktor_yaml_task_fields_destroy(owner); }
};
using TaskFieldsOwner = std::unique_ptr<praktor_yaml_task_fields, TaskFieldsDeleter>;
StrList owned_list(const praktor_yaml_string_list* list) {
    StrList result;
    result.reserve(praktor_yaml_string_list_size(list));
    for (std::size_t index = 0; index < praktor_yaml_string_list_size(list); ++index) {
        vstr text{};
        require_native_params(praktor_yaml_string_list_at(list, index, &text));
        result.push_back(owned_text(text));
    }
    return result;
}
Vars owned_map(const praktor_yaml_string_map* map) {
    Vars result;
    for (std::size_t index = 0; index < praktor_yaml_string_map_size(map); ++index) {
        praktor_yaml_string_entry entry{};
        require_native_params(praktor_yaml_string_map_at(map, index, &entry));
        result[owned_text(entry.key)] = owned_text(entry.value);
    }
    return result;
}
} // namespace

// Temporary STL projections. Construction and validation have a C-only owner.
StrList node_to_string_vector(const TaskYamlDetail::YamlNodeRef& node) {
    TaskFieldsOwner owner(TaskYamlDetail::read_task_fields_or_throw(node, PRAKTOR_YAML_STRING_LIST));
    const praktor_yaml_string_list* list = nullptr;
    require_native_params(praktor_yaml_task_list(owner.get(), &list));
    return owned_list(list);
}
Vars node_to_string_map(const TaskYamlDetail::YamlNodeRef& node) {
    TaskFieldsOwner owner(TaskYamlDetail::read_task_fields_or_throw(node, PRAKTOR_YAML_STRING_MAP));
    const praktor_yaml_string_map* map = nullptr;
    require_native_params(praktor_yaml_task_map(owner.get(), &map));
    return owned_map(map);
}
RunCommandParams parse_run_command_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_COMMAND));
    praktor_yaml_command_view view{};
    require_native_params(praktor_yaml_action_command(owner.get(), &view));
    RunCommandParams result;
    if (view.sequence) result.command = owned_arguments(owner.get(), view.command_count);
    else result.command = owned_text(view.command);
    result.output_format = static_cast<CommandOutputFormat>(view.output_format);
    switch (view.postprocessor) {
    case PRAKTOR_YAML_POST_REGEX:
        result.parse_regex = ParseRegexConfig{owned_text(view.parser.regex.pattern), view.parser.regex.capture_group, owned_text(view.parser.regex.output_key)}; break;
    case PRAKTOR_YAML_POST_JSON:
        result.parse_json = ParseJsonConfig{owned_text(view.parser.json.path), owned_text(view.parser.json.output_key)}; break;
    case PRAKTOR_YAML_POST_LINES:
        result.parse_lines = ParseLinesConfig{owned_text(view.parser.lines.filter), owned_text(view.parser.lines.output_key)}; break;
    case PRAKTOR_YAML_POST_KEYVALUE:
        result.parse_keyvalue = ParseKeyValueConfig{owned_text(view.parser.keyvalue.delimiter), owned_text(view.parser.keyvalue.line_separator), owned_text(view.parser.keyvalue.output_key)}; break;
    case PRAKTOR_YAML_POST_NONE: break;
    }
    return result;
}
ProgramParams parse_program_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_PROGRAM));
    praktor_yaml_program_view view{};
    require_native_params(praktor_yaml_action_program(owner.get(), &view));
    ProgramParams result;
    result.program = owned_text(view.program); result.input = owned_text(view.input);
    result.args = owned_arguments(owner.get(), view.argument_count);
    result.output_format = static_cast<CommandOutputFormat>(view.output_format);
    return result;
}

// Temporary STL conversion only. Validation/defaults/owned strings live in C.
DownloadParams parse_download_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_DOWNLOAD));
    praktor_yaml_download_view view{};
    require_native_params(praktor_yaml_action_download(owner.get(), &view));
    DownloadParams result;
    result.url = owned_text(view.url); result.path = owned_text(view.path); result.sha256 = owned_text(view.sha256);
    result.overwrite = view.overwrite; result.timeout_ms = view.timeout_ms;
    return result;
}
ServiceParams parse_service_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_SERVICE));
    praktor_yaml_service_view view{};
    require_native_params(praktor_yaml_action_service(owner.get(), &view));
    ServiceParams result;
    result.operation = static_cast<SystemOperation>(view.operation);
    result.name = owned_text(view.name); result.profile = owned_text(view.profile);
    result.arguments = owned_arguments(owner.get(), view.argument_count);
    result.timeout_ms = view.timeout_ms; result.poll_interval_ms = view.poll_interval_ms;
    return result;
}
ManagedProcessParams parse_managed_process_params(const TaskYamlDetail::YamlNodeRef& node) {
    ActionParamsOwner owner(TaskYamlDetail::read_action_params_or_throw(node, PRAKTOR_YAML_ACTION_MANAGED_PROCESS));
    praktor_yaml_managed_process_view view{};
    require_native_params(praktor_yaml_action_managed_process(owner.get(), &view));
    ManagedProcessParams result;
    result.operation = static_cast<SystemOperation>(view.operation);
    result.identity.image_name = owned_text(view.image_name); result.executable = owned_text(view.executable);
    result.working_directory = owned_text(view.working_directory);
    result.arguments = owned_arguments(owner.get(), view.argument_count);
    result.startup_timeout_ms = view.startup_timeout_ms; result.stop_timeout_ms = view.stop_timeout_ms;
    result.force_terminate = view.force_terminate;
    return result;
}

UsesParams parse_uses_params(const TaskYamlDetail::YamlNodeRef& node) {
    TaskFieldsOwner owner(TaskYamlDetail::read_task_fields_or_throw(node, PRAKTOR_YAML_USES));
    vstr path{};
    require_native_params(praktor_yaml_task_uses(owner.get(), &path));
    UsesParams result;
    result.path = owned_text(path);
    return result;
}
DynamicTasksParams parse_dynamic_tasks_params(const TaskYamlDetail::YamlNodeRef& node) {
    TaskFieldsOwner owner(TaskYamlDetail::read_task_fields_or_throw(node, PRAKTOR_YAML_DYNAMIC_TASKS));
    praktor_yaml_dynamic_view view{};
    require_native_params(praktor_yaml_task_dynamic(owner.get(), &view));
    DynamicTasksParams result;
    result.items_variable = owned_text(view.items_variable);
    result.task_template.name = owned_text(view.name);
    if (view.sequence) result.task_template.command = owned_list(view.commands);
    else result.task_template.command = owned_text(view.command);
    if (view.has_timeout) result.task_template.timeout = owned_text(view.timeout);
    if (view.has_when) result.task_template.when = owned_text(view.when);
    result.task_template.depends_on = owned_list(view.depends_on);
    result.task_template.env = owned_map(view.env);
    return result;
}
