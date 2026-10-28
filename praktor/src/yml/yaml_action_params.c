#include "yml/yaml_action_params.h"
#include "yaml_diagnostic_internal.h"
#include <ctype.h>
#include <fmt.h>
#include <stdlib.h>
#include <string.h>

struct praktor_yaml_action_params {
    praktor_yaml_action_kind kind;
    size_t retained_bytes;
    /* Private bounded argument array; every tstr has exactly one owner. */
    struct { tstr *items; size_t count, capacity; } arguments;
    union {
        struct { tstr url, path, sha256; bool overwrite; int timeout_ms; } download;
        struct { tstr name, profile; praktor_process_operation operation; int timeout_ms, poll_interval_ms; } service;
        struct { tstr image_name, executable, working_directory; praktor_process_operation operation;
            int startup_timeout_ms, stop_timeout_ms; bool force_terminate; } process;
        struct { tstr program, input; praktor_yaml_output_format output_format; } program;
        struct { bool sequence; tstr text; praktor_yaml_output_format output_format;
            praktor_yaml_postprocessor postprocessor;
            tstr pattern, path, filter, delimiter, line_separator, output_key;
            int capture_group; } command;
    } value;
};

/* The native YAML Action owner stores only its own owned argument strings.
 * This is not a generic container provider or CMeta shadow runtime. */
