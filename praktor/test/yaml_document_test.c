#include "yml/yaml_document.h"
#include <limits.h>
#include <fmt.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *result = NULL;
    char error[256];
    int status = praktor_yaml_document_create(vstr_from_cstr(source), &result, error, sizeof(error));
    info("parse status %d: %s", status, error);
    check(status == SALTS_OK && result && !error[0]);
    return result;
}

static praktor_yaml_node get(praktor_yaml_node root, const char *key) {
    praktor_yaml_node result = {0};
    char error[256];
    int status = praktor_yaml_get(root, key, &result, error, sizeof(error));
    info("key %s / status %d: %s", key, status, error);
    check(status == SALTS_OK && !error[0]);
    return result;
}

static void text_is(praktor_yaml_node node, const char *expected) {
    tstr text = NULL;
    check(praktor_yaml_scalar(node, &text, NULL, 0) == SALTS_OK);
    check(text && tstr_len(text) == strlen(expected) && strcmp(text, expected) == 0);
    tstr_free(text);
}

static void independent_document(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 100 && *passed; ++index) {
        praktor_yaml_document *owner = NULL;
        praktor_yaml_node value = {0};
        int number = 0;
        tstr source = tstr_format("value: {}\n", index);
        *passed = source && praktor_yaml_document_create(tstr_to_v(source), &owner, NULL, 0) == SALTS_OK;
        tstr_free(source);
        *passed = *passed && praktor_yaml_get(praktor_yaml_root(owner), "value", &value, NULL, 0) == SALTS_OK &&
            praktor_yaml_integer(value, &number, NULL, 0) == SALTS_OK && number == (int)index;
        praktor_yaml_document_destroy(owner);
    }
}

