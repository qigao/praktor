#ifndef PRAKTOR_YAML_TASK_FIELDS_H
#define PRAKTOR_YAML_TASK_FIELDS_H
#include "yml/yaml_fields.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API, outside the installed SDK ABI. The input is the
 * selected block (not its parent task). No execution/context/document is retained. */
typedef enum praktor_yaml_task_field_kind {
    PRAKTOR_YAML_STRING_LIST, PRAKTOR_YAML_STRING_MAP, PRAKTOR_YAML_EACH,
    PRAKTOR_YAML_TRIGGERS, PRAKTOR_YAML_DYNAMIC_TASKS, PRAKTOR_YAML_USES
} praktor_yaml_task_field_kind;
#define PRAKTOR_YAML_TASK_MAX_ENTRIES 4096u
#define PRAKTOR_YAML_TASK_MAX_STRINGS 16384u
#define PRAKTOR_YAML_TASK_MAX_BYTES (1024u * 1024u)
typedef struct praktor_yaml_task_fields praktor_yaml_task_fields;
typedef struct praktor_yaml_string_list praktor_yaml_string_list;
typedef struct praktor_yaml_string_map praktor_yaml_string_map;
typedef struct praktor_yaml_list_map praktor_yaml_list_map;

/* Unique owner. Transfer the pointer, never copy ownership. Construction copies
 * decoded strings, publishes only on success and leaves *out untouched on failure.
 * Destroy accepts NULL. Independent owners support concurrent use; destruction
 * must not overlap any read of that owner. Views survive source destruction and
 * pointer transfer, and expire on owner destruction. Error storage follows
 * yaml_fields.h and must be disjoint from inputs/outputs.
 *
 * Lists/maps admit at most 4096 entries, construction expands at most 16384
 * data strings (including overwritten values/keys), and retained plus provisional
 * decoded text admits 1 MiB including terminators. Individual strings preserve
 * legacy length policy and decoded-NUL truncation. These limits do not bound
 * validation/lookup decoding, parser/temporary decode allocations or allocator/container overhead. Excess
 * fails closed. SALTS_OK, EINVAL (shape/value/capacity/arguments) or ENOMEM;
 * the error origin distinguishes owned allocation from provider failure.
 * Example: create(block, PRAKTOR_YAML_EACH, &owner, &error), read each(owner,
 * &view), consume borrowed data, destroy(owner); dispose error on every path. */
int praktor_yaml_task_fields_create(praktor_yaml_node node, praktor_yaml_task_field_kind kind,
    praktor_yaml_task_fields **out, praktor_yaml_field_error *error);
void praktor_yaml_task_fields_destroy(praktor_yaml_task_fields *owner);

typedef struct praktor_yaml_each_view {
    const praktor_yaml_string_list *items;
    const praktor_yaml_list_map *matrix;
    vstr as, index_variable;
} praktor_yaml_each_view;
typedef struct praktor_yaml_triggers_view {
    const praktor_yaml_string_list *on_success, *on_failure, *on_complete;
} praktor_yaml_triggers_view;
typedef struct praktor_yaml_dynamic_view {
    vstr items_variable, name, command, timeout, when;
    bool sequence, has_timeout, has_when;
    const praktor_yaml_string_list *commands, *depends_on;
    const praktor_yaml_string_map *env;
} praktor_yaml_dynamic_view;
int praktor_yaml_task_list(const praktor_yaml_task_fields *owner, const praktor_yaml_string_list **out);
int praktor_yaml_task_map(const praktor_yaml_task_fields *owner, const praktor_yaml_string_map **out);
int praktor_yaml_task_each(const praktor_yaml_task_fields *owner, praktor_yaml_each_view *out);
int praktor_yaml_task_triggers(const praktor_yaml_task_fields *owner, praktor_yaml_triggers_view *out);
int praktor_yaml_task_dynamic(const praktor_yaml_task_fields *owner, praktor_yaml_dynamic_view *out);
int praktor_yaml_task_uses(const praktor_yaml_task_fields *owner, vstr *out);

/* Allocation-free borrowed access, strong output on EINVAL/ENOENT. Text is also
 * NUL terminated. Maps iterate in first insertion order; duplicate decoded keys
 * replace values without moving that key. This order preserves the temporary
 * legacy unordered_map projection's task expansion order. Size(NULL) is zero. */
typedef struct praktor_yaml_string_entry { vstr key, value; } praktor_yaml_string_entry;
typedef struct praktor_yaml_list_entry { vstr key; const praktor_yaml_string_list *values; } praktor_yaml_list_entry;
size_t praktor_yaml_string_list_size(const praktor_yaml_string_list *list);
size_t praktor_yaml_string_map_size(const praktor_yaml_string_map *map);
size_t praktor_yaml_list_map_size(const praktor_yaml_list_map *map);
int praktor_yaml_string_list_at(const praktor_yaml_string_list *list, size_t index, vstr *out);
int praktor_yaml_string_map_at(const praktor_yaml_string_map *map, size_t index, praktor_yaml_string_entry *out);
int praktor_yaml_list_map_at(const praktor_yaml_list_map *map, size_t index, praktor_yaml_list_entry *out);

#ifdef __cplusplus
}
#endif
#endif