static bool argument_push(praktor_yaml_action_params *owner,tstr value) {
    size_t capacity;
    tstr *items;
    if (owner->arguments.count>=PRAKTOR_YAML_ACTION_MAX_ARGUMENTS)
        return false;
    if (owner->arguments.count==owner->arguments.capacity) {
        capacity=owner->arguments.capacity?owner->arguments.capacity:4u;
        while (capacity<=owner->arguments.count) {
            if(capacity>PRAKTOR_YAML_ACTION_MAX_ARGUMENTS/2u) {
                capacity=PRAKTOR_YAML_ACTION_MAX_ARGUMENTS;break;
            }
            capacity*=2u;
        }
        if(capacity>PRAKTOR_YAML_ACTION_MAX_ARGUMENTS ||
           capacity>SIZE_MAX/sizeof(*items))return false;
        items=(tstr *)realloc(owner->arguments.items,capacity*sizeof(*items));
        if(!items)return false;
        owner->arguments.items=items;owner->arguments.capacity=capacity;
    }
    owner->arguments.items[owner->arguments.count++]=value;
    return true;
}
static int owned_failure(praktor_yaml_field_error *error, const char *message) {
    return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_ALLOCATION,
        error ? tstr_dup(message) : NULL);
}
static int invalid(praktor_yaml_field_error *error) {
    if (!error) return SALTS_EINVAL;
    return praktor_yaml_field_fail(error, SALTS_EINVAL, PRAKTOR_YAML_FAILURE_INPUT,
        tstr_dup("Invalid YAML action parameter argument"));
}
static int parameter_error_at(praktor_yaml_node node, const char *message, praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail_at(node, vstr_from_cstr(message), error);
}
static int capacity_error(praktor_yaml_node node, const char *prefix, size_t maximum,
    praktor_yaml_field_error *error) {
    tstr message;
    int status;
    if (!error) return SALTS_EINVAL;
    message = tstr_format("{}{}", prefix, maximum);
    if (!message) return owned_failure(error, "Failed to report YAML action capacity");
    status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
    tstr_free(message);
    return status;
}
static int retain_text(praktor_yaml_action_params *owner, praktor_yaml_node node, tstr text,
    tstr *out, praktor_yaml_field_error *error) {
    size_t length = tstr_len(text);
    if (length >= PRAKTOR_YAML_ACTION_MAX_BYTES - owner->retained_bytes) {
        tstr_free(text);
        return capacity_error(node, "YAML action retained text exceeds maximum bytes of ", PRAKTOR_YAML_ACTION_MAX_BYTES, error);
    }
    owner->retained_bytes += length + 1u;
    *out = text;
    return SALTS_OK;
}
static int read_text(praktor_yaml_action_params *owner, praktor_yaml_node parent, const char *key,
    const char *message, bool string_only, tstr *out, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    tstr text = NULL;
    int status = praktor_yaml_field_get(parent, key, &node, error);
    if (status != SALTS_OK) return status;
    status = string_only ? praktor_yaml_field_string(node, vstr_from_cstr(message), &text, error) :
        praktor_yaml_field_scalar(node, vstr_from_cstr(message), &text, error);
    if (status != SALTS_OK) return status;
    return retain_text(owner, node, text, out, error);
}
static int empty_error(praktor_yaml_node parent, const char *key, const char *message, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    int status = praktor_yaml_field_get(parent, key, &node, error);
    return status == SALTS_OK ? parameter_error_at(node, message, error) : status;
}
static int operation(praktor_yaml_node parent, const char *field, praktor_process_operation *out,
    praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    tstr text = NULL, message;
    int status;
    if (!praktor_yaml_has(parent, "operation")) return SALTS_OK;
    status = praktor_yaml_field_get(parent, "operation", &node, error);
    if (status != SALTS_OK) return status;
    message = tstr_format("{} must be a string", field);
    if (!message) return owned_failure(error, "Failed to read YAML operation");
    status = praktor_yaml_field_string(node, tstr_to_v(message), &text, error);
    tstr_free(message);
    if (status != SALTS_OK) return status;
    if (!strcmp(text, "status")) *out = PRAKTOR_PROCESS_STATUS;
    else if (!strcmp(text, "start")) *out = PRAKTOR_PROCESS_START;
    else if (!strcmp(text, "stop")) *out = PRAKTOR_PROCESS_STOP;
    else if (!strcmp(text, "restart")) *out = PRAKTOR_PROCESS_RESTART;
    else {
        if (!error) { tstr_free(text); return SALTS_EINVAL; }
        message = tstr_format("Unsupported {} value: '{}'", field, text);
        tstr_free(text);
        if (!message) return owned_failure(error, "Failed to report YAML operation");
        status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
        tstr_free(message);
        return status;
    }
    tstr_free(text);
    return SALTS_OK;
}
static int positive_integer(praktor_yaml_node parent, const char *key, const char *message,
    int *out, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    int status;
    if (!praktor_yaml_has(parent, key)) return SALTS_OK;
    status = praktor_yaml_field_get(parent, key, &node, error);
    if (status != SALTS_OK) return status;
    status = praktor_yaml_field_integer(node, out, error);
    if (status != SALTS_OK) return status;
    return *out > 0 ? SALTS_OK : parameter_error_at(node, message, error);
}
/* Consumes text on every path. A failed append retains no dangling owner. */
static int append_argument(praktor_yaml_action_params *owner, praktor_yaml_node node,
    tstr text, praktor_yaml_field_error *error) {
    int status = retain_text(owner, node, text, &text, error);
    if (status != SALTS_OK) return status;
    if (owner->arguments.count>=PRAKTOR_YAML_ACTION_MAX_ARGUMENTS) {
        tstr_free(text);
        return invalid(error);
    }
    if (argument_push(owner,text))return SALTS_OK;
    tstr_free(text);
    return owned_failure(error, "Failed to retain YAML action arguments");
}
static int arguments(praktor_yaml_action_params *owner, praktor_yaml_node parent,
    const char *shape_error, const char *entry_error, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    size_t index;
    int status;
    if (!praktor_yaml_has(parent, "arguments")) return SALTS_OK;
    status = praktor_yaml_field_get(parent, "arguments", &node, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_is_seq(node)) return parameter_error_at(node, shape_error, error);
    if (praktor_yaml_count(node) > PRAKTOR_YAML_ACTION_MAX_ARGUMENTS)
        return capacity_error(node, "YAML action arguments exceed maximum count of ", PRAKTOR_YAML_ACTION_MAX_ARGUMENTS, error);
    for (index = 0; index < praktor_yaml_count(node); ++index) {
        praktor_yaml_node item = praktor_yaml_at(node, index);
        tstr text = NULL;
        status = praktor_yaml_field_string(item, vstr_from_cstr(entry_error), &text, error);
        if (status != SALTS_OK) return status;
        status = append_argument(owner, item, text, error);
        if (status != SALTS_OK) return status;
    }
    return SALTS_OK;
}
#define KEY(text) {text, sizeof(text) - 1u}
static int download(praktor_yaml_action_params *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {KEY("url"), KEY("path"), KEY("sha256"), KEY("overwrite"), KEY("timeout_ms")};
    int status;
    if (!praktor_yaml_is_map(node)) return parameter_error_at(node, "download must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(keys[0]), error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(node, "url")) return parameter_error_at(node, "download requires 'url'", error);
    if (!praktor_yaml_has(node, "path")) return parameter_error_at(node, "download requires 'path'", error);
    status = read_text(owner, node, "url", "download.url must be a scalar", false, &owner->value.download.url, error);
    if (status != SALTS_OK) return status;
    status = read_text(owner, node, "path", "download.path must be a scalar", false, &owner->value.download.path, error);
    if (status != SALTS_OK) return status;
    if (!tstr_len(owner->value.download.url)) return empty_error(node, "url", "download.url cannot be empty", error);
    if (!tstr_len(owner->value.download.path)) return empty_error(node, "path", "download.path cannot be empty", error);
    if (praktor_yaml_has(node, "sha256")) {
        status = read_text(owner, node, "sha256", "download.sha256 must be a scalar", false, &owner->value.download.sha256, error);
        if (status != SALTS_OK) return status;
    }
    if (praktor_yaml_has(node, "overwrite")) {
        praktor_yaml_node flag = {0};
        status = praktor_yaml_field_get(node, "overwrite", &flag, error);
        if (status == SALTS_OK) status = praktor_yaml_field_boolean(flag, vstr_from_cstr("download.overwrite"),
            PRAKTOR_YAML_BOOLEAN_STRICT, &owner->value.download.overwrite, error);
        if (status != SALTS_OK) return status;
    }
    return positive_integer(node, "timeout_ms", "download.timeout_ms must be positive", &owner->value.download.timeout_ms, error);
}
static int service(praktor_yaml_action_params *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {KEY("operation"), KEY("name"), KEY("profile"), KEY("arguments"), KEY("timeout_ms"), KEY("poll_interval_ms")};
    int status;
    if (!praktor_yaml_is_map(node)) return parameter_error_at(node, "service must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(keys[0]), error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(node, "name")) return parameter_error_at(node, "service requires 'name'", error);
    status = read_text(owner, node, "name", "service.name must be a string", true, &owner->value.service.name, error);
    if (status != SALTS_OK) return status;
    if (!tstr_len(owner->value.service.name)) return empty_error(node, "name", "service.name cannot be empty", error);
    status = operation(node, "service.operation", &owner->value.service.operation, error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(node, "profile")) {
        status = read_text(owner, node, "profile", "service.profile must be a string", true, &owner->value.service.profile, error);
        if (status != SALTS_OK) return status;
        if (!tstr_len(owner->value.service.profile)) return empty_error(node, "profile", "service.profile cannot be empty", error);
    }
    status = arguments(owner, node, "service.arguments must be a sequence", "service.arguments entries must be strings", error);
    if (status != SALTS_OK) return status;
    status = positive_integer(node, "timeout_ms", "service.timeout_ms must be positive", &owner->value.service.timeout_ms, error);
    if (status != SALTS_OK) return status;
    return positive_integer(node, "poll_interval_ms", "service.poll_interval_ms must be positive", &owner->value.service.poll_interval_ms, error);
}
static int managed_process(praktor_yaml_action_params *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {KEY("operation"), KEY("executable"), KEY("arguments"), KEY("working_directory"), KEY("identity"),
        KEY("startup_timeout_ms"), KEY("stop_timeout_ms"), KEY("force_terminate")};
    static const vstr identity_keys[] = {KEY("image_name")};
    praktor_yaml_node identity = {0};
    int status;
    if (!praktor_yaml_is_map(node)) return parameter_error_at(node, "managed_process must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(keys[0]), error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(node, "identity")) return parameter_error_at(node, "managed_process requires 'identity'", error);
    status = praktor_yaml_field_get(node, "identity", &identity, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_is_map(identity)) return parameter_error_at(identity, "managed_process.identity must be a map", error);
    status = praktor_yaml_check_keys(identity, identity_keys, 1, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(identity, "image_name")) return parameter_error_at(identity, "managed_process.identity requires 'image_name'", error);
    status = read_text(owner, identity, "image_name", "managed_process.identity.image_name must be a string", true, &owner->value.process.image_name, error);
    if (status != SALTS_OK) return status;
    if (!tstr_len(owner->value.process.image_name)) return empty_error(identity, "image_name", "managed_process.identity.image_name cannot be empty", error);
    status = operation(node, "managed_process.operation", &owner->value.process.operation, error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(node, "executable")) {
        status = read_text(owner, node, "executable", "managed_process.executable must be a string", true, &owner->value.process.executable, error);
        if (status != SALTS_OK) return status;
    }
    if (!tstr_len(owner->value.process.executable)) {
        if (owner->value.process.operation == PRAKTOR_PROCESS_START) return parameter_error_at(node, "managed_process.start requires 'executable'", error);
        if (owner->value.process.operation == PRAKTOR_PROCESS_RESTART) return parameter_error_at(node, "managed_process.restart requires 'executable'", error);
    }
    status = arguments(owner, node, "managed_process.arguments must be a sequence", "managed_process.arguments entries must be strings", error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(node, "working_directory")) {
        status = read_text(owner, node, "working_directory", "managed_process.working_directory must be a string", true, &owner->value.process.working_directory, error);
        if (status != SALTS_OK) return status;
        if (!tstr_len(owner->value.process.working_directory)) return empty_error(node, "working_directory", "managed_process.working_directory cannot be empty", error);
    }
    status = positive_integer(node, "startup_timeout_ms", "managed_process.startup_timeout_ms must be positive", &owner->value.process.startup_timeout_ms, error);
    if (status != SALTS_OK) return status;
    status = positive_integer(node, "stop_timeout_ms", "managed_process.stop_timeout_ms must be positive", &owner->value.process.stop_timeout_ms, error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(node, "force_terminate")) {
        praktor_yaml_node flag = {0};
        status = praktor_yaml_field_get(node, "force_terminate", &flag, error);
        if (status == SALTS_OK) status = praktor_yaml_field_boolean(flag, vstr_from_cstr("force_terminate"),
            PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE, &owner->value.process.force_terminate, error);
        if (status != SALTS_OK) return status;
    }
    return SALTS_OK;
}
/* Command/Program use the old document scalar conversion directly, with no
 * system-field 10 KiB guard and no added string-kind requirement. */
static int scalar_field(praktor_yaml_action_params *owner, praktor_yaml_node parent,
    const char *key, tstr *out, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    tstr text = NULL;
    int status = praktor_yaml_field_get(parent, key, &node, error);
    if (status == SALTS_OK) status = praktor_yaml_field_decode(node, false, &text, error);
    return status == SALTS_OK ? retain_text(owner, node, text, out, error) : status;
}
static int scalar_list(praktor_yaml_action_params *owner, praktor_yaml_node node,
    praktor_yaml_field_error *error) {
    size_t count = praktor_yaml_count(node), index;
    int status;
    if (praktor_yaml_is_seq(node)) {
        if (count > PRAKTOR_YAML_ACTION_MAX_ARGUMENTS)
            return capacity_error(node, "YAML action arguments exceed maximum count of ", PRAKTOR_YAML_ACTION_MAX_ARGUMENTS, error);
        for (index = 0; index < count; ++index) {
            praktor_yaml_node item = praktor_yaml_at(node, index);
            tstr text = NULL;
            status = praktor_yaml_field_decode(item, false, &text, error);
            if (status == SALTS_OK) status = append_argument(owner, item, text, error);
            if (status != SALTS_OK) return status;
        }
        return SALTS_OK;
    }
    if (praktor_yaml_has_val(node)) {
        tstr text = NULL;
        status = praktor_yaml_field_decode(node, false, &text, error);
        if (status != SALTS_OK) return status;
        if (!tstr_len(text)) { tstr_free(text); return SALTS_OK; }
        return append_argument(owner, node, text, error);
    }
    return parameter_error_at(node, "Expected sequence or scalar when converting to string list", error);
}
static bool equal_lowered(tstr text, const char *expected) {
    size_t index, length = tstr_len(text);
    if (length != strlen(expected)) return false;
    for (index = 0; index < length; ++index)
        if (tolower((unsigned char)text[index]) != (unsigned char)expected[index]) return false;
    return true;
}
static int output_format(praktor_yaml_node task, praktor_yaml_output_format *out,
    praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    tstr text = NULL, message;
    int status;
    if (!praktor_yaml_has(task, "output_format")) return SALTS_OK;
    status = praktor_yaml_field_get(task, "output_format", &node, error);
    if (status == SALTS_OK) status = praktor_yaml_field_decode(node, false, &text, error);
    if (status != SALTS_OK) return status;
    if (!tstr_len(text) || equal_lowered(text, "text")) *out = PRAKTOR_YAML_OUTPUT_TEXT;
    else if (equal_lowered(text, "json")) *out = PRAKTOR_YAML_OUTPUT_JSON;
    else {
        if (!error) { tstr_free(text); return SALTS_EINVAL; }
        message = tstr_format("Unsupported output_format value: '{}'", text);
        tstr_free(text);
        if (!message) return owned_failure(error, "Failed to report YAML output format");
        status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
        tstr_free(message);
        return status;
    }
    tstr_free(text);
    return SALTS_OK;
}
static int required_scalar(praktor_yaml_action_params *owner, praktor_yaml_node node,
    const char *block, const char *key, tstr *out, praktor_yaml_field_error *error) {
    tstr message;
    int status;
    if (praktor_yaml_has(node, key)) return scalar_field(owner, node, key, out, error);
    if (!error) return SALTS_EINVAL;
    message = tstr_format("{} requires '{}' field", block, key);
    if (!message) return owned_failure(error, "Failed to report required YAML parser field");
    status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
    tstr_free(message);
    return status;
}
static int command_parser(praktor_yaml_action_params *owner, praktor_yaml_node task,
    const char *selected, praktor_yaml_field_error *error) {
    static const vstr regex_keys[] = {KEY("pattern"), KEY("capture_group"), KEY("output_key")};
    static const vstr json_keys[] = {KEY("path"), KEY("output_key")};
    static const vstr lines_keys[] = {KEY("filter"), KEY("output_key")};
    static const vstr keyvalue_keys[] = {KEY("delimiter"), KEY("line_separator"), KEY("output_key")};
    const vstr *keys;
    size_t count;
    praktor_yaml_node node = {0};
    tstr message;
    int status = praktor_yaml_field_get(task, selected, &node, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_is_map(node)) {
        if (!error) return SALTS_EINVAL;
        message = tstr_format("{} must be a map", selected);
        if (!message) return owned_failure(error, "Failed to report YAML parser shape");
        status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
        tstr_free(message); return status;
    }
    switch (owner->value.command.postprocessor) {
    case PRAKTOR_YAML_POST_REGEX: keys = regex_keys; count = 3; break;
    case PRAKTOR_YAML_POST_JSON: keys = json_keys; count = 2; break;
    case PRAKTOR_YAML_POST_LINES: keys = lines_keys; count = 2; break;
    default: keys = keyvalue_keys; count = 3; break;
    }
    status = praktor_yaml_check_keys(node, keys, count, error);
    if (status != SALTS_OK) return status;
    if (owner->value.command.postprocessor == PRAKTOR_YAML_POST_REGEX) {
        status = required_scalar(owner, node, selected, "pattern", &owner->value.command.pattern, error);
        if (status == SALTS_OK && praktor_yaml_has(node, "capture_group")) {
            praktor_yaml_node capture = {0};
            status = praktor_yaml_field_get(node, "capture_group", &capture, error);
            if (status == SALTS_OK) status = praktor_yaml_field_integer(capture, &owner->value.command.capture_group, error);
        }
    } else if (owner->value.command.postprocessor == PRAKTOR_YAML_POST_JSON)
        status = required_scalar(owner, node, selected, "path", &owner->value.command.path, error);
    else if (owner->value.command.postprocessor == PRAKTOR_YAML_POST_LINES) {
        if (praktor_yaml_has(node, "filter")) status = scalar_field(owner, node, "filter", &owner->value.command.filter, error);
    } else {
        if (praktor_yaml_has(node, "delimiter")) status = scalar_field(owner, node, "delimiter", &owner->value.command.delimiter, error);
        if (status == SALTS_OK && praktor_yaml_has(node, "line_separator"))
            status = scalar_field(owner, node, "line_separator", &owner->value.command.line_separator, error);
    }
    return status == SALTS_OK ? required_scalar(owner, node, selected, "output_key", &owner->value.command.output_key, error) : status;
}
static int command_params(praktor_yaml_action_params *owner, praktor_yaml_node task, praktor_yaml_field_error *error) {
    static const char *const parsers[] = {"parse_regex", "parse_json", "parse_lines", "parse_keyvalue"};
    const char *selected = NULL;
    praktor_yaml_node node = {0};
    size_t index;
    int status;
    if (!praktor_yaml_has(task, "command")) return parameter_error_at(task, "run_command task requires a 'command'", error);
    for (index = 0; index < 4; ++index) if (praktor_yaml_has(task, parsers[index])) {
        if (selected) {
            tstr message;
            status = praktor_yaml_field_get(task, parsers[index], &node, error);
            if (status != SALTS_OK) return status;
            if (!error) return SALTS_EINVAL;
            message = tstr_format("command task cannot combine '{}' and '{}'", selected, parsers[index]);
            if (!message) return owned_failure(error, "Failed to report YAML parser conflict");
            status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
            tstr_free(message); return status;
        }
        selected = parsers[index];
        owner->value.command.postprocessor = (praktor_yaml_postprocessor)(index + 1u);
    }
    status = praktor_yaml_field_get(task, "command", &node, error);
    if (status != SALTS_OK) return status;
    owner->value.command.sequence = praktor_yaml_is_seq(node);
    if (owner->value.command.sequence) {
        status = scalar_list(owner, node, error);
        if (status != SALTS_OK) return status;
        if (!owner->arguments.count) return parameter_error_at(node, "command array must contain at least one entry", error);
        /* Old code decodes every entry before checking any empty command. */
        for (index = 0; index < owner->arguments.count; ++index)
            if (!tstr_len(owner->arguments.items[index]))
                return parameter_error_at(node, "command array entries cannot be empty", error);
    } else if (praktor_yaml_has_val(node)) {
        status = scalar_field(owner, task, "command", &owner->value.command.text, error);
        if (status != SALTS_OK) return status;
        if (!tstr_len(owner->value.command.text)) return parameter_error_at(node, "command string cannot be empty", error);
    } else return parameter_error_at(node, "command must be a scalar or sequence", error);
    status = output_format(task, &owner->value.command.output_format, error);
    if (status == SALTS_OK && selected) {
        owner->value.command.capture_group = 1;
        status = command_parser(owner, task, selected, error);
    }
    return status;
}
static int program_params(praktor_yaml_action_params *owner, praktor_yaml_node task, praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    int status;
    if (!praktor_yaml_has(task, "program")) return parameter_error_at(task, "program task requires a 'program'", error);
    status = praktor_yaml_field_get(task, "program", &node, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has_val(node)) return parameter_error_at(node, "program must be a scalar", error);
    status = scalar_field(owner, task, "program", &owner->value.program.program, error);
    if (status != SALTS_OK) return status;
    if (!tstr_len(owner->value.program.program)) return parameter_error_at(node, "program cannot be empty", error);
    if (praktor_yaml_has(task, "args")) {
        status = praktor_yaml_field_get(task, "args", &node, error);
        if (status == SALTS_OK) status = scalar_list(owner, node, error);
        if (status != SALTS_OK) return status;
    }
    if (praktor_yaml_has(task, "stdin")) {
        status = scalar_field(owner, task, "stdin", &owner->value.program.input, error);
        if (status != SALTS_OK) return status;
    }
    return output_format(task, &owner->value.program.output_format, error);
}
#undef KEY

void praktor_yaml_action_params_destroy(praktor_yaml_action_params *owner) {
    size_t index;
    if (!owner) return;
    for (index = 0; index < owner->arguments.count; ++index) tstr_free(owner->arguments.items[index]);
    free(owner->arguments.items);
    switch (owner->kind) {
    case PRAKTOR_YAML_ACTION_DOWNLOAD:
        tstr_free(owner->value.download.url); tstr_free(owner->value.download.path); tstr_free(owner->value.download.sha256); break;
    case PRAKTOR_YAML_ACTION_SERVICE:
        tstr_free(owner->value.service.name); tstr_free(owner->value.service.profile); break;
    case PRAKTOR_YAML_ACTION_MANAGED_PROCESS:
        tstr_free(owner->value.process.image_name); tstr_free(owner->value.process.executable); tstr_free(owner->value.process.working_directory); break;
    case PRAKTOR_YAML_ACTION_PROGRAM:
        tstr_free(owner->value.program.program); tstr_free(owner->value.program.input); break;
    case PRAKTOR_YAML_ACTION_COMMAND:
        tstr_free(owner->value.command.text); tstr_free(owner->value.command.pattern); tstr_free(owner->value.command.path);
        tstr_free(owner->value.command.filter); tstr_free(owner->value.command.delimiter);
        tstr_free(owner->value.command.line_separator); tstr_free(owner->value.command.output_key); break;
    }
    free(owner);
}
int praktor_yaml_action_params_create(praktor_yaml_node task, praktor_yaml_action_kind kind,
    praktor_yaml_action_params **out, praktor_yaml_field_error *error) {
    praktor_yaml_action_params *owner;
    praktor_yaml_node node = {0};
    const char *key;
    int status;
    praktor_yaml_field_error_dispose(error);
    if (!out || (kind != PRAKTOR_YAML_ACTION_DOWNLOAD && kind != PRAKTOR_YAML_ACTION_SERVICE && kind != PRAKTOR_YAML_ACTION_MANAGED_PROCESS && kind != PRAKTOR_YAML_ACTION_COMMAND && kind != PRAKTOR_YAML_ACTION_PROGRAM)) return invalid(error);
    owner = (praktor_yaml_action_params *)calloc(1, sizeof(*owner));
    if (!owner) return owned_failure(error, "Failed to allocate YAML action parameters");
    owner->kind = kind;
    if (kind == PRAKTOR_YAML_ACTION_COMMAND) status = command_params(owner, task, error);
    else if (kind == PRAKTOR_YAML_ACTION_PROGRAM) status = program_params(owner, task, error);
    else {
        key = kind == PRAKTOR_YAML_ACTION_DOWNLOAD ? "download" : kind == PRAKTOR_YAML_ACTION_SERVICE ? "service" : "managed_process";
        status = praktor_yaml_field_get(task, key, &node, error);
        if (status == SALTS_OK) {
            if (kind == PRAKTOR_YAML_ACTION_DOWNLOAD) { owner->value.download.timeout_ms = 300000; status = download(owner, node, error); }
            else if (kind == PRAKTOR_YAML_ACTION_SERVICE) {
                owner->value.service.timeout_ms = 30000; owner->value.service.poll_interval_ms = 200; status = service(owner, node, error);
            } else {
                owner->value.process.startup_timeout_ms = 5000; owner->value.process.stop_timeout_ms = 5000; status = managed_process(owner, node, error);
            }
        }
    }
    if (status != SALTS_OK) { praktor_yaml_action_params_destroy(owner); return status; }
    *out = owner;
    return SALTS_OK;
}
static vstr text_view(tstr text) { return text ? tstr_to_v(text) : vstr_from_cstr(""); }
int praktor_yaml_action_download(const praktor_yaml_action_params *owner, praktor_yaml_download_view *out) {
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_DOWNLOAD) return SALTS_EINVAL;
    *out = (praktor_yaml_download_view){text_view(owner->value.download.url), text_view(owner->value.download.path), text_view(owner->value.download.sha256),
        owner->value.download.overwrite, owner->value.download.timeout_ms};
    return SALTS_OK;
}
int praktor_yaml_action_service(const praktor_yaml_action_params *owner, praktor_yaml_service_view *out) {
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_SERVICE) return SALTS_EINVAL;
    *out = (praktor_yaml_service_view){owner->value.service.operation, text_view(owner->value.service.name),
        owner->value.service.profile ? text_view(owner->value.service.profile) : vstr_from_cstr("windows_scm"), owner->arguments.count,
        owner->value.service.timeout_ms, owner->value.service.poll_interval_ms};
    return SALTS_OK;
}
int praktor_yaml_action_managed_process(const praktor_yaml_action_params *owner, praktor_yaml_managed_process_view *out) {
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_MANAGED_PROCESS) return SALTS_EINVAL;
    *out = (praktor_yaml_managed_process_view){owner->value.process.operation, text_view(owner->value.process.image_name),
        text_view(owner->value.process.executable), text_view(owner->value.process.working_directory), owner->arguments.count,
        owner->value.process.startup_timeout_ms, owner->value.process.stop_timeout_ms, owner->value.process.force_terminate};
    return SALTS_OK;
}
int praktor_yaml_action_argument(const praktor_yaml_action_params *owner, size_t index, vstr *out) {
    const tstr *text;
    if (!owner || !out) return SALTS_EINVAL;
    if (index>=owner->arguments.count) return SALTS_ENOENT;
    text = &owner->arguments.items[index];
    *out = text_view(*text);
    return SALTS_OK;
}

