#include "yml/yaml_contract.h"
#include <string.h>
#include <tinytest.h>

static praktor_yaml_document *make_doc(const char *source) {
    praktor_yaml_document *doc = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &doc, NULL) == SALTS_OK);
    return doc;
}
static json_value_t *project(praktor_yaml_document *doc, const char *key, bool output,
                             praktor_yaml_field_error *error) {
    praktor_yaml_node node = {0};
    json_value_t *result = NULL;
    check(praktor_yaml_get(praktor_yaml_root(doc), key, &node, NULL) == SALTS_OK);
    check(praktor_yaml_contract_fields_create(node, output, &result, error) == SALTS_OK);
    return result;
}
static void equals_json(const json_value_t *value, const char *expected) {
    char *text = json_serialize(value, NULL);
    check(text != NULL && strcmp(text, expected) == 0);
    json_serialize_free(text);
}

spec("C-owned YAML workflow contract projection") {
    it("projects validated defaults, enum values, and descriptions") {
        praktor_yaml_document *doc = make_doc(
            "inputs:\n  count:\n    type: integer\n    required: true\n"
            "    default: 3\n    enum: [1, 2, 3]\n    description: Retry count\n");
        praktor_yaml_field_error error = {0};
        json_value_t *result = project(doc, "inputs", false, &error);
        equals_json(result, "{\"count\":{\"type\":\"integer\",\"required\":true,"
            "\"description\":\"Retry count\",\"default\":3,\"enum\":[1,2,3]}}");
        json_free(result); praktor_yaml_field_error_dispose(&error);
        praktor_yaml_document_destroy(doc);
    }
    it("accepts output values and refuses value on input fields") {
        praktor_yaml_document *doc = make_doc(
            "outputs:\n  result:\n    type: string\n    value: '{{ tasks.build.outputs.x }}'\n"
            "inputs:\n  bad:\n    type: string\n    value: forbidden\n");
        praktor_yaml_field_error error = {0};
        json_value_t *result = project(doc, "outputs", true, &error);
        equals_json(result, "{\"result\":{\"type\":\"string\",\"value\":\"{{ tasks.build.outputs.x }}\"}}");
        json_free(result);
        {
            praktor_yaml_node node = {0};
            check(praktor_yaml_get(praktor_yaml_root(doc), "inputs", &node, NULL) == SALTS_OK);
            result = NULL;
            check(praktor_yaml_contract_fields_create(node, false, &result, &error) == SALTS_EINVAL);
            check(result == NULL && error.text != NULL);
        }
        praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(doc);
    }
    it("rejects invalid defaults, enum values, unknown keys and empty enums") {
        const char *bad[] = {
            "inputs:\n  n:\n    type: integer\n    default: nope\n",
            "inputs:\n  n:\n    type: integer\n    enum: [1, nope]\n",
            "inputs:\n  n:\n    type: integer\n    mystery: 1\n",
            "inputs:\n  n:\n    type: integer\n    enum: []\n"
        };
        size_t i;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
            praktor_yaml_document *doc = make_doc(bad[i]);
            praktor_yaml_node node = {0};
            praktor_yaml_field_error error = {0};
            json_value_t *result = NULL;
            check(praktor_yaml_get(praktor_yaml_root(doc), "inputs", &node, NULL) == SALTS_OK);
            check(praktor_yaml_contract_fields_create(node, false, &result, &error) != SALTS_OK);
            check(result == NULL && error.text != NULL);
            praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(doc);
        }
    }
    it("keeps caller output unchanged on invalid root") {
        praktor_yaml_document *doc = make_doc("inputs: []\n");
        praktor_yaml_node node = {0};
        praktor_yaml_field_error error = {0};
        json_value_t *sentinel = json_create_int64(7), *result = sentinel;
        check(praktor_yaml_get(praktor_yaml_root(doc), "inputs", &node, NULL) == SALTS_OK);
        check(praktor_yaml_contract_fields_create(node, false, &result, &error) == SALTS_EINVAL);
        check(result == sentinel);
        json_free(sentinel); praktor_yaml_field_error_dispose(&error); praktor_yaml_document_destroy(doc);
    }
}
