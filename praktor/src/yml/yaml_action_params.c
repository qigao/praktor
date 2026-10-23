#include "yml/yaml_action_params.h"
#include "yaml_diagnostic_internal.h"
#include <cstl/vec.h>
#include <fmt.h>
#include <stdlib.h>
#include <string.h>

struct praktor_yaml_action_params {
    praktor_yaml_action_kind kind;
    size_t retained_bytes;
    /* Raw pointer storage only: this owner explicitly destroys each tstr. */
    vec_t arguments;
    union {
        struct { tstr url, path, sha256; bool overwrite; int timeout_ms; } download;
        struct { tstr name, profile; praktor_process_operation operation; int timeout_ms, poll_interval_ms; } service;
        struct { tstr image_name, executable, working_directory; praktor_process_operation operation;
            int startup_timeout_ms, stop_timeout_ms; bool force_terminate; } process;
    } value;
};

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
    size_t bytes = tstr_len(text) + 1;
    if (bytes > PRAKTOR_YAML_ACTION_MAX_BYTES - owner->retained_bytes) {
        tstr_free(text);
        return capacity_error(node, "YAML action retained text exceeds maximum bytes of ", PRAKTOR_YAML_ACTION_MAX_BYTES, error);
    }
    owner->retained_bytes += bytes;
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
        stl_status stored;
        status = praktor_yaml_field_string(item, vstr_from_cstr(entry_error), &text, error);
        if (status != SALTS_OK) return status;
        status = retain_text(owner, item, text, &text, error);
        if (status != SALTS_OK) return status;
        stored = vec_push(&owner->arguments, &text);
        if (stored != STL_OK) {
            tstr_free(text);
            return stored == STL_OUT_OF_MEMORY ? owned_failure(error, "Failed to retain YAML action arguments") : invalid(error);
        }
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
#undef KEY

void praktor_yaml_action_params_destroy(praktor_yaml_action_params *owner) {
    size_t index;
    if (!owner) return;
    for (index = 0; index < vec_size(&owner->arguments); ++index) tstr_free(*(const tstr *)vec_at_const(&owner->arguments, index));
    vec_destroy(&owner->arguments);
    switch (owner->kind) {
    case PRAKTOR_YAML_ACTION_DOWNLOAD:
        tstr_free(owner->value.download.url); tstr_free(owner->value.download.path); tstr_free(owner->value.download.sha256); break;
    case PRAKTOR_YAML_ACTION_SERVICE:
        tstr_free(owner->value.service.name); tstr_free(owner->value.service.profile); break;
    case PRAKTOR_YAML_ACTION_MANAGED_PROCESS:
        tstr_free(owner->value.process.image_name); tstr_free(owner->value.process.executable); tstr_free(owner->value.process.working_directory); break;
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
    if (!out || (kind != PRAKTOR_YAML_ACTION_DOWNLOAD && kind != PRAKTOR_YAML_ACTION_SERVICE && kind != PRAKTOR_YAML_ACTION_MANAGED_PROCESS)) return invalid(error);
    owner = (praktor_yaml_action_params *)calloc(1, sizeof(*owner));
    if (!owner) return owned_failure(error, "Failed to allocate YAML action parameters");
    owner->kind = kind;
    if (vec_init_bytes(&owner->arguments, sizeof(tstr), _Alignof(tstr), PRAKTOR_YAML_ACTION_MAX_ARGUMENTS) != STL_OK) {
        free(owner); return invalid(error);
    }
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
        owner->value.service.profile ? text_view(owner->value.service.profile) : vstr_from_cstr("windows_scm"), vec_size(&owner->arguments),
        owner->value.service.timeout_ms, owner->value.service.poll_interval_ms};
    return SALTS_OK;
}
int praktor_yaml_action_managed_process(const praktor_yaml_action_params *owner, praktor_yaml_managed_process_view *out) {
    if (!owner || !out || owner->kind != PRAKTOR_YAML_ACTION_MANAGED_PROCESS) return SALTS_EINVAL;
    *out = (praktor_yaml_managed_process_view){owner->value.process.operation, text_view(owner->value.process.image_name),
        text_view(owner->value.process.executable), text_view(owner->value.process.working_directory), vec_size(&owner->arguments),
        owner->value.process.startup_timeout_ms, owner->value.process.stop_timeout_ms, owner->value.process.force_terminate};
    return SALTS_OK;
}
int praktor_yaml_action_argument(const praktor_yaml_action_params *owner, size_t index, vstr *out) {
    const tstr *text;
    if (!owner || !out) return SALTS_EINVAL;
    text = (const tstr *)vec_at_const(&owner->arguments, index);
    if (!text) return SALTS_ENOENT;
    *out = text_view(*text);
    return SALTS_OK;
}
