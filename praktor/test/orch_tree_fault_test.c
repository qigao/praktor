/* Fault public constructor boundaries, not CSTL/tstr allocator internals. */
#include "yml/orch_tree.h"
#include <cstl/vec.h>
#include <fmt.h>
#include <salts/error_codes.h>
#include <stdlib.h>
#include <string.h>
#ifndef PRAKTOR_ORCH_FAULT_PROVIDER
#include <tinytest.h>
#endif
#include <tstr.h>

static unsigned failure_point, operations;
static size_t live_owners, live_strings;
static bool injected;
static bool inject(void) {
    if (!failure_point || ++operations != failure_point) return false;
    injected = true; return true;
}
void praktor_orch_test_fail(unsigned point) { failure_point = point; operations = 0; injected = false; }
size_t praktor_orch_test_live(void) { return live_owners + live_strings; }
static void *fault_calloc(size_t count, size_t size) {
    void *result = inject() ? NULL : calloc(count, size);
    if (result) ++live_owners;
    return result;
}
static void fault_free(void *pointer) { if (pointer) --live_owners; free(pointer); }
static tstr fault_copy(vstr source) {
    tstr result = inject() ? NULL : tstr_from_v(source);
    if (result) ++live_strings;
    return result;
}
static tstr fault_format(const char *pattern, const fmt_arg_t *args, size_t count) {
    tstr result = inject() ? NULL : tstr_format_typed_impl(pattern, args, count);
    if (result) ++live_strings;
    return result;
}
static void fault_string_free(tstr text) { if (text) --live_strings; tstr_free(text); }
static stl_status fault_push(vec_t *vector, const void *value) { return inject() ? STL_OUT_OF_MEMORY : vec_push(vector, value); }
#define calloc fault_calloc
#define free fault_free
#define tstr_from_v fault_copy
#define tstr_free fault_string_free
#define vec_push fault_push
#undef tstr_format
#define tstr_format(pattern, ...) fault_format(pattern, FMT_ARGS(__VA_ARGS__), (size_t)FMT_ARG_COUNT(__VA_ARGS__))
#include "../src/yml/orch_tree.c"
#undef calloc
#undef free
#undef tstr_from_v
#undef tstr_free
#undef vec_push
#undef tstr_format

#ifndef PRAKTOR_ORCH_FAULT_PROVIDER
static void walk_faults(praktor_orch_command input, int expected) {
    praktor_orch_command saved = {0};
    unsigned point;
    saved.command = vstr_from_cstr("retained");
    for (point = 1; point < 200; ++point) {
        praktor_orch_tree *previous = NULL, *out;
        const praktor_orch_node *shell = NULL;
        praktor_orch_param param = {0};
        char error[256] = {0};
        int status;
        bool failed;
        check(praktor_orch_from_command(&saved, &previous, NULL, 0) == SALTS_OK); out = previous;
        praktor_orch_test_fail(point);
        status = praktor_orch_from_command(&input, &out, error, sizeof error); failed = injected;
        praktor_orch_test_fail(0);
        info("point %u, failed %d, status %d: %s", point, failed, status, error);
        if (failed) check(status == SALTS_ENOMEM && out == previous && error[0]);
        else {
            check(status == expected);
            if (status == SALTS_OK) praktor_orch_tree_destroy(out);
            else check(out == previous && error[0]);
        }
        check(praktor_orch_child_at(praktor_orch_tree_root(previous), 0, &shell) == SALTS_OK &&
            praktor_orch_param_at(shell, 0, &param) == SALTS_OK && vstr_eq(param.value, saved.command));
        praktor_orch_tree_destroy(previous); check(praktor_orch_test_live() == 0);
        if (!failed) break;
    }
    check(point > 1 && point < 200);
}
spec("Native orchestration construction rollback") {
    it("releases scalar and sequence nodes strings and both vectors at every boundary") {
        praktor_orch_command input = {0}; vstr commands[] = {{"one", 3}, {"", 0}, {"last", 4}};
        input.command = vstr_from_cstr("echo"); walk_faults(input, SALTS_OK);
        input.sequence = true; input.commands = commands; input.command_count = 3; walk_faults(input, SALTS_OK);
    }
    it("releases regex capture formatting and every other postprocessor") {
        praktor_orch_command input = {0};
        input.regex.present = true; input.regex.pattern = vstr_from_cstr("x"); input.regex.capture_group = -7; input.regex.output_key = vstr_from_cstr("match"); walk_faults(input, SALTS_OK);
        input.regex.present = false; input.json.present = true; input.json.path = vstr_from_cstr("$"); input.json.output_key = vstr_from_cstr("data"); walk_faults(input, SALTS_OK);
        input.json.present = false; input.lines.present = true; input.lines.filter = vstr_from_cstr("x"); input.lines.output_key = vstr_from_cstr("lines"); walk_faults(input, SALTS_OK);
        input.lines.present = false; input.keyvalue.present = true; input.keyvalue.delimiter = vstr_from_cstr("="); input.keyvalue.line_separator = vstr_from_cstr("\n"); input.keyvalue.output_key = vstr_from_cstr("vars"); walk_faults(input, SALTS_OK);
    }
    it("cleans JSON fallback and late invalid active postprocessor values") {
        praktor_orch_command input = {0};
        input.output_format = PRAKTOR_YAML_OUTPUT_JSON; walk_faults(input, SALTS_OK);
        input.regex.present = true; input.regex.pattern = (vstr){NULL, 1}; walk_faults(input, SALTS_EINVAL);
        input.regex.present = false; input.keyvalue.present = true; input.keyvalue.line_separator = (vstr){NULL, 1}; walk_faults(input, SALTS_EINVAL);
        {
            tstr text = tstr_new_len(NULL, PRAKTOR_ORCH_MAX_BYTES / 2u);
            check(text); memset(text, 'x', tstr_len(text));
            input.keyvalue.present = false; input.regex.present = true;
            input.command = tstr_to_v(text); input.regex.pattern = tstr_to_v(text);
            walk_faults(input, SALTS_EINVAL); tstr_free(text);
        }
    }
}
#endif
