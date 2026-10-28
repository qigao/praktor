/* Fail each allocation/adoption boundary of the actual production algorithm.
 * Wrappers call the real public JSON provider; no replacement JSON tree. */
#include "data/workflow_value.h"
#include <string.h>
#include <tinytest.h>

static unsigned fail_at, operation;
static bool injected(void) { return fail_at && ++operation == fail_at; }
static json_value_t *fault_object(void) { return injected() ? NULL : json_create_object(); }
static json_value_t *fault_array(void) { return injected() ? NULL : json_create_array(); }
static json_value_t *fault_clone(const json_value_t *v) { return injected() ? NULL : json_clone(v); }
static bool fault_add(json_value_t *o, const char *k, size_t n, json_value_t *v) {
    return !injected() && json_object_add_n(o, k, n, v);
}
static bool fault_push(json_value_t *a, json_value_t *v) {
    return !injected() && json_array_add_checked(a, v);
}
#define json_create_object fault_object
#define json_create_array fault_array
#define json_clone fault_clone
#define json_object_add_n fault_add
#define json_array_add_checked fault_push
#include "../src/data/workflow_value.c"
#undef json_create_object
#undef json_create_array
#undef json_clone
#undef json_object_add_n
#undef json_array_add_checked

static void exercise(const char *initial, unsigned count, bool take, unsigned action) {
    unsigned point;
    vstr keys[] = {vstr_from_cstr("a"), vstr_from_cstr("b"), vstr_from_cstr("c")};
    for (point = 1; point < 32; ++point) {
        json_value_t *root = json_parse(initial, strlen(initial));
        json_value_t *source = json_parse("{\"v\":7}", 7), *saved_root = root, *saved_source = source;
        char *before = json_serialize(root, NULL), *source_before = json_serialize(source, NULL);
        char error[128];
        int status;
        check(root && source && before && source_before);
        operation = 0; fail_at = point;
        if (action == 2) status = praktor_value_erase(&root, 1, error, sizeof(error));
        else if (action == 1) status = take ?
            praktor_value_append_take(&root, keys, count, &source, error, sizeof(error)) :
            praktor_value_append_copy(&root, keys, count, source, error, sizeof(error));
        else status = take ?
            praktor_value_set_take(&root, keys, count, &source, error, sizeof(error)) :
            praktor_value_set_copy(&root, keys, count, source, error, sizeof(error));
        fail_at = 0;
        info("failpoint %u / action %u / depth %u / take %d: %d %s", point, action, count, take, status, error);
        if (status != SALTS_OK) {
            char *after = json_serialize(root, NULL), *source_after = json_serialize(source, NULL);
            check(status == SALTS_ENOMEM);
            check(root == saved_root && source == saved_source);
            check(after && source_after && strcmp(before, after) == 0 && strcmp(source_before, source_after) == 0);
            json_serialize_free(after); json_serialize_free(source_after);
        } else {
            check(error[0] == '\0');
            if (take && action != 2) check(source == NULL);
            else check(source == saved_source);
        }
        json_serialize_free(before); json_serialize_free(source_before);
        json_free(root); json_free(source);
        if (status == SALTS_OK) break;
    }
    check(point < 32);
}

spec("C workflow value rollback at every provider boundary") {
    it("rolls back null-root promotion and missing suffix construction") {
        exercise("null", 3, false, 0);
        exercise("null", 3, true, 0);
        exercise("{\"keep\":1}", 3, false, 0);
        exercise("{\"keep\":1}", 3, true, 0);
        exercise("{\"a\":{\"keep\":1}}", 3, true, 0);
    }
    it("rolls back replacement and direct copy or take adoption") {
        exercise("{\"a\":1}", 1, false, 0);
        exercise("{\"a\":1}", 1, true, 0);
        exercise("{\"a\":1}", 0, false, 0);
        exercise("{\"a\":1}", 0, true, 0);
    }
    it("rolls back array appends and complete erase reconstruction") {
        exercise("[1,2,3]", 0, false, 1);
        exercise("[1,2,3]", 0, true, 1);
        exercise("{\"a\":{\"b\":{\"c\":[1,2,3]}}}", 3, true, 1);
        exercise("[1,2,3]", 0, false, 2);
    }
    it("retains the self owner after failed self insertion") {
        unsigned point;
        for (point = 1; point < 8; ++point) {
            json_value_t *root = json_parse("[1,2]", 5), *saved = root;
            int status;
            operation = 0; fail_at = point;
            status = praktor_value_append_take(&root, NULL, 0, &root, NULL, 0);
            fail_at = 0;
            check(root == saved);
            check(json_array_size(root) == (status == SALTS_OK ? 3u : 2u));
            json_free(root);
            if (status == SALTS_OK) break;
            check(status == SALTS_ENOMEM);
        }
        check(point < 8);
    }
    it("rolls back self snapshots while building missing parent objects") {
        const char *initial[] = {"null", "{\"keep\":7}"};
        vstr keys[] = {vstr_from_cstr("a"), vstr_from_cstr("b"), vstr_from_cstr("c")};
        size_t index;
        for (index = 0; index < sizeof(initial) / sizeof(initial[0]); ++index) {
            unsigned point;
            for (point = 1; point < 64; ++point) {
                json_value_t *root = json_parse(initial[index], strlen(initial[index])), *saved = root;
                int status;
                operation = 0; fail_at = point;
                status = praktor_value_set_take(&root, keys, 3, &root, NULL, 0);
                fail_at = 0;
                if (status != SALTS_OK) {
                    char *text = json_serialize(root, NULL);
                    check(status == SALTS_ENOMEM && root == saved);
                    check(text && strcmp(text, initial[index]) == 0);
                    json_serialize_free(text);
                }
                json_free(root);
                if (status == SALTS_OK) break;
            }
            check(point < 64);
        }
    }
}
