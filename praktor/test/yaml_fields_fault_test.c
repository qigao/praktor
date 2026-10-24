/* Single-point failures in the real document/field implementation boundaries;
 * provider parsing/decoding and allocation remain real, never reimplemented. */
#include "yml/yaml_fields.h"
#include <fmt.h>
#include <string.h>
#include <tinytest.h>

static unsigned failure_point, operation;
static bool injected(void) { return failure_point && ++operation == failure_point; }
static char *fault_decode(const cyaml_doc_t *document, const cyaml_node_t *node) {
    return injected() ? NULL : cyaml_scalar_str(document, node);
}
static tstr fault_copy(const char *text) { return injected() ? NULL : tstr_dup(text); }
static tstr fault_format(const char *pattern, const fmt_arg_t *args, size_t count) {
    return injected() ? NULL : tstr_format_typed_impl(pattern, args, count);
}
#define cyaml_scalar_str fault_decode
#define tstr_dup fault_copy
#undef tstr_format
#define tstr_format(pattern, ...) fault_format(pattern, FMT_ARGS(__VA_ARGS__), (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#include "../src/yml/yaml_document.c"
#include "../src/yml/yaml_fields.c"
#undef cyaml_scalar_str
#undef tstr_dup
#undef tstr_format

static unsigned check_faults(const char *source, unsigned action, unsigned provider_point) {
    unsigned point;
    for (point = 1; point < 12; ++point) {
        praktor_yaml_document *owner = NULL;
        praktor_yaml_field_error error = {0};
        tstr previous = tstr_dup("retained"), text = previous;
        bool flag = true;
        int status;
        check(previous && praktor_yaml_document_create(vstr_from_cstr(source), &owner, NULL) == SALTS_OK);
        operation = 0; failure_point = point;
        status = action ? praktor_yaml_field_boolean(praktor_yaml_at(praktor_yaml_root(owner), 0),
            vstr_from_cstr("flag"), PRAKTOR_YAML_BOOLEAN_STRICT, &flag, &error) :
            praktor_yaml_field_scalar(praktor_yaml_at(praktor_yaml_root(owner), 0), vstr_from_cstr("scalar"), &text, &error);
        failure_point = 0;
        info("action %u, point %u, operations %u, status %d", action, point, operation, status);
        if (operation >= point) {
            check(status == SALTS_ENOMEM && text == previous && flag);
            check(error.origin == (point == provider_point ? PRAKTOR_YAML_FAILURE_PROVIDER : PRAKTOR_YAML_FAILURE_ALLOCATION));
        } else if (status == SALTS_OK) {
            check(error.origin == PRAKTOR_YAML_FAILURE_NONE && !error.text);
            if (!action) { check(strcmp(text, "valid") == 0); tstr_free(text); }
            else check(!flag);
        } else check(status == SALTS_EINVAL && text == previous && flag && error.text && error.origin == PRAKTOR_YAML_FAILURE_INPUT);
        tstr_free(previous);
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(owner);
        if (operation < point) break;
    }
    return point;
}

spec("C YAML fields preserve outputs through allocation and provider failure") {
    it("checks scalar provider decoding and owned copies") {
        check(check_faults("value: valid\n", 0, 1) == 3);
    }
    it("checks boolean message, provider decoding and owned copies") {
        check(check_faults("value: false\n", 1, 2) == 4);
    }
    it("checks shape diagnostics and key location allocation") {
        check(check_faults("value: []\n", 0, 0) == 3);
        check(check_faults("value: []\n", 1, 0) == 4);
    }
    it("checks rejected scalar cleanup and complete boolean diagnostics") {
        check(check_faults("value: maybe\n", 1, 2) == 6);
    }
    it("checks overlength scalar cleanup and error construction") {
        tstr source = tstr_new_len(NULL, PRAKTOR_YAML_MAX_PARAMETER_LENGTH + 1);
        tstr yaml;
        check(source);
        memset(source, 'x', tstr_len(source));
        yaml = tstr_format_typed_impl("value: {}\n", (fmt_arg_t[]){fmt_arg_str(source)}, 1);
        check(yaml && check_faults(yaml, 0, 1) == 5);
        tstr_free(yaml); tstr_free(source);
    }
}
