#include "yml/yaml_value.h"
#include <fmt.h>
#include <salts/thread.h>
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *document(const char *source) {
    praktor_yaml_document *owner = NULL;
    char message[256];
    praktor_yaml_diagnostic error = {message, sizeof(message), PRAKTOR_YAML_FAILURE_NONE};
    int status = praktor_yaml_document_create(vstr_from_cstr(source), &owner, &error);
    info("source %s: status %d: %s", source, status, message);
    check(status == SALTS_OK);
    return owner;
}
static praktor_yaml_node value(praktor_yaml_document *owner) {
    return praktor_yaml_at(praktor_yaml_root(owner), 0);
}
static json_value_t *convert(praktor_yaml_node node) {
    json_value_t *result = NULL;
    praktor_yaml_field_error error = {0};
    int status = praktor_yaml_value(node, &result, &error);
    info("convert status %d: %s", status, error.text ? error.text : "");
    check(status == SALTS_OK && result && !error.text);
    praktor_yaml_field_error_dispose(&error);
    return result;
}
static void json_is(const json_value_t *result, const char *expected) {
    char *text = json_serialize(result, NULL);
    check(text && strcmp(text, expected) == 0);
    json_serialize_free(text);
}
static void independent_values(void *argument) {
    bool *passed = (bool *)argument;
    unsigned index;
    *passed = true;
    for (index = 0; index < 100 && *passed; ++index) {
        praktor_yaml_document *owner = NULL;
        json_value_t *result = NULL;
        praktor_yaml_field_error error = {0};
        *passed = praktor_yaml_document_create(vstr_from_cstr("value: {a: [1, '1', true]}\n"), &owner, NULL) == SALTS_OK &&
            praktor_yaml_value(value(owner), &result, &error) == SALTS_OK && !error.text &&
            json_array_size(json_object_get(result, "a")) == 3;
        json_free(result);
        praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(owner);
    }
}