spec("C-owned YAML document and borrowed nodes") {
    it("retains empty-document refusal for null and non-null empty views") {
        praktor_yaml_document *owner = document("value: 7\n"), *result = owner;
        vstr empty[] = {{NULL, 0}, {"", 0}};
        size_t index;
        for (index = 0; index < 2; ++index) {
            char error[256];
            check(praktor_yaml_document_create(empty[index], &result, error, sizeof(error)) == SALTS_EINVAL);
            check(result == owner && strcmp(error, "Invalid YAML input") == 0);
        }
        praktor_yaml_document_destroy(owner);
    }
    it("retains an independent exact-length source until after parser destruction") {
        tstr source = tstr_dup("value: 7\nignored: [");
        praktor_yaml_document *owner = NULL, *moved;
        praktor_yaml_node value;
        tstr text = NULL;
        check(praktor_yaml_document_create(vstr_from_buf(source, 9), &owner, NULL, 0) == SALTS_OK);
        memset(source, 'x', tstr_len(source)); tstr_free(source);
        value = get(praktor_yaml_root(owner), "value");
        moved = owner; owner = NULL;
        text_is(value, "7");
        check(praktor_yaml_scalar(value, &text, NULL, 0) == SALTS_OK);
        praktor_yaml_document_destroy(moved);
        check(strcmp(text, "7") == 0);
        tstr_free(text);
        praktor_yaml_document_destroy(NULL);
    }
    it("iterates mappings and sequences and resolves aliases using the same provider") {
        praktor_yaml_document *owner = document("base: &base {name: Ada, count: 7}\ncopy: *base\nitems: [a, b]\n");
        praktor_yaml_node root = praktor_yaml_root(owner), copy = get(root, "copy"), items = get(root, "items");
        tstr key = NULL;
        check(praktor_yaml_valid(root) && praktor_yaml_is_map(root) && praktor_yaml_count(root) == 3);
        check(praktor_yaml_is_map(copy) && praktor_yaml_count(copy) == 2);
        text_is(get(copy, "name"), "Ada");
        check(praktor_yaml_key(praktor_yaml_at(root, 1), &key, NULL, 0) == SALTS_OK);
        check(strcmp(key, "copy") == 0); tstr_free(key);
        check(praktor_yaml_is_seq(items) && praktor_yaml_count(items) == 2);
        text_is(praktor_yaml_at(items, 1), "b");
        check(!praktor_yaml_valid(praktor_yaml_at(items, SIZE_MAX)));
        praktor_yaml_document_destroy(owner);
    }
    it("retains escape, folding, literal and decoded-NUL scalar semantics") {
        praktor_yaml_document *owner = document(
            "single: 'it''s fine'\ndouble: \"a\\nb\"\nbinary: \"x\\0y\"\n"
            "literal: |-\n  first\n  second\nfold: >-\n  first\n  second\n");
        praktor_yaml_node root = praktor_yaml_root(owner);
        text_is(get(root, "single"), "it's fine");
        text_is(get(root, "double"), "a\nb");
        text_is(get(root, "binary"), "x");
        text_is(get(root, "literal"), "first\nsecond");
        text_is(get(root, "fold"), "first second");
        check(praktor_yaml_is_string_scalar(get(root, "double")));
        praktor_yaml_document_destroy(owner);
    }
    it("preserves strict decoded decimal int conversion and untouched failed outputs") {
        const char *cases[] = {"7", "'7'", "-0", "'01'", "2147483647", "-2147483648",
            "+1", "0x1", "1_0", "1.0", "' 1'", "'1 '", "''", "'-'", "2147483648", "-2147483649", "9999999999999999999999999999"};
        const int expected[] = {7, 7, 0, 1, INT_MAX, INT_MIN};
        size_t index;
        for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            tstr source = tstr_format("value: {}\n", cases[index]);
            praktor_yaml_document *owner = document(source);
            praktor_yaml_node value = get(praktor_yaml_root(owner), "value");
            char error[256];
            int result = 71, status = praktor_yaml_integer(value, &result, error, sizeof(error));
            if (index < sizeof(expected) / sizeof(expected[0])) {
                check(status == SALTS_OK && result == expected[index] && !error[0]);
            } else {
                check(status == SALTS_ERANGE && result == 71);
                check(strcmp(error, "Expected YAML integer at line 1, column 1") == 0);
            }
            praktor_yaml_document_destroy(owner); tstr_free(source);
        }
    }
    it("keeps semantic integer booleans distinct from quoted text and booleans") {
        praktor_yaml_document *owner = document("values: [0, 1, 2, -1, '1', true]\n");
        praktor_yaml_node sequence = get(praktor_yaml_root(owner), "values");
        bool result = true;
        size_t index;
        check(praktor_yaml_bool_integer(praktor_yaml_at(sequence, 0), &result) == SALTS_OK && !result);
        check(praktor_yaml_bool_integer(praktor_yaml_at(sequence, 1), &result) == SALTS_OK && result);
        for (index = 2; index < 6; ++index)
            check(praktor_yaml_bool_integer(praktor_yaml_at(sequence, index), &result) == SALTS_ENOENT && result);
        praktor_yaml_document_destroy(owner);
    }
    it("keeps raw provider key lookup and decoded-key location behavior") {
        praktor_yaml_document *owner = document("\"a\\nb\": value\nplain: text\n");
        praktor_yaml_node root = praktor_yaml_root(owner), escaped = get(root, "a\\nb"), plain = get(root, "plain");
        tstr text = NULL;
        check(praktor_yaml_has(root, "a\\nb") && !praktor_yaml_has(root, "a\nb"));
        text_is(escaped, "value");
        check(praktor_yaml_key(escaped, &text, NULL, 0) == SALTS_EINVAL && !text);
        check(praktor_yaml_location(plain, &text) == SALTS_OK);
        check(strcmp(text, " at line 2, column 1") == 0); tstr_free(text);
        praktor_yaml_document_destroy(owner);
    }
    it("preserves parse diagnostics and publishes no document on failure") {
        praktor_yaml_document *owner = document("keep: 7\n"), *result = owner;
        const char *invalid[] = {"a: 1\na: 2\n", "value: [\n"};
        size_t index;
        for (index = 0; index < 2; ++index) {
            char error[256];
            check(praktor_yaml_document_create(vstr_from_cstr(invalid[index]), &result, error, sizeof(error)) == SALTS_EINVAL);
            check(result == owner && error[0]);
            if (!index) check(strstr(error, "Duplicate mapping key at line 2, column 1") != NULL);
        }
        {
            char error[8];
            vstr bad = {NULL, 1}, huge = {"x", SIZE_MAX};
            memset(error, 'x', sizeof(error));
            check(praktor_yaml_document_create(bad, &result, error, sizeof(error)) == SALTS_EINVAL && result == owner);
            check(error[7] == '\0');
            check(praktor_yaml_document_create(huge, &result, NULL, 0) == SALTS_EINVAL && result == owner);
            check(praktor_yaml_document_create(vstr_from_cstr(""), NULL, NULL, 0) == SALTS_EINVAL);
        }
        text_is(get(praktor_yaml_root(owner), "keep"), "7");
        praktor_yaml_document_destroy(owner);
    }
    it("keeps missing or invalid node and scalar outputs unchanged") {
        praktor_yaml_document *owner = document("value: [1,2]\n");
        praktor_yaml_node root = praktor_yaml_root(owner), result = root;
        tstr sentinel = tstr_dup("retained"), text = sentinel;
        int number = 71;
        check(praktor_yaml_get(root, "absent", &result, NULL, 0) == SALTS_ENOENT && result.node == root.node);
        check(praktor_yaml_get(root, NULL, &result, NULL, 0) == SALTS_ENOENT);
        check(praktor_yaml_get(root, "value", NULL, NULL, 0) == SALTS_EINVAL);
        check(praktor_yaml_scalar(root, &text, NULL, 0) == SALTS_EINVAL && text == sentinel);
        check(praktor_yaml_key(root, &text, NULL, 0) == SALTS_EINVAL && text == sentinel);
        check(praktor_yaml_integer(root, &number, NULL, 0) == SALTS_EINVAL && number == 71);
        check(praktor_yaml_location(root, NULL) == SALTS_EINVAL);
        tstr_free(sentinel); praktor_yaml_document_destroy(owner);
    }
    it("bounds alias work for borrowed metadata including nontrivial cycles") {
        cyaml_doc_t metadata = {0};
        cyaml_node_t nodes[PRAKTOR_YAML_MAX_ALIAS_HOPS + 2] = {0};
        praktor_yaml_node handle = {&metadata, &nodes[0], NULL};
        size_t index;
        for (index = 0; index < PRAKTOR_YAML_MAX_ALIAS_HOPS + 1; ++index) {
            nodes[index].type = CYAML_ALIAS; nodes[index].alias.target = &nodes[index + 1];
        }
        nodes[index].type = CYAML_SCALAR;
        check(!praktor_yaml_valid(handle) && !praktor_yaml_has_val(handle));
        handle.node = &nodes[1];
        check(praktor_yaml_valid(handle) && praktor_yaml_has_val(handle));
        nodes[0].alias.target = &nodes[1]; nodes[1].alias.target = &nodes[0]; handle.node = &nodes[0];
        check(!praktor_yaml_valid(handle) && !praktor_yaml_is_map(handle));
    }
    it("retains the 32-level source-location search and key preference") {
        cyaml_node_t nodes[PRAKTOR_YAML_LOCATION_DEPTH + 1] = {0}, key = {0};
        cyaml_node_t *children[PRAKTOR_YAML_LOCATION_DEPTH];
        praktor_yaml_node handle = {NULL, &nodes[0], NULL};
        tstr text = NULL;
        size_t index;
        for (index = 0; index < PRAKTOR_YAML_LOCATION_DEPTH; ++index) {
            children[index] = &nodes[index + 1]; nodes[index].type = CYAML_SEQ;
            nodes[index].seq.items = &children[index]; nodes[index].seq.count = 1;
        }
        nodes[31].span.start_line = 9; nodes[31].span.start_col = 2;
        check(praktor_yaml_location(handle, &text) == SALTS_OK && strcmp(text, " at line 9, column 2") == 0);
        tstr_free(text); text = NULL;
        nodes[31].span.start_line = 0; nodes[32].span.start_line = 9;
        check(praktor_yaml_location(handle, &text) == SALTS_OK && tstr_len(text) == 0);
        tstr_free(text); text = NULL;
        key.span.start_line = 7; key.span.start_col = 5; handle.key_node = &key;
        check(praktor_yaml_location(handle, &text) == SALTS_OK && strcmp(text, " at line 7, column 5") == 0);
        tstr_free(text);
    }
    it("isolates concurrent independent parser and source owners") {
        cmeta_thread_t threads[4] = {NULL};
        bool passed[4] = {false}, joined = true;
        size_t created = 0, index;
        for (; created < 4; ++created)
            if (cmeta_thread_create(&threads[created], independent_document, &passed[created]) != SALTS_OK) break;
        for (index = 0; index < created; ++index)
            if (cmeta_thread_join(&threads[index]) != SALTS_OK) joined = false;
        check(joined && created == 4);
        for (index = 0; index < 4; ++index) check(passed[index] && !threads[index]);
    }
}
