/* Inject public module-boundary failures; use real YAML decoding and CSTL
 * storage. These tests do not replace or fault the providers' internals. */
#include "yml/yaml_action_params.h"
#include <cstl/vec.h>
#include <fmt.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

static unsigned failure_point, operation_count;
static praktor_yaml_failure failed_origin;
static void *owners[4];
static size_t owner_count;
static bool injected(praktor_yaml_failure origin) {
    if (!failure_point || ++operation_count != failure_point) return false;
    failed_origin = origin; return true;
}
static void *fault_owner(size_t count, size_t size) {
    void *result = injected(PRAKTOR_YAML_FAILURE_ALLOCATION) ? NULL : calloc(count, size);
    if (result && owner_count < 4) owners[owner_count++] = result;
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
#define calloc fault_owner
#define free observed_free
#define cyaml_scalar_str fault_decode
#define tstr_dup fault_copy
#define vec_push fault_push
#undef tstr_format
#define tstr_format(pattern, ...) fault_format(pattern, FMT_ARGS(__VA_ARGS__), (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#include "../src/yml/yaml_document.c"
#include "../src/yml/yaml_fields.c"
#include "../src/yml/yaml_action_params.c"
#undef calloc
#undef free
#undef cyaml_scalar_str
#undef tstr_dup
#undef vec_push
#undef tstr_format

static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *result = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &result, NULL) == SALTS_OK);
    return result;
}
static void check_faults(const char *source, praktor_yaml_action_kind kind, int expected) {
    unsigned point;
    praktor_yaml_document *input = document(source), *saved = document("service: {name: retained}\n");
    for (point = 1; point < 400; ++point) {
        praktor_yaml_action_params *previous = NULL, *result;
        praktor_yaml_field_error error = {0};
        praktor_yaml_service_view view;
        int status;
        check(praktor_yaml_action_params_create(praktor_yaml_root(saved), PRAKTOR_YAML_ACTION_SERVICE, &previous, NULL) == SALTS_OK);
        result = previous;
        operation_count = 0; failed_origin = PRAKTOR_YAML_FAILURE_NONE; failure_point = point;
        status = praktor_yaml_action_params_create(praktor_yaml_root(input), kind, &result, &error);
        failure_point = 0;
        info("kind %d, point %u, operations %u, origin %d, status %d", (int)kind, point, operation_count, (int)failed_origin, status);
        if (failed_origin != PRAKTOR_YAML_FAILURE_NONE) check(status == SALTS_ENOMEM && result == previous && error.origin == failed_origin);
        else {
            check(status == expected);
            if (status == SALTS_OK) praktor_yaml_action_params_destroy(result);
            else check(result == previous && error.origin == PRAKTOR_YAML_FAILURE_INPUT && error.text);
        }
        check(praktor_yaml_action_service(previous, &view) == SALTS_OK && vstr_eq(view.name, vstr_from_cstr("retained")));
        praktor_yaml_action_params_destroy(previous); praktor_yaml_field_error_dispose(&error);
        check(owner_count == 0);
        if (failed_origin == PRAKTOR_YAML_FAILURE_NONE) break;
    }
    check(point > 1 && point < 400);
    praktor_yaml_document_destroy(input); praktor_yaml_document_destroy(saved);
}

spec("C YAML Action parameter construction rollback") {
    it("preserves owners through download lookup, decode, boolean and timeout failures") {
        check_faults("download: {url: x, path: y, sha256: z, overwrite: true, timeout_ms: 7}\n", PRAKTOR_YAML_ACTION_DOWNLOAD, SALTS_OK);
    }
    it("preserves owners through service operation, CSTL append and integer failures") {
        check_faults("service: {name: worker, operation: start, profile: windows_scm, arguments: ['a', '', 'b'], timeout_ms: 7, poll_interval_ms: 2}\n", PRAKTOR_YAML_ACTION_SERVICE, SALTS_OK);
    }
    it("preserves owners through complete managed-process construction") {
        check_faults("managed_process: {identity: {image_name: worker.exe}, operation: restart, executable: worker.exe, arguments: ['a', 'b'], working_directory: '.', startup_timeout_ms: 7, stop_timeout_ms: 8, force_terminate: '0x1'}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS, SALTS_OK);
    }
    it("cleans early and late refusal diagnostics and partial fields") {
        check_faults("download: {url: '', path: x}\n", PRAKTOR_YAML_ACTION_DOWNLOAD, SALTS_EINVAL);
        check_faults("service: {name: worker, operation: START}\n", PRAKTOR_YAML_ACTION_SERVICE, SALTS_EINVAL);
        check_faults("managed_process: {identity: {image_name: worker.exe}, arguments: ['a', 'b'], stop_timeout_ms: +1}\n", PRAKTOR_YAML_ACTION_MANAGED_PROCESS, SALTS_ERANGE);
    }
    it("rolls back Program arguments, stdin and format conversion") {
        check_faults("program: tool\nargs: [a, '', true]\nstdin: input\noutput_format: JSON\n", PRAKTOR_YAML_ACTION_PROGRAM, SALTS_OK);
        check_faults("program: tool\nargs: ''\nstdin: []\n", PRAKTOR_YAML_ACTION_PROGRAM, SALTS_EINVAL);
    }
    it("rolls back all Command parser projections and scalar/list forms") {
        check_faults("command: [echo, 7]\noutput_format: JSON\nparse_regex: {pattern: x, capture_group: -1, output_key: result}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_OK);
        check_faults("command: echo\nparse_json: {path: '$.x', output_key: result}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_OK);
        check_faults("command: echo\nparse_lines: {filter: x, output_key: result}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_OK);
        check_faults("command: echo\nparse_keyvalue: {delimiter: ':', line_separator: ';', output_key: result}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_OK);
    }
    it("cleans Command conflicts, unsupported format and late required-field refusals") {
        check_faults("command: echo\nparse_json: {}\nparse_regex: {}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_EINVAL);
        check_faults("command: echo\noutput_format: ' JSON '\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_EINVAL);
        check_faults("command: echo\nparse_regex: {pattern: x, capture_group: +1}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_ERANGE);
        check_faults("command: echo\nparse_keyvalue: {delimiter: ':', line_separator: ';'}\n", PRAKTOR_YAML_ACTION_COMMAND, SALTS_EINVAL);
    }

}