spec("C YAML contract values and mapping whitelists") {
    it("preserves quoted types, exact signed integers and numeric fallback") {
        const struct { const char *scalar, *expected; json_type_t type; } cases[] = {
            {"'true'", "\"true\"", JSON_STRING}, {"true", "true", JSON_BOOL}, {"false", "false", JSON_BOOL},
            {"'1'", "\"1\"", JSON_STRING}, {"01", "1", JSON_NUMBER}, {"-0", "0", JSON_NUMBER},
            {"9223372036854775807", "9223372036854775807", JSON_NUMBER},
            {"-9223372036854775808", "-9223372036854775808", JSON_NUMBER},
            {"+1", "1", JSON_NUMBER}, {"0x1", "1", JSON_NUMBER}, {"2.5", "2.5", JSON_NUMBER},
            {"1e2", "100", JSON_NUMBER}, {"'null'", "\"null\"", JSON_STRING},
            {"hello", "\"hello\"", JSON_STRING}, {"''", "\"\"", JSON_STRING}
        };
        size_t index;
        for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            tstr source = tstr_format("value: {}\n", cases[index].scalar);
            praktor_yaml_document *owner = document(source);
            json_value_t *result = convert(value(owner));
            check(json_type(result) == cases[index].type);
            json_is(result, cases[index].expected);
            json_free(result); praktor_yaml_document_destroy(owner); tstr_free(source);
        }
    }
    it("owns nested objects and arrays independently of the document") {
        praktor_yaml_document *owner = document("value: {items: [1, '2', true, {name: Ada}], empty: {}, list: []}\n");
        json_value_t *result = convert(value(owner));
        praktor_yaml_document_destroy(owner);
        json_is(result, "{\"items\":[1,\"2\",true,{\"name\":\"Ada\"}],\"empty\":{},\"list\":[]}");
        json_free(result);
    }
    it("creates independent copies when collection aliases repeat") {
        praktor_yaml_document *owner = document("base: &base {n: 7}\nvalue: [*base, *base]\n");
        json_value_t *result = convert(praktor_yaml_at(praktor_yaml_root(owner), 1));
        json_value_t *replacement = json_create_int64(9);
        check(replacement && json_object_add_checked(json_array_get(result, 0), "n", replacement));
        praktor_yaml_document_destroy(owner);
        json_is(result, "[{\"n\":9},{\"n\":7}]");
        json_free(result);
    }
    it("retains decoded-NUL truncation and last decoded duplicate key wins") {
        praktor_yaml_document *owner = document("value: {\"a\\0first\": 1, \"a\\0second\": 2, text: \"x\\0y\"}\n");
        json_value_t *result = convert(value(owner));
        json_is(result, "{\"a\":2,\"text\":\"x\"}");
        json_free(result); praktor_yaml_document_destroy(owner);
    }
    it("preserves null-node refusal and leaves existing output owners unchanged") {
        praktor_yaml_document *owner = document("value:\n  good: 1\n  bad: null\n");
        praktor_yaml_field_error error = {0};
        json_value_t *previous = json_create_int64(71), *result = previous;
        check(previous && praktor_yaml_value(value(owner), &result, &error) == SALTS_EINVAL);
        check(result == previous && error.origin == PRAKTOR_YAML_FAILURE_INPUT);
        check(strcmp(error.text, "Parse error at line 3, column 3: contract value must be a scalar, sequence, or map") == 0);
        json_is(previous, "71");
        praktor_yaml_field_error_dispose(&error);
        json_free(previous); praktor_yaml_document_destroy(owner);
    }
    it("keeps large contract strings outside the ordinary scalar-field length limit") {
        tstr text = tstr_new_len(NULL, 20000), source;
        praktor_yaml_document *owner;
        json_value_t *result;
        check(text); memset(text, 'x', tstr_len(text));
        source = tstr_format("value: '{}'\n", text); check(source);
        owner = document(source); result = convert(value(owner));
        check(json_type(result) == JSON_STRING && json_string_len(result) == 20000);
        json_free(result); praktor_yaml_document_destroy(owner); tstr_free(source); tstr_free(text);
    }
    it("enforces the exact native nesting boundary without partial output") {
        unsigned depth;
        for (depth = PRAKTOR_YAML_VALUE_MAX_DEPTH - 1; depth <= PRAKTOR_YAML_VALUE_MAX_DEPTH; ++depth) {
            tstr source = tstr_new_len(NULL, depth * 2 + 1);
            praktor_yaml_document *owner;
            praktor_yaml_field_error error = {0};
            json_value_t *previous = json_create_int64(71), *result = previous;
            int status;
            check(source && previous);
            memset(source, '[', depth); source[depth] = '1'; memset(source + depth + 1, ']', depth);
            owner = document(source);
            status = praktor_yaml_value(praktor_yaml_root(owner), &result, &error);
            if (depth == PRAKTOR_YAML_VALUE_MAX_DEPTH - 1) { check(status == SALTS_OK && result != previous); json_free(result); }
            else check(status == SALTS_EINVAL && result == previous && strstr(error.text, "maximum depth of 128"));
            json_free(previous); praktor_yaml_field_error_dispose(&error);
            praktor_yaml_document_destroy(owner); tstr_free(source);
        }
    }
    it("enforces the exact expanded value budget") {
        size_t count;
        for (count = PRAKTOR_YAML_VALUE_MAX_NODES - 1; count <= PRAKTOR_YAML_VALUE_MAX_NODES; ++count) {
            tstr source = tstr_new_len(NULL, count * 2 + 1);
            praktor_yaml_document *owner;
            praktor_yaml_field_error error = {0};
            json_value_t *result = NULL;
            size_t index;
            int status;
            check(source); source[0] = '[';
            for (index = 0; index < count; ++index) { source[index * 2 + 1] = '1'; source[index * 2 + 2] = ','; }
            source[count * 2] = ']';
            owner = document(source);
            status = praktor_yaml_value(praktor_yaml_root(owner), &result, &error);
            if (count == PRAKTOR_YAML_VALUE_MAX_NODES - 1) check(status == SALTS_OK && json_array_size(result) == count);
            else check(status == SALTS_EINVAL && !result && strstr(error.text, "maximum count of 16384"));
            json_free(result); praktor_yaml_field_error_dispose(&error);
            praktor_yaml_document_destroy(owner); tstr_free(source);
        }
    }
    it("fails closed on recursive and exponentially expanded aliases") {
        praktor_yaml_document *owner = document("value: &loop [*loop]\n");
        praktor_yaml_field_error error = {0};
        json_value_t *result = NULL;
        tstr source = tstr_dup("a0: &a0 [1]\n");
        unsigned index;
        check(praktor_yaml_value(value(owner), &result, &error) == SALTS_EINVAL && !result);
        check(strstr(error.text, "maximum depth of 128"));
        praktor_yaml_document_destroy(owner);
        for (index = 1; index <= 14; ++index) {
            source = tstr_append_format(source, "a{}: &a{} [*a{}, *a{}]\n", index, index, index - 1, index - 1);
            check(source);
        }
        owner = document(source);
        check(praktor_yaml_value(praktor_yaml_at(praktor_yaml_root(owner), 14), &result, &error) == SALTS_EINVAL && !result);
        check(strstr(error.text, "maximum count of 16384"));
        praktor_yaml_document_destroy(owner); praktor_yaml_field_error_dispose(&error); tstr_free(source);
    }
    it("checks decoded case-sensitive keys in source order without modifying the document") {
        praktor_yaml_document *owner = document("good: 1\nBad: 2\nlater: 3\n");
        praktor_yaml_field_error error = {0};
        const vstr allowed[] = {{"good", 4}, {"bad", 3}};
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), allowed, 2, &error) == SALTS_EINVAL);
        check(strcmp(error.text, "Parse error at line 2, column 1: Unknown key: 'Bad'") == 0);
        check(praktor_yaml_count(praktor_yaml_root(owner)) == 3);
        check(praktor_yaml_check_keys(value(owner), NULL, 0, &error) == SALTS_OK && !error.text);
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), NULL, 1, &error) == SALTS_EINVAL);
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), &(vstr){NULL, 1}, 1, &error) == SALTS_EINVAL);
        check(praktor_yaml_value(value(owner), NULL, &error) == SALTS_EINVAL);
        praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(owner);
    }
    it("matches truncated decoded keys against exact whitelist views") {
        praktor_yaml_document *owner = document("\"key\\0tail\": 1\n");
        const vstr allowed[] = {{"key", 3}, {"key\0tail", 8}};
        praktor_yaml_field_error error = {0};
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), allowed, 1, &error) == SALTS_OK);
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), allowed + 1, 1, &error) == SALTS_EINVAL);
        check(strstr(error.text, "Unknown key: 'key'"));
        praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(owner);
    }
    it("keeps the complete diagnostic for an unknown key longer than a field value") {
        tstr key = tstr_new_len(NULL, 13000), source;
        praktor_yaml_document *owner;
        praktor_yaml_field_error error = {0};
        check(key); memset(key, 'x', tstr_len(key));
        source = tstr_format("'{}': 1\n", key); check(source);
        owner = document(source);
        check(praktor_yaml_check_keys(praktor_yaml_root(owner), NULL, 0, &error) == SALTS_EINVAL);
        check(tstr_len(error.text) == 13000 + strlen("Parse error at line 1, column 1: Unknown key: ''"));
        check(strstr(error.text, key));
        praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(owner);
        tstr_free(source); tstr_free(key);
    }
    it("isolates concurrent independent conversions and result owners") {
        cmeta_thread_t threads[4] = {NULL};
        bool passed[4] = {false}, joined = true;
        size_t created = 0, index;
        for (; created < 4; ++created)
            if (cmeta_thread_create(&threads[created], independent_values, &passed[created]) != SALTS_OK) break;
        for (index = 0; index < created; ++index)
            if (cmeta_thread_join(&threads[index]) != SALTS_OK) joined = false;
        check(joined && created == 4);
        for (index = 0; index < 4; ++index) check(passed[index] && !threads[index]);
    }
}
