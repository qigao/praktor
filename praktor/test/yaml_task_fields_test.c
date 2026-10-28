#include "yml/yaml_task_fields.h"
#include <fmt.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *doc = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &doc, NULL) == SALTS_OK);
    return doc;
}
static praktor_yaml_task_fields *fields(const char *source, praktor_yaml_task_field_kind kind) {
    praktor_yaml_document *doc = document(source);
    praktor_yaml_task_fields *owner = NULL;
    praktor_yaml_field_error error = {0};
    int status = praktor_yaml_task_fields_create(praktor_yaml_root(doc), kind, &owner, &error);
    info("status %d: %s", status, error.text ? error.text : "");
    check(status == SALTS_OK && owner && !error.text);
    praktor_yaml_document_destroy(doc); praktor_yaml_field_error_dispose(&error); return owner;
}
static bool text_is(vstr text, const char *expected) { return vstr_eq(text, vstr_from_cstr(expected)) != 0; }
static void list_is(const praktor_yaml_string_list *list, size_t index, const char *expected) {
    vstr text = {0};
    check(praktor_yaml_string_list_at(list, index, &text) == SALTS_OK && text_is(text, expected));
}
static void refuses(const char *source, praktor_yaml_task_field_kind kind, const char *message) {
    praktor_yaml_document *doc = document(source);
    praktor_yaml_task_fields *saved = fields("retained", PRAKTOR_YAML_USES), *out = saved;
    praktor_yaml_field_error error = {0};
    vstr text = {0};
    int status = praktor_yaml_task_fields_create(praktor_yaml_root(doc), kind, &out, &error);
    info("status %d: %s", status, error.text ? error.text : "");
    check(status == SALTS_EINVAL && out == saved && error.origin == PRAKTOR_YAML_FAILURE_INPUT);
    check(error.text && strstr(error.text, message));
    check(praktor_yaml_task_uses(saved, &text) == SALTS_OK && text_is(text, "retained"));
    praktor_yaml_task_fields_destroy(saved); praktor_yaml_document_destroy(doc); praktor_yaml_field_error_dispose(&error);
}
static void independent(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 50 && *passed; ++index) {
        praktor_yaml_document *doc = NULL;
        praktor_yaml_task_fields *owner = NULL;
        praktor_yaml_each_view view = {0};
        praktor_yaml_list_entry entry = {0};
        vstr text = {0};
        *passed = praktor_yaml_document_create(vstr_from_cstr("matrix: {x: [a, b], y: [1]}\n"), &doc, NULL) == SALTS_OK &&
            praktor_yaml_task_fields_create(praktor_yaml_root(doc), PRAKTOR_YAML_EACH, &owner, NULL) == SALTS_OK;
        praktor_yaml_document_destroy(doc);
        *passed = *passed && praktor_yaml_task_each(owner, &view) == SALTS_OK &&
            praktor_yaml_list_map_at(view.matrix, 0, &entry) == SALTS_OK &&
            praktor_yaml_string_list_at(entry.values, 1, &text) == SALTS_OK && text_is(text, "b");
        praktor_yaml_task_fields_destroy(owner);
    }
}
spec("C-owned YAML task fields") {
    it("owns scalar and sequence lists after document destruction") {
        praktor_yaml_task_fields *owner = fields("['', true, 7, \"x\\0y\"]", PRAKTOR_YAML_STRING_LIST);
        const praktor_yaml_string_list *list = NULL;
        check(praktor_yaml_task_list(owner, &list) == SALTS_OK && praktor_yaml_string_list_size(list) == 4);
        list_is(list, 0, ""); list_is(list, 1, "true"); list_is(list, 2, "7"); list_is(list, 3, "x");
        praktor_yaml_task_fields_destroy(owner);
        owner = fields("''", PRAKTOR_YAML_STRING_LIST);
        check(praktor_yaml_task_list(owner, &list) == SALTS_OK && !praktor_yaml_string_list_size(list));
        praktor_yaml_task_fields_destroy(owner);
        refuses("{}", PRAKTOR_YAML_STRING_LIST, "Expected sequence or scalar");
        refuses("[a, {}]", PRAKTOR_YAML_STRING_LIST, "Expected YAML scalar");
    }
    it("replaces decoded duplicate map values without moving first insertion") {
        praktor_yaml_task_fields *owner = fields("z: first\na: one\n\"z\\0last\": last\n\"a\\0suffix\": two\n", PRAKTOR_YAML_STRING_MAP), *moved = owner;
        const praktor_yaml_string_map *map = NULL;
        praktor_yaml_string_entry entry = {0};
        owner = NULL;
        check(praktor_yaml_task_map(moved, &map) == SALTS_OK && praktor_yaml_string_map_size(map) == 2);
        check(praktor_yaml_string_map_at(map, 0, &entry) == SALTS_OK && text_is(entry.key, "z") && text_is(entry.value, "last"));
        check(praktor_yaml_string_map_at(map, 1, &entry) == SALTS_OK && text_is(entry.key, "a") && text_is(entry.value, "two"));
        check(praktor_yaml_string_map_at(map, 2, &entry) == SALTS_ENOENT && text_is(entry.key, "a") && text_is(entry.value, "two"));
        praktor_yaml_task_fields_destroy(moved); praktor_yaml_task_fields_destroy(owner);
        refuses("[]", PRAKTOR_YAML_STRING_MAP, "Expected mapping");
        refuses("x: []", PRAKTOR_YAML_STRING_MAP, "Expected YAML scalar");
    }
    it("retains Each content-based selection defaults and matrix order") {
        praktor_yaml_task_fields *owner = fields("items: []\nmatrix: {z: [a, b], x: c, \"z\\0last\": [last]}\nas: ''\nindex_variable: 7\n", PRAKTOR_YAML_EACH);
        praktor_yaml_each_view view = {0};
        praktor_yaml_list_entry entry = {0};
        check(praktor_yaml_task_each(owner, &view) == SALTS_OK && text_is(view.as, "item") && text_is(view.index_variable, "7"));
        check(!praktor_yaml_string_list_size(view.items) && praktor_yaml_list_map_size(view.matrix) == 2);
        check(praktor_yaml_list_map_at(view.matrix, 0, &entry) == SALTS_OK && text_is(entry.key, "z")); list_is(entry.values, 0, "last");
        check(praktor_yaml_list_map_at(view.matrix, 2, &entry) == SALTS_ENOENT && text_is(entry.key, "z")); list_is(entry.values, 0, "last");
        praktor_yaml_task_fields_destroy(owner);
        owner = fields("items: [a]\nmatrix: {}\nas: value\n", PRAKTOR_YAML_EACH);
        check(praktor_yaml_task_each(owner, &view) == SALTS_OK && text_is(view.as, "value")); list_is(view.items, 0, "a");
        praktor_yaml_task_fields_destroy(owner);
    }
    it("refuses Each conflicts empty dimensions and unknown keys in original order") {
        refuses("[]", PRAKTOR_YAML_EACH, "each must be a map");
        refuses("items: [a]\nunknown: x", PRAKTOR_YAML_EACH, "Unknown key: 'unknown'");
        refuses("items: [a]\nmatrix: {x: b}", PRAKTOR_YAML_EACH, "each cannot define both");
        refuses("items: []\nmatrix: {}", PRAKTOR_YAML_EACH, "each requires either");
        refuses("matrix: []", PRAKTOR_YAML_EACH, "each.matrix must be a map");
        refuses("matrix: {x: [], \"x\\0last\": [a]}", PRAKTOR_YAML_EACH, "dimension 'x' must not be empty");
    }
    it("preserves trigger groups scalar types and fixed validation order") {
        praktor_yaml_task_fields *owner = fields("on_complete: [end]\non_success: [true, 7]\non_failure: [retry]", PRAKTOR_YAML_TRIGGERS);
        praktor_yaml_triggers_view view = {0};
        check(praktor_yaml_task_triggers(owner, &view) == SALTS_OK);
        list_is(view.on_success, 0, "true"); list_is(view.on_success, 1, "7");
        list_is(view.on_failure, 0, "retry"); list_is(view.on_complete, 0, "end"); praktor_yaml_task_fields_destroy(owner);
        refuses("{}", PRAKTOR_YAML_TRIGGERS, "at least one action list");
        refuses("on_complete: {}\non_success: []", PRAKTOR_YAML_TRIGGERS, "must contain at least one task name");
        refuses("on_success: x", PRAKTOR_YAML_TRIGGERS, "must be a sequence");
        refuses("on_success: [{}]", PRAKTOR_YAML_TRIGGERS, "must be a task name");
        refuses("on_success: ['']", PRAKTOR_YAML_TRIGGERS, "task name cannot be empty");
    }
    it("owns dynamic template lists maps and explicitly empty optional fields") {
        praktor_yaml_task_fields *owner = fields("items_variable: jobs\ntemplate: {name: '${item}', command: [echo, 7], timeout: '', when: '', depends_on: ['', ready], env: {z: old, a: true, \"z\\0last\": last}}", PRAKTOR_YAML_DYNAMIC_TASKS);
        praktor_yaml_dynamic_view view = {0};
        praktor_yaml_string_entry entry = {0};
        check(praktor_yaml_task_dynamic(owner, &view) == SALTS_OK && view.sequence && view.has_timeout && view.has_when);
        check(text_is(view.items_variable, "jobs") && text_is(view.name, "${item}") && !view.timeout.len && !view.when.len);
        list_is(view.commands, 1, "7"); list_is(view.depends_on, 0, "");
        check(praktor_yaml_string_map_at(view.env, 0, &entry) == SALTS_OK && text_is(entry.key, "z") && text_is(entry.value, "last"));
        praktor_yaml_task_fields_destroy(owner);
        owner = fields("items_variable: jobs\ntemplate: {name: task, command: true}", PRAKTOR_YAML_DYNAMIC_TASKS);
        check(praktor_yaml_task_dynamic(owner, &view) == SALTS_OK && !view.sequence && !view.has_timeout && !view.has_when && text_is(view.command, "true"));
        praktor_yaml_task_fields_destroy(owner);
    }
    it("preserves dynamic required fields and decode-before-empty validation") {
        refuses("{}", PRAKTOR_YAML_DYNAMIC_TASKS, "requires 'items_variable'");
        refuses("items_variable: ''", PRAKTOR_YAML_DYNAMIC_TASKS, "'items_variable' cannot be empty");
        refuses("items_variable: jobs", PRAKTOR_YAML_DYNAMIC_TASKS, "requires 'template'");
        refuses("items_variable: jobs\ntemplate: []", PRAKTOR_YAML_DYNAMIC_TASKS, "'template' must be a map");
        refuses("items_variable: jobs\ntemplate: {}", PRAKTOR_YAML_DYNAMIC_TASKS, "requires 'name'");
        refuses("items_variable: jobs\ntemplate: {name: task}", PRAKTOR_YAML_DYNAMIC_TASKS, "requires 'command'");
        refuses("items_variable: jobs\ntemplate: {name: task, command: []}", PRAKTOR_YAML_DYNAMIC_TASKS, "'command' cannot be empty");
        refuses("items_variable: jobs\ntemplate: {name: task, command: ['', {}]}", PRAKTOR_YAML_DYNAMIC_TASKS, "Expected YAML scalar");
        refuses("items_variable: jobs\ntemplate: {name: task, command: ['', x]}", PRAKTOR_YAML_DYNAMIC_TASKS, "command entries cannot be empty");
        refuses("items_variable: jobs\ntemplate: {name: task, command: {}}", PRAKTOR_YAML_DYNAMIC_TASKS, "Expected YAML scalar");
    }
    it("keeps Uses plain scalar semantics and getter outputs on refusal") {
        praktor_yaml_task_fields *owner = fields("true", PRAKTOR_YAML_USES);
        vstr text = vstr_from_cstr("saved");
        const praktor_yaml_string_list *list = NULL;
        check(praktor_yaml_task_list(owner, &list) == SALTS_EINVAL && !list);
        check(praktor_yaml_task_uses(NULL, &text) == SALTS_EINVAL && text_is(text, "saved"));
        check(praktor_yaml_task_uses(owner, &text) == SALTS_OK && text_is(text, "true"));
        praktor_yaml_task_fields_destroy(owner);
        refuses("''", PRAKTOR_YAML_USES, "uses value cannot be empty");
        refuses("[]", PRAKTOR_YAML_USES, "Expected YAML scalar");
        owner = fields("[a]", PRAKTOR_YAML_STRING_LIST);
        check(praktor_yaml_task_list(owner, &list) == SALTS_OK);
        text = vstr_from_cstr("saved");
        check(praktor_yaml_string_list_at(list, 1, &text) == SALTS_ENOENT && text_is(text, "saved"));
        praktor_yaml_task_fields_destroy(owner);
    }
    it("admits long legacy scalars and exact aggregate bytes then refuses excess") {
        tstr source = tstr_new_len(NULL, PRAKTOR_YAML_TASK_MAX_BYTES);
        praktor_yaml_task_fields *owner;
        vstr text = {0};
        check(source); memset(source, 'x', PRAKTOR_YAML_TASK_MAX_BYTES);
        source[PRAKTOR_YAML_TASK_MAX_BYTES - 1u] = 0;
        owner = fields(source, PRAKTOR_YAML_USES);
        check(praktor_yaml_task_uses(owner, &text) == SALTS_OK && text.len == PRAKTOR_YAML_TASK_MAX_BYTES - 1u);
        praktor_yaml_task_fields_destroy(owner); source[PRAKTOR_YAML_TASK_MAX_BYTES - 1u] = 'x';
        refuses(source, PRAKTOR_YAML_USES, "retained text bytes"); tstr_free(source);
    }
    it("bounds lists maps and repeated decoded keys without truncation") {
        tstr list = tstr_dup("["), map = tstr_dup("{"), repeated = tstr_dup("matrix: {a: &base [");
        size_t index;
        praktor_yaml_task_fields *owner;
        for (index = 0; index < PRAKTOR_YAML_TASK_MAX_ENTRIES; ++index) {
            list = tstr_append_format(list, "{}a", index ? "," : "");
            map = tstr_append_format(map, "{}k{}: v", index ? "," : "", index);
        }
        {
            tstr exact_list = tstr_append_format(tstr_dup(list), "{}", "]");
            tstr exact_map = tstr_append_format(tstr_dup(map), "{}", "}");
            check(exact_list && exact_map);
            owner = fields(exact_list, PRAKTOR_YAML_STRING_LIST); praktor_yaml_task_fields_destroy(owner);
            owner = fields(exact_map, PRAKTOR_YAML_STRING_MAP); praktor_yaml_task_fields_destroy(owner);
            tstr_free(exact_list); tstr_free(exact_map);
        }
        list = tstr_append_format(list, "{}", ",a]"); map = tstr_append_format(map, "{}", ",extra: v}"); check(list && map);
        refuses(list, PRAKTOR_YAML_STRING_LIST, "list entry count"); refuses(map, PRAKTOR_YAML_STRING_MAP, "map entry count");
        /* Repeated aliases count expanded values without an enormous sibling
         * map's provider duplicate-key validation dominating sanitizer time. */
        for (index = 0; index < PRAKTOR_YAML_TASK_MAX_ENTRIES; ++index)
            repeated = tstr_append_format(repeated, "{}v", index ? "," : "");
        repeated = tstr_append_format(repeated, "{}", "], b: *base, c: *base, d: [");
        for (index = 0; index < PRAKTOR_YAML_TASK_MAX_ENTRIES - 4u; ++index)
            repeated = tstr_append_format(repeated, "{}v", index ? "," : "");
        {
            tstr exact = tstr_append_format(tstr_dup(repeated), "{}", "]}");
            check(exact);
            owner = fields(exact, PRAKTOR_YAML_EACH); praktor_yaml_task_fields_destroy(owner); tstr_free(exact);
        }
        repeated = tstr_append_format(repeated, "{}", ",v]}"); check(repeated);
        refuses(repeated, PRAKTOR_YAML_EACH, "expanded string count");
        tstr_free(repeated); tstr_free(map); tstr_free(list);
    }
    it("releases superseded matrix lists and map values from retained byte accounting") {
        tstr text = tstr_new_len(NULL, 65536), source = tstr_dup("{");
        size_t index;
        praktor_yaml_task_fields *owner;
        check(text && source); memset(text, 'x', 65536);
        for (index = 0; index < 24; ++index) source = tstr_append_format(source,
            "{}\"x\\0{}\": '{}'", index ? "," : "", index, text);
        source = tstr_append_format(source, "{}", "}"); check(source);
        owner = fields(source, PRAKTOR_YAML_STRING_MAP); praktor_yaml_task_fields_destroy(owner);
        tstr_free(source); source = tstr_dup("matrix: {");
        for (index = 0; index < 24; ++index) source = tstr_append_format(source,
            "{}\"x\\0{}\": ['{}']", index ? "," : "", index, text);
        source = tstr_append_format(source, "{}", "}"); check(source);
        owner = fields(source, PRAKTOR_YAML_EACH); praktor_yaml_task_fields_destroy(owner);
        tstr_free(source); tstr_free(text);
    }
    it("uses independent owners concurrently without document lifetime coupling") {
        cmeta_thread_t threads[4] = {0};
        bool passed[4] = {false};
        size_t index;
        for (index = 0; index < 4; ++index) check(cmeta_thread_create(&threads[index], independent, &passed[index]) == SALTS_OK);
        for (index = 0; index < 4; ++index) check(cmeta_thread_join(&threads[index]) == SALTS_OK && passed[index]);
    }
}
