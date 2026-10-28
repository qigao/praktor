/* Fault each production module boundary while retaining real YAML/JSON trees. */
#include "yml/yaml_value.h"
#include <fmt.h>
#include <string.h>
#include <tinytest.h>

static unsigned failure_point, operation;
static praktor_yaml_failure failed_origin;
static const cyaml_node_t *decoded[2];
static unsigned recorded;
static bool record_decodes;
static bool injected(praktor_yaml_failure origin) {
    if (!failure_point || ++operation != failure_point) return false;
    failed_origin = origin;
    return true;
}
static char *fault_decode(const cyaml_doc_t *doc, const cyaml_node_t *node) {
    if (record_decodes && recorded < 2) decoded[recorded++] = node;
    return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : cyaml_scalar_str(doc, node);
}
static tstr fault_copy(const char *text) { return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : tstr_dup(text); }
static tstr fault_format(const char *pattern, const fmt_arg_t *args, size_t count) {
    return injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : tstr_format_typed_impl(pattern, args, count);
}
static json_value_t *fault_object(void) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_object(); }
static json_value_t *fault_array(void) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_array(); }
static json_value_t *fault_string(const char *text, size_t length) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_string_n(text, length); }
static json_value_t *fault_number(double number) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_number(number); }
static json_value_t *fault_integer(int64_t number) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_int64(number); }
static json_value_t *fault_boolean(bool value) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_bool(value); }
static json_value_t *fault_null(void) { return injected(PRAKTOR_YAML_FAILURE_PROVIDER) ? NULL : json_create_null(); }
static bool fault_member(json_value_t *object, const char *key, size_t length, json_value_t *value) {
    return !injected(PRAKTOR_YAML_FAILURE_PROVIDER) && json_object_add_n(object, key, length, value);
}
static bool fault_item(json_value_t *array, json_value_t *value) {
    return !injected(PRAKTOR_YAML_FAILURE_PROVIDER) && json_array_add_checked(array, value);
}
#define cyaml_scalar_str fault_decode
#define tstr_dup fault_copy
#undef tstr_format
#define tstr_format(pattern, ...) fault_format(pattern, FMT_ARGS(__VA_ARGS__), (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#define json_create_object fault_object
#define json_create_array fault_array
#define json_create_string_n fault_string
#define json_create_number fault_number
#define json_create_int64 fault_integer
#define json_create_bool fault_boolean
#define json_create_null fault_null
#define json_object_add_n fault_member
#define json_array_add_checked fault_item
#include "../src/yml/yaml_document.c"
#include "../src/yml/yaml_fields.c"
#include "../src/yml/yaml_value.c"
#undef cyaml_scalar_str
#undef tstr_dup
#undef tstr_format
#undef json_create_object
#undef json_create_array
#undef json_create_string_n
#undef json_create_number
#undef json_create_int64
#undef json_create_bool
#undef json_create_null
#undef json_object_add_n
#undef json_array_add_checked

static unsigned check_faults(const char *source, bool keys, int expected) {
    unsigned point;
    for (point = 1; point < 200; ++point) {
        praktor_yaml_document *owner = NULL;
        praktor_yaml_field_error error = {0};
        json_value_t *previous = json_create_int64(71), *result = previous;
        const vstr allowed = {"good", 4};
        int status;
        check(previous && praktor_yaml_document_create(vstr_from_cstr(source), &owner, NULL) == SALTS_OK);
        operation = 0; failed_origin = PRAKTOR_YAML_FAILURE_NONE; failure_point = point;
        status = keys ? praktor_yaml_check_keys(praktor_yaml_root(owner), &allowed, 1, &error) :
            praktor_yaml_value(praktor_yaml_at(praktor_yaml_root(owner), 0), &result, &error);
        failure_point = 0;
        info("point %u, operations %u, origin %d, status %d", point, operation, (int)failed_origin, status);
        if (failed_origin != PRAKTOR_YAML_FAILURE_NONE) {
            check(status == SALTS_ENOMEM && result == previous && error.origin == failed_origin);
        } else {
            check(status == expected);
            if (status == SALTS_OK && !keys) json_free(result);
            else check(result == previous);
        }
        check(json_number(previous) > 70 && json_number(previous) < 72);
        json_free(previous); praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(owner);
        if (failed_origin == PRAKTOR_YAML_FAILURE_NONE) break;
    }
    check(point < 200);
    return point;
}

spec("C YAML structured conversion rollback and failure categories") {
    it("converts mapping values before decoding their keys, retaining RHS error precedence") {
        praktor_yaml_document *owner = NULL;
        json_value_t *result = NULL;
        praktor_yaml_node child;
        check(praktor_yaml_document_create(vstr_from_cstr("key: 7\n"), &owner, NULL) == SALTS_OK);
        child = praktor_yaml_at(praktor_yaml_root(owner), 0);
        recorded = 0; record_decodes = true;
        check(praktor_yaml_value(praktor_yaml_root(owner), &result, NULL) == SALTS_OK);
        record_decodes = false;
        check(recorded == 2 && decoded[0] == child.node && decoded[1] == child.key_node);
        json_free(result); praktor_yaml_document_destroy(owner);
    }
    it("checks every nested value, key decode, factory and checked adoption boundary") {
        check(check_faults("value: {a: [1, 'text', true, 2.5], b: {}, c: []}\n", false, SALTS_OK) == 30);
    }
    it("discards provisional owners when a later contract null is refused") {
        check(check_faults("value: {a: [1, 2], bad: null}\n", false, SALTS_EINVAL) > 10);
    }
    it("checks unknown-key message and location allocation without changing the document") {
        check(check_faults("good: 1\nunknown: 2\n", true, SALTS_EINVAL) == 8);
    }
    it("checks successful whitelist decoding and owned text cleanup") {
        check(check_faults("good: 1\n", true, SALTS_OK) == 3);
    }
    it("releases bounded recursive alias expansion through every factory and limit diagnostic failure") {
        check(check_faults("value: &loop [*loop]\n", false, SALTS_EINVAL) > 100);
    }
}
