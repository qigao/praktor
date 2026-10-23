#include "projection.h"
#include <cmeta_fs.h>
#include <fmt.h>
#include <salts/error_codes.h>
#include <string.h>
#include <tinytest.h>
#include <tstr.h>

static cmeta_fs_buf_t source(const char *name) {
    cmeta_fs_buf_t bytes = {0};
    tstr path = tstr_format("{}/{}", PRAKTOR_SCHEMA_SOURCE_DIR, name);
    check(path != NULL);
    if (path) { check(cmeta_fs_read_file(path, &bytes) == 0); tstr_free(path); }
    return bytes;
}
static int equal(const json_value_t *left, const json_value_t *right) {
    size_t i, a_size = 0, b_size = 0;
    char *a, *b;
    int same;
    if (!left || !right || json_type(left) != json_type(right)) return 0;
    if (json_type(left) == JSON_OBJECT) {
        if (json_object_size(left) != json_object_size(right)) return 0;
        for (i = 0; i < json_object_size(left); ++i)
            if (!equal(json_object_value(left, i), json_object_get_v(right, json_object_key_v(left, i)))) return 0;
        return 1;
    }
    if (json_type(left) == JSON_ARRAY) {
        if (json_array_size(left) != json_array_size(right)) return 0;
        for (i = 0; i < json_array_size(left); ++i)
            if (!equal(json_array_get(left, i), json_array_get(right, i))) return 0;
        return 1;
    }
    a = json_serialize(left, &a_size); b = json_serialize(right, &b_size);
    same = a && b && a_size == b_size && memcmp(a, b, a_size) == 0;
    json_serialize_free(a); json_serialize_free(b); return same;
}
static json_value_t *parse(const char *text) {
    json_value_t *result = json_parse(text, strlen(text)); check(result != NULL); return result;
}
static int string_is(const json_value_t *value, const char *expected) {
    return value && json_type(value) == JSON_STRING &&
        json_string_len(value) == strlen(expected) && strcmp(json_string(value), expected) == 0;
}
static int number_is(const json_value_t *value, const char *expected) {
    char *text; size_t length = 0; int same;
    if (!value || json_type(value) != JSON_NUMBER) return 0;
    text = json_serialize(value, &length); same = text && strcmp(text, expected) == 0;
    json_serialize_free(text); return same;
}
static json_value_t *get(const json_value_t *root, const char *path) { return json_path_get(root, path); }

