#include "data/workflow_value.h"
#include <math.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static json_value_t *parse(const char *text) {
    json_value_t *value = json_parse(text, strlen(text));
    check(value != NULL);
    return value;
}

static void expect_json(const json_value_t *value, const char *expected) {
    char *text = json_serialize(value, NULL);
    check(text != NULL);
    info("value: %s", text);
    check(strcmp(text, expected) == 0);
    json_serialize_free(text);
}

static void independent_owner(void *argument) {
    bool *passed = (bool *)argument;
    unsigned iteration;
    vstr keys[] = {vstr_from_cstr("state"), vstr_from_cstr("values")};
    *passed = true;
    for (iteration = 0; iteration < 100 && *passed; ++iteration) {
        json_value_t *root = json_create_null(), *array = json_create_array();
        json_value_t *number = json_create_uint64(iteration);
        const json_value_t *found = NULL;
        uint64_t result = UINT64_MAX;
        *passed = root && array && number &&
            praktor_value_set_take(&root, keys, 2, &array, NULL, 0) == SALTS_OK && !array &&
            praktor_value_append_take(&root, keys, 2, &number, NULL, 0) == SALTS_OK && !number &&
            praktor_value_find(root, keys, 2, &found) == SALTS_OK &&
            praktor_value_as_unsigned(json_array_get(found, 0), 64, &result, NULL, 0) == SALTS_OK &&
            result == iteration;
        json_free(number); json_free(array); json_free(root);
    }
}

