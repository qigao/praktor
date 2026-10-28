/* Inject public module-boundary failures; use real YAML decoding and CSTL
 * storage. These tests do not replace or fault the providers' internals. */
#include "yml/yaml_task_fields.h"
#include <cstl/vec.h>
#include <cstl/map.h>
#include <fmt.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

static unsigned failure_point, operation_count;
static praktor_yaml_failure failed_origin;
static void *owners[32];
static size_t owner_count;
static bool injected(praktor_yaml_failure origin) {
    if (!failure_point || ++operation_count != failure_point) return false;
    failed_origin = origin; return true;
}
static void *fault_owner(size_t count, size_t size) {
    void *result = injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : calloc(count, size);
    if (result && owner_count < 32) owners[owner_count++] = result;
    return result;
}
static void observed_free(void *pointer) {
    size_t index;
    for (index = 0; index < owner_count; ++index) if (owners[index] == pointer) {
        owners[index] = owners[--owner_count]; break;
    }
    free(pointer);
}
static char *fault_decode(const cyaml_doc_t *doc, const cyaml_node_t *node) {
    return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : cyaml_scalar_str(doc, node);
}
static tstr fault_copy(const char *text) { return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : tstr_dup(text); }
static tstr fault_format(const char *pattern, const fmt_arg_t *args, size_t count) {
    return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : tstr_format_typed_impl(pattern, args, count);
}
static stl_status fault_push(vec_t *list, const void *text) {
    return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? STL_OUT_OF_MEMORY : vec_push(list, text);
}
static stl_status fault_map_init(map_t *map, size_t ks, size_t ka, size_t vs, size_t va, size_t limit,
    int (*compare)(const void *, const void *, void *), void *context) {
    return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? STL_OUT_OF_MEMORY :
        map_init_bytes(map, ks, ka, vs, va, limit, compare, context);
}
static stl_status fault_map_put(map_t *map, const void *key, const void *value) {
    return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? STL_OUT_OF_MEMORY : map_put(map, key, value);
}
#define map_init_bytes fault_map_init
#define map_put fault_map_put
#define calloc fault_owner
#define free observed_free
#define cyaml_scalar_str fault_decode
#define tstr_dup fault_copy
#define vec_push fault_push
#undef tstr_format
#define tstr_format(pattern, ...) fault_format(pattern, FMT_ARGS(__VA_ARGS__), (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#include "../src/yml/yaml_document.c"
#include "../src/yml/yaml_fields.c"
#include "../src/yml/yaml_task_fields.c"
#undef calloc
#undef free
#undef cyaml_scalar_str
#undef tstr_dup
#undef vec_push
#undef tstr_format

#undef map_init_bytes
#undef map_put
static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *result = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &result, NULL) == SALTS_OK);
    return result;
}
static void check_faults(const char *source, praktor_yaml_task_field_kind kind, int expected) {
    unsigned point;
    praktor_yaml_document *input = document(source), *saved = document("retained");
    for (point = 1; point < 500; ++point) {
        praktor_yaml_task_fields *previous = NULL, *result;
        praktor_yaml_field_error error = {0};
        vstr text = {0};
        int status;
        check(praktor_yaml_task_fields_create(praktor_yaml_root(saved), PRAKTOR_YAML_USES, &previous, NULL) == SALTS_OK);
        result = previous; operation_count = 0; failed_origin = PRAKTOR_YAML_FAILURE_NONE; failure_point = point;
        status = praktor_yaml_task_fields_create(praktor_yaml_root(input), kind, &result, &error);
        failure_point = 0;
        info("kind %d, point %u, operations %u, origin %d, status %d", (int)kind, point, operation_count, (int)failed_origin, status);
        if (failed_origin != PRAKTOR_YAML_FAILURE_NONE) check(status == SALTS_ENOMEM && result == previous && error.origin == failed_origin);
        else {
            check(status == expected);
            if (status == SALTS_OK) praktor_yaml_task_fields_destroy(result);
            else check(result == previous && error.origin == PRAKTOR_YAML_FAILURE_INPUT && error.text);
        }
        check(praktor_yaml_task_uses(previous, &text) == SALTS_OK && vstr_eq(text, vstr_from_cstr("retained")));
        praktor_yaml_task_fields_destroy(previous); praktor_yaml_field_error_dispose(&error);
        check(owner_count == 0);
        if (failed_origin == PRAKTOR_YAML_FAILURE_NONE) break;
    }
    check(point > 1 && point < 500);
    praktor_yaml_document_destroy(input); praktor_yaml_document_destroy(saved);
}
spec("C YAML task field boundary failure rollback") {
    it("rolls back list decoding and vector storage") {
        check_faults("[a, '', true]", PRAKTOR_YAML_STRING_LIST, SALTS_OK);
        check_faults("[a, {}]", PRAKTOR_YAML_STRING_LIST, SALTS_EINVAL);
    }
    it("rolls back map index creation insertion and duplicate replacement") {
        check_faults("{z: a, x: b, \"z\\0last\": last}", PRAKTOR_YAML_STRING_MAP, SALTS_OK);
        check_faults("{z: a, x: []}", PRAKTOR_YAML_STRING_MAP, SALTS_EINVAL);
    }
    it("releases nested matrix lists across duplicate dimensions and validation") {
        check_faults("{matrix: {z: [a, b], x: c, \"z\\0last\": [last]}, as: value, index_variable: n}", PRAKTOR_YAML_EACH, SALTS_OK);
        check_faults("{matrix: {z: [a], x: []}}", PRAKTOR_YAML_EACH, SALTS_EINVAL);
        check_faults("{items: [a], matrix: {x: b}}", PRAKTOR_YAML_EACH, SALTS_EINVAL);
    }
    it("releases all three trigger groups on success and late refusal") {
        check_faults("{on_success: [a, b], on_failure: [retry], on_complete: [end]}", PRAKTOR_YAML_TRIGGERS, SALTS_OK);
        check_faults("{on_success: [a], on_complete: ['']}", PRAKTOR_YAML_TRIGGERS, SALTS_EINVAL);
    }
    it("rolls back dynamic task templates and final environment maps") {
        check_faults("items_variable: jobs\ntemplate: {name: task, command: [echo, true], timeout: '', when: yes, depends_on: [ready], env: {z: a, x: b, \"z\\0last\": last}}", PRAKTOR_YAML_DYNAMIC_TASKS, SALTS_OK);
        check_faults("items_variable: jobs\ntemplate: {name: task, command: echo, env: {z: []}}", PRAKTOR_YAML_DYNAMIC_TASKS, SALTS_EINVAL);
    }
    it("preserves Uses and early unknown-key refusal categories") {
        check_faults("path", PRAKTOR_YAML_USES, SALTS_OK);
        check_faults("''", PRAKTOR_YAML_USES, SALTS_EINVAL);
        check_faults("unknown: value", PRAKTOR_YAML_EACH, SALTS_EINVAL);
    }
}