spec("C schema projection") {
    it("matches the checked-in schema and the generated C CLI artifact") {
        cmeta_fs_buf_t idl = source("schema/workflow.schema"), policy = source("schema/workflow.editor.json"), checked = source("grammar.schema.json"), generated = {0};
        json_value_t *editor = json_parse(policy.base, policy.len), *expected = json_parse(checked.base, checked.len), *actual = NULL, *from_cli;
        char error[512] = {0};
        check(editor != NULL && expected != NULL);
        check(praktor_schema_project(idl.base, idl.len, editor, &actual, error, sizeof(error)) == SALTS_OK);
        check(actual != NULL && error[0] == '\0'); check(equal(actual, expected));
        check(number_is(get(actual, "$['$defs'].taskDefinition.properties.tool.minLength"), "1"));
        check(string_is(get(actual, "$['$defs'].taskDefinition.dependencies.with[0]"), "tool"));
        check(string_is(get(actual, "$['$defs'].taskDefinition.properties.with.type"), "object"));
        check(json_object_size(get(actual, "$['$defs'].workflowInputField.properties.default")) == 0);
        check(number_is(get(actual, "$['$defs'].eachBlock.properties.items.oneOf[0].minLength"), "1"));
        check(cmeta_fs_read_file(PRAKTOR_SCHEMA_GENERATED_PATH, &generated) == 0);
        check(generated.len > 0 && generated.base[generated.len - 1] == '\n');
        from_cli = json_parse(generated.base, generated.len); check(equal(from_cli, expected));
        json_free(from_cli); json_free(actual); json_free(expected); json_free(editor);
        cmeta_fs_buf_free(&generated); cmeta_fs_buf_free(&checked); cmeta_fs_buf_free(&policy); cmeta_fs_buf_free(&idl);
    }
    it("preserves IDL aliases, presence, defaults, constraints and editor policy") {
        const char *idl =
            "schema Projection [version(1)]; message Workflow {"
            " @Size(min = 1, max = 9) [name(\"title\")] string label;"
            " optional bool enabled default false; optional string policy default \"strict\";"
            " optional @Min(0) @Max(1024) int64 retries;"
            " optional @Size(min = 1) list<string> arguments; optional @Pattern(\"^[a-z]+$\") string tag; }";
        json_value_t *editor = parse("{\"$defs\":{},\"properties\":{\"title\":{\"description\":\"A title\"}}}"), *actual = NULL;
        json_value_t *original = json_clone(editor);
        char error[256] = {0};
        check(praktor_schema_project(idl, strlen(idl), editor, &actual, error, sizeof(error)) == SALTS_OK);
        check(equal(editor, original));
        check(string_is(get(actual, "$.properties.title.type"), "string"));
        check(string_is(get(actual, "$.properties.title.description"), "A title"));
        check(number_is(get(actual, "$.properties.title.minLength"), "1"));
        check(number_is(get(actual, "$.properties.title.maxLength"), "9"));
        check(json_array_size(get(actual, "$.required")) == 1);
        check(string_is(get(actual, "$.required[0]"), "title"));
        check(json_type(get(actual, "$.properties.enabled.default")) == JSON_BOOL && !json_bool(get(actual, "$.properties.enabled.default")));
        check(string_is(get(actual, "$.properties.policy.default"), "strict"));
        check(number_is(get(actual, "$.properties.retries.minimum"), "0"));
        check(number_is(get(actual, "$.properties.retries.maximum"), "1024"));
        check(string_is(get(actual, "$.properties.arguments.items.type"), "string"));
        check(number_is(get(actual, "$.properties.arguments.minItems"), "1"));
        check(string_is(get(actual, "$.properties.tag.pattern"), "^[a-z]+$"));
        json_free(actual); json_free(original); json_free(editor);
    }
    it("merges independent required fields and keeps record references finite") {
        const char *idl = "message Workflow { Item item; optional Workflow parent; } message Item { string value; }";
        json_value_t *editor = parse("{\"$defs\":{\"Item\":{}},\"required\":[\"extra\"]}"), *actual = NULL;
        check(praktor_schema_project(idl, strlen(idl), editor, &actual, NULL, 0) == SALTS_OK);
        check(json_array_size(get(actual, "$.required")) == 2);
        check(string_is(get(actual, "$.required[0]"), "extra"));
        check(string_is(get(actual, "$.required[1]"), "item"));
        check(string_is(get(actual, "$.properties.item['$ref']"), "#/$defs/Item"));
        check(string_is(get(actual, "$.properties.parent['$ref']"), "#"));
        check(string_is(get(actual, "$['$defs'].Item.properties.value.type"), "string"));
        json_free(actual); json_free(editor);
    }
    it("rejects conflicts and unsupported IDL without mutating the borrowed policy") {
        const struct { const char *idl, *policy; } cases[] = {
            {"message Workflow { string value;", "{\"$defs\":{}}"},
            {"message Workflow { Unknown value; }", "{\"$defs\":{}}"},
            {"message Workflow { bytes value; }", "{\"$defs\":{}}"},
            {"message Workflow { string value; }", "{\"$defs\":{},\"properties\":{\"value\":{\"type\":\"integer\"}}}"},
            {"message Workflow { [alias(old)] string value; }", "{\"$defs\":{}}"},
            {"message Workflow { [name(x)] string a; [name(x)] string b; }", "{\"$defs\":{}}"},
            {"message Workflow { string value; }", "{\"$defs\":{},\"required\":[\"value\"]}"},
            {"message Workflow { Item value; } message Item { string label; }", "{\"$defs\":{}}"},
            {"message Other { string value; }", "{\"$defs\":{}}"},
            {"message Workflow { string value; }", "{\"$defs\":[]}"},
            {"message Workflow { string value; }", "{\"$defs\":{},\"required\":[42]}"}
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            json_value_t *editor = parse(cases[i].policy), *original = json_clone(editor), *actual = editor;
            char error[256] = {0};
            check(praktor_schema_project(cases[i].idl, strlen(cases[i].idl), editor, &actual, error, sizeof(error)) != SALTS_OK);
            check(actual == NULL && error[0] != '\0'); check(equal(editor, original));
            json_free(actual); json_free(original); json_free(editor);
        }
    }
    it("bounds diagnostics and clears rejected outputs") {
        json_value_t *editor = parse("{\"$defs\":{}}"), *result = editor;
        char error[1] = {'x'};
        check(praktor_schema_project("invalid", 7, editor, &result, error, sizeof(error)) == SALTS_EINVAL);
        check(result == NULL && error[0] == '\0');
        check(praktor_schema_project("invalid", 7, editor, NULL, NULL, 0) == SALTS_EINVAL);
        check(praktor_schema_project("invalid", 7, editor, &result, NULL, 1) == SALTS_EINVAL);
        json_free(editor);
    }
}