int praktor_yaml_action_program(const praktor_yaml_action_params *owner, praktor_yaml_program_view *out) {
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_PROGRAM) return SALTS_EINVAL;
    *out = (praktor_yaml_program_view){text_view(owner->value.program.program), text_view(owner->value.program.input),
        owner->arguments.count, owner->value.program.output_format};
    return SALTS_OK;
}
int praktor_yaml_action_command(const praktor_yaml_action_params *owner, praktor_yaml_command_view *out) {
    praktor_yaml_command_view result = {0};
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_COMMAND) return SALTS_EINVAL;
    result.sequence = owner->value.command.sequence;
    result.command = text_view(owner->value.command.text);
    result.command_count = owner->arguments.count;
    result.output_format = owner->value.command.output_format;
    result.postprocessor = owner->value.command.postprocessor;
    switch (result.postprocessor) {
    case PRAKTOR_YAML_POST_REGEX:
        result.parser.regex.pattern = text_view(owner->value.command.pattern);
        result.parser.regex.output_key = text_view(owner->value.command.output_key);
        result.parser.regex.capture_group = owner->value.command.capture_group; break;
    case PRAKTOR_YAML_POST_JSON:
        result.parser.json.path = text_view(owner->value.command.path);
        result.parser.json.output_key = text_view(owner->value.command.output_key); break;
    case PRAKTOR_YAML_POST_LINES:
        result.parser.lines.filter = text_view(owner->value.command.filter);
        result.parser.lines.output_key = text_view(owner->value.command.output_key); break;
    case PRAKTOR_YAML_POST_KEYVALUE:
        result.parser.keyvalue.delimiter = owner->value.command.delimiter ? text_view(owner->value.command.delimiter) : vstr_from_cstr("=");
        result.parser.keyvalue.line_separator = owner->value.command.line_separator ? text_view(owner->value.command.line_separator) : vstr_from_cstr("\n");
        result.parser.keyvalue.output_key = text_view(owner->value.command.output_key); break;
    case PRAKTOR_YAML_POST_NONE: break;
    }
    *out = result;
    return SALTS_OK;
}
