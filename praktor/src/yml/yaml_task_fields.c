#include "yml/yaml_task_fields.h"
#include "yaml_diagnostic_internal.h"
#include <cstl/map.h>
#include <cstl/vec.h>
#include <fmt.h>
#include <stdlib.h>
#include <string.h>

struct praktor_yaml_string_list { vec_t texts; };
typedef struct task_entry { tstr key, value; praktor_yaml_string_list *values; } task_entry;
/* Entries own data; the tree is only an index borrowing immutable entry keys.
 * Index values survive vector relocation. Destroy the index before its keys. */
typedef struct task_map { vec_t entries; map_t index; } task_map;
struct praktor_yaml_string_map { task_map storage; };
struct praktor_yaml_list_map { task_map storage; };
struct praktor_yaml_task_fields {
    praktor_yaml_task_field_kind kind;
    size_t retained_bytes, expanded_strings;
    praktor_yaml_string_list lists[3];
    praktor_yaml_string_map strings;
    praktor_yaml_list_map matrix;
    tstr as, index_variable, items_variable, name, command, timeout, when, uses;
    bool sequence, has_timeout, has_when;
};
static int tf_oom(praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail(error, SALTS_ENOMEM, PRAKTOR_YAML_FAILURE_ALLOCATION,
        error ? tstr_dup("Failed to construct YAML task fields") : NULL);
}
static int tf_invalid(praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail(error, SALTS_EINVAL, PRAKTOR_YAML_FAILURE_INPUT,
        error ? tstr_dup("Invalid YAML task field argument") : NULL);
}
static int tf_error(praktor_yaml_node node, const char *message, praktor_yaml_field_error *error) {
    return praktor_yaml_field_fail_at(node, vstr_from_cstr(message), error);
}
static int tf_limit(praktor_yaml_node node, const char *unit, size_t maximum, praktor_yaml_field_error *error) {
    tstr message;
    int status;
    if (!error) return SALTS_EINVAL;
    message = tstr_format("YAML task {} exceeds maximum of {}", unit, maximum);
    if (!message) return tf_oom(error);
    status = praktor_yaml_field_fail_at(node, tstr_to_v(message), error);
    tstr_free(message); return status;
}
static void tf_release(praktor_yaml_task_fields *owner, tstr text) {
    if (text) owner->retained_bytes -= tstr_len(text) + 1u;
    tstr_free(text);
}
static int tf_decode(praktor_yaml_task_fields *owner, praktor_yaml_node node, bool key,
    tstr *out, praktor_yaml_field_error *error) {
    tstr text = NULL;
    size_t length;
    int status;
    if (owner->expanded_strings == PRAKTOR_YAML_TASK_MAX_STRINGS)
        return tf_limit(node, "expanded string count", PRAKTOR_YAML_TASK_MAX_STRINGS, error);
    ++owner->expanded_strings;
    status = praktor_yaml_field_decode(node, key, &text, error);
    if (status != SALTS_OK) return status;
    length = tstr_len(text);
    if (length >= PRAKTOR_YAML_TASK_MAX_BYTES - owner->retained_bytes) {
        tstr_free(text); return tf_limit(node, "retained text bytes", PRAKTOR_YAML_TASK_MAX_BYTES, error);
    }
    owner->retained_bytes += length + 1u; *out = text; return SALTS_OK;
}
static void tf_list_init(praktor_yaml_string_list *list) {
    vec_init_bytes(&list->texts, sizeof(tstr), _Alignof(tstr), PRAKTOR_YAML_TASK_MAX_ENTRIES);
}
static void tf_list_dispose(praktor_yaml_task_fields *owner, praktor_yaml_string_list *list) {
    size_t index;
    if (!list) return;
    for (index = 0; index < vec_size(&list->texts); ++index)
        tf_release(owner, *(tstr *)vec_at(&list->texts, index));
    vec_destroy(&list->texts);
}
static void tf_entry_dispose(praktor_yaml_task_fields *owner, task_entry *entry) {
    tf_release(owner, entry->key); tf_release(owner, entry->value);
    if (entry->values) { tf_list_dispose(owner, entry->values); free(entry->values); }
}
static void tf_map_dispose(praktor_yaml_task_fields *owner, task_map *map) {
    size_t index;
    map_destroy(&map->index);
    for (index = 0; index < vec_size(&map->entries); ++index)
        tf_entry_dispose(owner, (task_entry *)vec_at(&map->entries, index));
    vec_destroy(&map->entries);
}
void praktor_yaml_task_fields_destroy(praktor_yaml_task_fields *owner) {
    size_t index;
    if (!owner) return;
    for (index = 0; index < 3; ++index) tf_list_dispose(owner, &owner->lists[index]);
    tf_map_dispose(owner, &owner->strings.storage); tf_map_dispose(owner, &owner->matrix.storage);
    tf_release(owner, owner->as); tf_release(owner, owner->index_variable);
    tf_release(owner, owner->items_variable); tf_release(owner, owner->name);
    tf_release(owner, owner->command); tf_release(owner, owner->timeout);
    tf_release(owner, owner->when); tf_release(owner, owner->uses); free(owner);
}
static int tf_compare(const void *left, const void *right, void *context) {
    (void)context; return strcmp(*(const tstr *)left, *(const tstr *)right);
}
static int tf_map_init(task_map *map, praktor_yaml_field_error *error) {
    stl_status status;
    if (map->index.impl) return SALTS_OK;
    status = map_init_bytes(&map->index, sizeof(tstr), _Alignof(tstr), sizeof(size_t), _Alignof(size_t),
        PRAKTOR_YAML_TASK_MAX_ENTRIES, tf_compare, NULL);
    return status == STL_OK ? SALTS_OK : tf_oom(error);
}
/* Consumes the candidate on all paths; replacement never changes insertion order. */
static int tf_store(praktor_yaml_task_fields *owner, task_map *map, praktor_yaml_node node,
    task_entry candidate, praktor_yaml_field_error *error) {
    const size_t *previous;
    size_t index;
    stl_status stored;
    int status = tf_map_init(map, error);
    if (status != SALTS_OK) { tf_entry_dispose(owner, &candidate); return status; }
    previous = (const size_t *)map_get_const(&map->index, &candidate.key);
    if (previous) {
        task_entry *entry = (task_entry *)vec_at(&map->entries, *previous);
        tf_release(owner, candidate.key); tf_release(owner, entry->value);
        if (entry->values) { tf_list_dispose(owner, entry->values); free(entry->values); }
        entry->value = candidate.value; entry->values = candidate.values; return SALTS_OK;
    }
    index = vec_size(&map->entries);
    if (index == PRAKTOR_YAML_TASK_MAX_ENTRIES) {
        tf_entry_dispose(owner, &candidate);
        return tf_limit(node, "map entry count", PRAKTOR_YAML_TASK_MAX_ENTRIES, error);
    }
    stored = vec_push(&map->entries, &candidate);
    if (stored != STL_OK) { tf_entry_dispose(owner, &candidate); return tf_oom(error); }
    /* The entries vector owns the candidate even if index allocation now fails. */
    stored = map_put(&map->index, &candidate.key, &index);
    return stored == STL_OK ? SALTS_OK : tf_oom(error);
}
static int tf_append(praktor_yaml_task_fields *owner, praktor_yaml_string_list *list,
    praktor_yaml_node node, bool skip_empty, praktor_yaml_field_error *error) {
    tstr text = NULL;
    int status = tf_decode(owner, node, false, &text, error);
    if (status != SALTS_OK) return status;
    if (skip_empty && !tstr_len(text)) { tf_release(owner, text); return SALTS_OK; }
    if (vec_size(&list->texts) == PRAKTOR_YAML_TASK_MAX_ENTRIES) {
        tf_release(owner, text); return tf_limit(node, "list entry count", PRAKTOR_YAML_TASK_MAX_ENTRIES, error);
    }
    if (vec_push(&list->texts, &text) != STL_OK) { tf_release(owner, text); return tf_oom(error); }
    return SALTS_OK;
}
static int tf_list(praktor_yaml_task_fields *owner, praktor_yaml_node node,
    praktor_yaml_string_list *list, praktor_yaml_field_error *error) {
    size_t index;
    int status;
    if (praktor_yaml_is_seq(node)) {
        for (index = 0; index < praktor_yaml_count(node); ++index) {
            status = tf_append(owner, list, praktor_yaml_at(node, index), false, error);
            if (status != SALTS_OK) return status;
        }
        return SALTS_OK;
    }
    if (praktor_yaml_has_val(node)) return tf_append(owner, list, node, true, error);
    return tf_error(node, "Expected sequence or scalar when converting to string list", error);
}
static int tf_map(praktor_yaml_task_fields *owner, praktor_yaml_node node, task_map *map,
    bool matrix, praktor_yaml_field_error *error) {
    size_t index;
    int status;
    if (!praktor_yaml_is_map(node)) return tf_error(node, matrix ? "each.matrix must be a map" :
        "Expected mapping when converting to string map", error);
    for (index = 0; index < praktor_yaml_count(node); ++index) {
        praktor_yaml_node child = praktor_yaml_at(node, index);
        task_entry entry = {0};
        status = tf_decode(owner, child, true, &entry.key, error);
        if (status != SALTS_OK) return status;
        if (matrix) {
            entry.values = (praktor_yaml_string_list *)calloc(1, sizeof(*entry.values));
            if (!entry.values) { tf_entry_dispose(owner, &entry); return tf_oom(error); }
            tf_list_init(entry.values);
            status = tf_list(owner, child, entry.values, error);
            if (status == SALTS_OK && !vec_size(&entry.values->texts)) {
                tstr message = error ? tstr_format("each.matrix dimension '{}' must not be empty", entry.key) : NULL;
                status = !error ? SALTS_EINVAL : !message ? tf_oom(error) :
                    praktor_yaml_field_fail_at(child, tstr_to_v(message), error);
                tstr_free(message);
            }
        } else status = tf_decode(owner, child, false, &entry.value, error);
        if (status != SALTS_OK) { tf_entry_dispose(owner, &entry); return status; }
        status = tf_store(owner, map, child, entry, error);
        if (status != SALTS_OK) return status;
    }
    return SALTS_OK;
}
static int tf_optional(praktor_yaml_task_fields *owner, praktor_yaml_node node,
    const char *key, tstr *out, praktor_yaml_field_error *error) {
    praktor_yaml_node field = {0};
    int status;
    if (!praktor_yaml_has(node, key)) return SALTS_OK;
    status = praktor_yaml_field_get(node, key, &field, error);
    return status == SALTS_OK ? tf_decode(owner, field, false, out, error) : status;
}
#define TF_KEY(text) {text, sizeof(text) - 1u}
static int tf_each(praktor_yaml_task_fields *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {TF_KEY("items"), TF_KEY("matrix"), TF_KEY("as"), TF_KEY("index_variable")};
    praktor_yaml_node field = {0};
    int status;
    if (!praktor_yaml_is_map(node)) return tf_error(node, "each must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(*keys), error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(node, "items")) {
        status = praktor_yaml_field_get(node, "items", &field, error);
        if (status == SALTS_OK) status = tf_list(owner, field, &owner->lists[0], error);
        if (status != SALTS_OK) return status;
    }
    if (praktor_yaml_has(node, "matrix")) {
        status = praktor_yaml_field_get(node, "matrix", &field, error);
        if (status == SALTS_OK) status = tf_map(owner, field, &owner->matrix.storage, true, error);
        if (status != SALTS_OK) return status;
    }
    if (vec_size(&owner->lists[0].texts) && vec_size(&owner->matrix.storage.entries))
        return tf_error(node, "each cannot define both 'items' and 'matrix'", error);
    if (!vec_size(&owner->lists[0].texts) && !vec_size(&owner->matrix.storage.entries))
        return tf_error(node, "each requires either 'items' or 'matrix'", error);
    status = tf_optional(owner, node, "as", &owner->as, error);
    if (status != SALTS_OK) return status;
    if (owner->as && !tstr_len(owner->as)) { tf_release(owner, owner->as); owner->as = NULL; }
    return tf_optional(owner, node, "index_variable", &owner->index_variable, error);
}
static int tf_triggers(praktor_yaml_task_fields *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {TF_KEY("on_success"), TF_KEY("on_failure"), TF_KEY("on_complete")};
    size_t group, index;
    bool any = false;
    int status;
    if (!praktor_yaml_is_map(node)) return tf_error(node, "triggers must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(*keys), error);
    if (status != SALTS_OK) return status;
    for (group = 0; group < 3; ++group) {
        praktor_yaml_node field = {0};
        if (!praktor_yaml_has(node, keys[group].data)) continue;
        any = true;
        status = praktor_yaml_field_get(node, keys[group].data, &field, error);
        if (status != SALTS_OK) return status;
        if (!praktor_yaml_is_seq(field)) return tf_error(field, "Trigger action list must be a sequence", error);
        for (index = 0; index < praktor_yaml_count(field); ++index) {
            praktor_yaml_node item = praktor_yaml_at(field, index);
            if (!praktor_yaml_has_val(item)) return tf_error(item, "Trigger action must be a task name (string)", error);
            status = tf_append(owner, &owner->lists[group], item, false, error);
            if (status != SALTS_OK) return status;
            if (!tstr_len(*(tstr *)vec_at(&owner->lists[group].texts, index)))
                return tf_error(item, "Trigger action task name cannot be empty", error);
        }
        if (!vec_size(&owner->lists[group].texts)) return tf_error(field, "Trigger action list must contain at least one task name", error);
    }
    return any ? SALTS_OK : tf_error(node, "triggers block must contain at least one action list", error);
}
static int tf_required(praktor_yaml_task_fields *owner, praktor_yaml_node node, const char *key,
    const char *missing, const char *empty, tstr *out, praktor_yaml_field_error *error) {
    praktor_yaml_node field = {0};
    int status;
    if (!praktor_yaml_has(node, key)) return tf_error(node, missing, error);
    status = praktor_yaml_field_get(node, key, &field, error);
    if (status == SALTS_OK) status = tf_decode(owner, field, false, out, error);
    if (status != SALTS_OK) return status;
    return tstr_len(*out) ? SALTS_OK : tf_error(field, empty, error);
}
static int tf_dynamic(praktor_yaml_task_fields *owner, praktor_yaml_node node, praktor_yaml_field_error *error) {
    static const vstr keys[] = {TF_KEY("items_variable"), TF_KEY("template")};
    static const vstr template_keys[] = {TF_KEY("name"), TF_KEY("command"), TF_KEY("timeout"), TF_KEY("when"), TF_KEY("depends_on"), TF_KEY("env")};
    praktor_yaml_node template_node = {0}, field = {0};
    size_t index;
    int status;
    if (!praktor_yaml_is_map(node)) return tf_error(node, "dynamic_tasks must be a map", error);
    status = praktor_yaml_check_keys(node, keys, sizeof(keys) / sizeof(*keys), error);
    if (status != SALTS_OK) return status;
    status = tf_required(owner, node, "items_variable", "dynamic_tasks requires 'items_variable'",
        "dynamic_tasks 'items_variable' cannot be empty", &owner->items_variable, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(node, "template")) return tf_error(node, "dynamic_tasks requires 'template'", error);
    status = praktor_yaml_field_get(node, "template", &template_node, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_is_map(template_node)) return tf_error(template_node, "dynamic_tasks 'template' must be a map", error);
    status = praktor_yaml_check_keys(template_node, template_keys, sizeof(template_keys) / sizeof(*template_keys), error);
    if (status != SALTS_OK) return status;
    status = tf_required(owner, template_node, "name", "dynamic_tasks template requires 'name'",
        "dynamic_tasks template 'name' cannot be empty", &owner->name, error);
    if (status != SALTS_OK) return status;
    if (!praktor_yaml_has(template_node, "command")) return tf_error(template_node, "dynamic_tasks template requires 'command'", error);
    status = praktor_yaml_field_get(template_node, "command", &field, error);
    if (status != SALTS_OK) return status;
    owner->sequence = praktor_yaml_is_seq(field);
    if (owner->sequence) {
        status = tf_list(owner, field, &owner->lists[0], error);
        if (status != SALTS_OK) return status;
        if (!vec_size(&owner->lists[0].texts)) return tf_error(field, "dynamic_tasks template 'command' cannot be empty", error);
        for (index = 0; index < vec_size(&owner->lists[0].texts); ++index)
            if (!tstr_len(*(tstr *)vec_at(&owner->lists[0].texts, index)))
                return tf_error(field, "dynamic_tasks template command entries cannot be empty", error);
    } else {
        status = tf_decode(owner, field, false, &owner->command, error);
        if (status != SALTS_OK) return status;
        if (!tstr_len(owner->command)) return tf_error(field, "dynamic_tasks template 'command' cannot be empty", error);
    }
    owner->has_timeout = praktor_yaml_has(template_node, "timeout");
    owner->has_when = praktor_yaml_has(template_node, "when");
    status = tf_optional(owner, template_node, "timeout", &owner->timeout, error);
    if (status == SALTS_OK) status = tf_optional(owner, template_node, "when", &owner->when, error);
    if (status != SALTS_OK) return status;
    if (praktor_yaml_has(template_node, "depends_on")) {
        status = praktor_yaml_field_get(template_node, "depends_on", &field, error);
        if (status == SALTS_OK) status = tf_list(owner, field, &owner->lists[1], error);
        if (status != SALTS_OK) return status;
    }
    if (praktor_yaml_has(template_node, "env")) {
        status = praktor_yaml_field_get(template_node, "env", &field, error);
        if (status == SALTS_OK) status = tf_map(owner, field, &owner->strings.storage, false, error);
    }
    return status;
}
int praktor_yaml_task_fields_create(praktor_yaml_node node, praktor_yaml_task_field_kind kind,
    praktor_yaml_task_fields **out, praktor_yaml_field_error *error) {
    praktor_yaml_task_fields *owner;
    size_t index;
    int status;
    if (error) praktor_yaml_field_error_dispose(error);
    if (!out || !node.document || !node.node || kind < PRAKTOR_YAML_STRING_LIST || kind > PRAKTOR_YAML_USES)
        return tf_invalid(error);
    owner = (praktor_yaml_task_fields *)calloc(1, sizeof(*owner));
    if (!owner) return tf_oom(error);
    owner->kind = kind;
    for (index = 0; index < 3; ++index) tf_list_init(&owner->lists[index]);
    vec_init_bytes(&owner->strings.storage.entries, sizeof(task_entry), _Alignof(task_entry), PRAKTOR_YAML_TASK_MAX_ENTRIES);
    vec_init_bytes(&owner->matrix.storage.entries, sizeof(task_entry), _Alignof(task_entry), PRAKTOR_YAML_TASK_MAX_ENTRIES);
    switch (kind) {
    case PRAKTOR_YAML_STRING_LIST: status = tf_list(owner, node, &owner->lists[0], error); break;
    case PRAKTOR_YAML_STRING_MAP: status = tf_map(owner, node, &owner->strings.storage, false, error); break;
    case PRAKTOR_YAML_EACH: status = tf_each(owner, node, error); break;
    case PRAKTOR_YAML_TRIGGERS: status = tf_triggers(owner, node, error); break;
    case PRAKTOR_YAML_DYNAMIC_TASKS: status = tf_dynamic(owner, node, error); break;
    case PRAKTOR_YAML_USES:
        status = tf_decode(owner, node, false, &owner->uses, error);
        if (status == SALTS_OK && !tstr_len(owner->uses)) status = tf_error(node, "uses value cannot be empty", error);
        break;
    default: status = tf_invalid(error); break;
    }
    if (status != SALTS_OK) { praktor_yaml_task_fields_destroy(owner); return status; }
    *out = owner; return SALTS_OK;
}
static vstr tf_view(tstr text) { return text ? tstr_to_v(text) : vstr_from_cstr(""); }
#define TF_GET(kind_value) if (!owner || !out || owner->kind != kind_value) return SALTS_EINVAL
int praktor_yaml_task_list(const praktor_yaml_task_fields *owner, const praktor_yaml_string_list **out) {
    TF_GET(PRAKTOR_YAML_STRING_LIST); *out = &owner->lists[0]; return SALTS_OK;
}
int praktor_yaml_task_map(const praktor_yaml_task_fields *owner, const praktor_yaml_string_map **out) {
    TF_GET(PRAKTOR_YAML_STRING_MAP); *out = &owner->strings; return SALTS_OK;
}
int praktor_yaml_task_each(const praktor_yaml_task_fields *owner, praktor_yaml_each_view *out) {
    TF_GET(PRAKTOR_YAML_EACH);
    *out = (praktor_yaml_each_view){&owner->lists[0], &owner->matrix,
        owner->as ? tf_view(owner->as) : vstr_from_cstr("item"), tf_view(owner->index_variable)};
    return SALTS_OK;
}
int praktor_yaml_task_triggers(const praktor_yaml_task_fields *owner, praktor_yaml_triggers_view *out) {
    TF_GET(PRAKTOR_YAML_TRIGGERS);
    *out = (praktor_yaml_triggers_view){&owner->lists[0], &owner->lists[1], &owner->lists[2]}; return SALTS_OK;
}
int praktor_yaml_task_dynamic(const praktor_yaml_task_fields *owner, praktor_yaml_dynamic_view *out) {
    TF_GET(PRAKTOR_YAML_DYNAMIC_TASKS);
    *out = (praktor_yaml_dynamic_view){tf_view(owner->items_variable), tf_view(owner->name), tf_view(owner->command),
        tf_view(owner->timeout), tf_view(owner->when), owner->sequence, owner->has_timeout, owner->has_when,
        &owner->lists[0], &owner->lists[1], &owner->strings}; return SALTS_OK;
}
int praktor_yaml_task_uses(const praktor_yaml_task_fields *owner, vstr *out) {
    TF_GET(PRAKTOR_YAML_USES); *out = tf_view(owner->uses); return SALTS_OK;
}
size_t praktor_yaml_string_list_size(const praktor_yaml_string_list *list) { return list ? vec_size(&list->texts) : 0u; }
size_t praktor_yaml_string_map_size(const praktor_yaml_string_map *map) { return map ? vec_size(&map->storage.entries) : 0u; }
size_t praktor_yaml_list_map_size(const praktor_yaml_list_map *map) { return map ? vec_size(&map->storage.entries) : 0u; }
int praktor_yaml_string_list_at(const praktor_yaml_string_list *list, size_t index, vstr *out) {
    if (!list || !out) return SALTS_EINVAL;
    if (index >= vec_size(&list->texts)) return SALTS_ENOENT;
    *out = tf_view(*(tstr const *)vec_at_const(&list->texts, index)); return SALTS_OK;
}
int praktor_yaml_string_map_at(const praktor_yaml_string_map *map, size_t index, praktor_yaml_string_entry *out) {
    const task_entry *entry;
    if (!map || !out) return SALTS_EINVAL;
    if (index >= vec_size(&map->storage.entries)) return SALTS_ENOENT;
    entry = (const task_entry *)vec_at_const(&map->storage.entries, index);
    *out = (praktor_yaml_string_entry){tf_view(entry->key), tf_view(entry->value)}; return SALTS_OK;
}
int praktor_yaml_list_map_at(const praktor_yaml_list_map *map, size_t index, praktor_yaml_list_entry *out) {
    const task_entry *entry;
    if (!map || !out) return SALTS_EINVAL;
    if (index >= vec_size(&map->storage.entries)) return SALTS_ENOENT;
    entry = (const task_entry *)vec_at_const(&map->storage.entries, index);
    *out = (praktor_yaml_list_entry){tf_view(entry->key), entry->values}; return SALTS_OK;
}