spec("C workflow value ownership and conversion") {
    it("preserves exact signed and unsigned integer lexemes") {
        json_value_t *value = parse("18446744073709551615");
        int64_t signed_value = 71;
        uint64_t unsigned_value = 72;
        char error[128];
        check(praktor_value_is_uint64(value));
        check(!praktor_value_is_int64(value));
        check(praktor_value_as_unsigned(value, 64, &unsigned_value, error, sizeof(error)) == SALTS_OK);
        check(unsigned_value == UINT64_MAX && error[0] == '\0');
        check(praktor_value_as_signed(value, 64, &signed_value, error, sizeof(error)) == SALTS_ERANGE);
        check(signed_value == 71);
        json_free(value);
        value = parse("-9223372036854775808");
        check(praktor_value_is_int64(value) && !praktor_value_is_uint64(value));
        check(praktor_value_as_signed(value, 64, &signed_value, error, sizeof(error)) == SALTS_OK);
        check(signed_value == INT64_MIN);
        check(praktor_value_as_unsigned(value, 64, &unsigned_value, error, sizeof(error)) == SALTS_ERANGE);
        check(unsigned_value == UINT64_MAX);
        json_free(value);
    }
    it("checks every integer width without overflowing or rounding maxima") {
        unsigned bits;
        for (bits = 1; bits <= 64; ++bits) {
            uint64_t bound = UINT64_C(1) << (bits - 1), u = 0;
            int64_t s = 0;
            json_value_t *value = json_create_uint64(bound - 1);
            check(praktor_value_as_signed(value, bits, &s, NULL, 0) == SALTS_OK);
            check((uint64_t)s == bound - 1);
            json_free(value);
            value = json_create_uint64(bound);
            check(praktor_value_as_signed(value, bits, &s, NULL, 0) == SALTS_ERANGE);
            check(praktor_value_as_unsigned(value, bits, &u, NULL, 0) == SALTS_OK);
            check(u == bound);
            json_free(value);
            value = json_create_number(ldexp(1.0, (int)bits));
            check(praktor_value_as_unsigned(value, bits, &u, NULL, 0) == SALTS_ERANGE);
            json_free(value);
        }
    }
    it("rejects out-of-range floating conversions and accepts representable neighbors") {
        const char *invalid[] = {"9223372036854775808.0", "18446744073709551616.0", "1.5", "1e99"};
        size_t index;
        for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
            int64_t s = 91;
            json_value_t *value = parse(invalid[index]);
            char error[8];
            memset(error, 'x', sizeof(error));
            check(praktor_value_as_signed(value, 64, &s, error, sizeof(error)) == SALTS_ERANGE);
            check(s == 91 && error[sizeof(error) - 1] == '\0');
            json_free(value);
        }
        {
            int64_t s;
            uint64_t u;
            json_value_t *value = parse("-9223372036854775808.0");
            check(praktor_value_as_signed(value, 64, &s, NULL, 0) == SALTS_OK && s == INT64_MIN);
            json_free(value);
            value = json_create_number(nextafter(ldexp(1.0, 64), 0.0));
            check(praktor_value_as_unsigned(value, 64, &u, NULL, 0) == SALTS_OK);
            check(u == UINT64_MAX - UINT64_C(2047));
            json_free(value);
            value = json_create_number(nextafter(ldexp(1.0, 63), 0.0));
            check(praktor_value_as_signed(value, 64, &s, NULL, 0) == SALTS_OK);
            check(s == INT64_MAX - INT64_C(1023));
            json_free(value);
        }
    }
    it("retains lexical classification, negative zero, and type errors") {
        json_value_t *value = parse("-0");
        uint64_t u = 17;
        int64_t s = 18;
        check(praktor_value_is_int64(value) && !praktor_value_is_uint64(value));
        check(praktor_value_as_unsigned(value, 64, &u, NULL, 0) == SALTS_ERANGE && u == 17);
        json_free(value);
        value = parse("-0.0");
        check(!praktor_value_is_int64(value) && !praktor_value_is_uint64(value));
        check(praktor_value_as_unsigned(value, 64, &u, NULL, 0) == SALTS_OK && u == 0);
        json_free(value);
        value = json_create_bool(true);
        check(praktor_value_as_signed(value, 64, &s, NULL, 0) == SALTS_EINVAL && s == 18);
        json_free(value);
        value = json_create_int64(7);
        check(praktor_value_as_signed(value, 0, &s, NULL, 0) == SALTS_EINVAL);
        check(praktor_value_as_unsigned(value, 65, &u, NULL, 0) == SALTS_EINVAL);
        check(praktor_value_as_signed(value, 64, NULL, NULL, 0) == SALTS_EINVAL);
        json_free(value);
    }
    it("builds missing paths and transfers a disjoint source only after commit") {
        json_value_t *root = parse("{\"keep\":7}"), *source = parse("[1,2]");
        vstr keys[] = {vstr_from_cstr("a"), vstr_from_cstr("b"), vstr_from_cstr("c")};
        const json_value_t *borrowed = NULL;
        check(praktor_value_set_take(&root, keys, 3, &source, NULL, 0) == SALTS_OK);
        check(source == NULL);
        expect_json(root, "{\"keep\":7,\"a\":{\"b\":{\"c\":[1,2]}}}");
        check(praktor_value_find(root, keys, 3, &borrowed) == SALTS_OK);
        check(praktor_value_size(borrowed) == 2);
        source = json_create_int64(9);
        check(praktor_value_append_copy(&root, keys, 3, source, NULL, 0) == SALTS_OK);
        check(praktor_value_append_take(&root, keys, 3, &source, NULL, 0) == SALTS_OK && !source);
        expect_json(root, "{\"keep\":7,\"a\":{\"b\":{\"c\":[1,2,9,9]}}}");
        json_free(root);
    }
    it("promotes only null roots and never promotes existing null children") {
        json_value_t *root = NULL, *source = json_create_int64(9), *saved;
        vstr keys[] = {vstr_from_cstr("a"), vstr_from_cstr("b")};
        const json_value_t *found = source;
        check(praktor_value_set_copy(&root, keys, 2, source, NULL, 0) == SALTS_OK);
        expect_json(root, "{\"a\":{\"b\":9}}");
        json_free(root);
        root = parse("{\"a\":null}"); saved = root;
        check(praktor_value_set_take(&root, keys, 2, &source, NULL, 0) == SALTS_EINVAL);
        check(root == saved && source != NULL);
        expect_json(root, "{\"a\":null}");
        check(praktor_value_find(root, keys, 2, &found) == SALTS_ENOENT && found == source);
        json_free(source); json_free(root);
    }
    it("supports binary and empty keys without requiring a terminator") {
        const char bytes[] = {'a', '\0', 'b', 'z'};
        vstr keys[] = {vstr_from_buf(bytes, 3), vstr_from_buf(NULL, 0)};
        const json_value_t *found = NULL;
        json_value_t *root = json_create_null(), *source = json_create_string_n(bytes, 3);
        check(praktor_value_set_copy(&root, keys, 2, source, NULL, 0) == SALTS_OK);
        check(praktor_value_find(root, keys, 2, &found) == SALTS_OK);
        check(praktor_value_size(found) == 3 && memcmp(json_string(found), bytes, 3) == 0);
        json_free(source); json_free(root);
    }
    it("copies self insertion and replaces or erases roots transactionally") {
        json_value_t *root = parse("[1,2,3]");
        check(praktor_value_append_take(&root, NULL, 0, &root, NULL, 0) == SALTS_OK);
        expect_json(root, "[1,2,3,[1,2,3]]");
        check(praktor_value_erase(&root, 1, NULL, 0) == SALTS_OK);
        expect_json(root, "[1,3,[1,2,3]]");
        check(praktor_value_erase(&root, 3, NULL, 0) == SALTS_ENOENT);
        expect_json(root, "[1,3,[1,2,3]]");
        check(praktor_value_set_take(&root, NULL, 0, &root, NULL, 0) == SALTS_OK);
        {
            json_value_t *source = json_create_object();
            check(praktor_value_set_take(&root, NULL, 0, &source, NULL, 0) == SALTS_OK && !source);
        }
        {
            vstr key = vstr_from_cstr("self");
            check(praktor_value_set_take(&root, &key, 1, &root, NULL, 0) == SALTS_OK);
            expect_json(root, "{\"self\":{}}");
        }
        json_free(root);
    }
    it("preserves self snapshots after constructing missing parent objects") {
        vstr keys[] = {vstr_from_cstr("a"), vstr_from_cstr("b")};
        json_value_t *root = json_create_null();
        check(praktor_value_set_take(&root, keys, 2, &root, NULL, 0) == SALTS_OK);
        expect_json(root, "{\"a\":{\"b\":{\"a\":{}}}}");
        json_free(root);
        root = json_create_null();
        check(praktor_value_set_copy(&root, keys, 1, root, NULL, 0) == SALTS_OK);
        expect_json(root, "{\"a\":{}}");
        json_free(root);
    }
    it("enforces the path bound and leaves invalid outputs and owners untouched") {
        vstr keys[PRAKTOR_VALUE_MAX_PATH + 1];
        json_value_t *root = json_create_null(), *source = json_create_int64(7), *saved = root;
        const json_value_t *found = source;
        size_t index;
        for (index = 0; index < PRAKTOR_VALUE_MAX_PATH + 1; ++index) keys[index] = vstr_from_cstr("x");
        check(praktor_value_set_take(&root, keys, PRAKTOR_VALUE_MAX_PATH + 1, &source, NULL, 0) == SALTS_EINVAL);
        check(root == saved && source != NULL);
        check(praktor_value_find(root, keys, PRAKTOR_VALUE_MAX_PATH + 1, &found) == SALTS_EINVAL && found == source);
        check(praktor_value_set_copy(&root, keys, PRAKTOR_VALUE_MAX_PATH, source, NULL, 0) == SALTS_OK);
        check(praktor_value_find(root, keys, PRAKTOR_VALUE_MAX_PATH, &found) == SALTS_OK);
        check(json_number(found) > 6.9 && json_number(found) < 7.1);
        json_free(source); json_free(root);
    }
    it("rejects invalid key lengths before touching their borrowed bytes") {
        const char byte = 'x';
        vstr key = {&byte, SIZE_MAX};
        json_value_t *root = json_create_object(), *source = json_create_null();
        const json_value_t *found = source;
        check(praktor_value_set_take(&root, &key, 1, &source, NULL, 0) == SALTS_EINVAL);
        check(praktor_value_find(root, &key, 1, &found) == SALTS_EINVAL && found == source);
        check(json_object_size(root) == 0 && source != NULL);
        json_free(source); json_free(root);
    }
    it("keeps concurrent independent root owners isolated") {
        cmeta_thread_t threads[4] = {NULL};
        bool passed[4] = {false}, joined = true;
        size_t created = 0, index;
        for (; created < 4; ++created)
            if (cmeta_thread_create(&threads[created], independent_owner, &passed[created]) != SALTS_OK) break;
        for (index = 0; index < created; ++index)
            if (cmeta_thread_join(&threads[index]) != SALTS_OK) joined = false;
        check(joined && created == 4);
        for (index = 0; index < 4; ++index) check(passed[index] && threads[index] == NULL);
    }
}
