#include "data/structured_document_query.h"
#include <stdint.h>
#include <string.h>
#include <tinytest.h>

static void expect(praktor_document_format format, const char *document,
    const char *path, const char *expected) {
    json_value_t *result = NULL;
    char error[1024], *serialized;
    int status = praktor_document_query(format, document, strlen(document), path,
        strlen(path), &result, error, sizeof(error));
    info("query: %s; status: %d; error: %s", path, status, error);
    check(status == SALTS_OK);
    check(result != NULL && error[0] == '\0');
    serialized = json_serialize(result, NULL);
    check(serialized != NULL);
    info("result: %s", serialized);
    check(strcmp(serialized, expected) == 0);
    json_serialize_free(serialized);
    json_free(result);
}

spec("C structured document queries") {
    it("retains JSONPath normalization and zero/one/many result shapes") {
        const char *document = "{\"users\":[{\"name\":\"Ada\"},{\"name\":\"Lin\"}]}";
        expect(PRAKTOR_DOCUMENT_JSON, document, "$.users[*].name", "[\"Ada\",\"Lin\"]");
        expect(PRAKTOR_DOCUMENT_JSON, document, "users[0].name", "\"Ada\"");
        expect(PRAKTOR_DOCUMENT_JSON, document, "$.absent", "null");
        expect(PRAKTOR_DOCUMENT_JSON, "[7,9]", "[1]", "9");
        expect(PRAKTOR_DOCUMENT_JSON, "{\"value\":7}", "", "{\"value\":7}");
    }
    it("retains YPATH and scalar types after freeing the YAML document") {
        const char *document = "users:\n  - name: Ada\n  - name: Lin\n";
        expect(PRAKTOR_DOCUMENT_YAML, document, "/users[*]/name", "[\"Ada\",\"Lin\"]");
        expect(PRAKTOR_DOCUMENT_YAML, "value: 7\n", "/value", "7");
        expect(PRAKTOR_DOCUMENT_YAML, "value: true\n", "/value", "true");
        expect(PRAKTOR_DOCUMENT_YAML, document, "/absent", "null");
        expect(PRAKTOR_DOCUMENT_YAML, "value: 7\n", "", "{\"value\":7}");
    }
    it("retains XPath text, attributes, element serialization and collapse") {
        const char *document = "<users><name id=\"7\">Ada</name><name>Lin</name></users>";
        expect(PRAKTOR_DOCUMENT_XML, document, "//name/text()", "[\"Ada\",\"Lin\"]");
        expect(PRAKTOR_DOCUMENT_XML, document, "//name/@id", "\"7\"");
        expect(PRAKTOR_DOCUMENT_XML, "<root><child/></root>", "//child", "\"[Element]='\\n<child/>'\"");
        expect(PRAKTOR_DOCUMENT_XML, document, "//missing", "null");
    }
    it("always returns CSV row arrays with string values and unchanged filters") {
        const char *document = "name,score_n\r\nAda,9\r\nLin,7\r\n";
        expect(PRAKTOR_DOCUMENT_CSV, document, "score_n >= 8", "[{\"name\":\"Ada\",\"score_n\":\"9\"}]");
        expect(PRAKTOR_DOCUMENT_CSV, document, "score_n > 10", "[]");
        expect(PRAKTOR_DOCUMENT_CSV, document, "score_n >= 9 or name == \"Lin\"",
            "[{\"name\":\"Ada\",\"score_n\":\"9\"},{\"name\":\"Lin\",\"score_n\":\"7\"}]");
        expect(PRAKTOR_DOCUMENT_CSV, "name,name,score_n\nAda,Lin,9\n", "score_n == 9",
            "[{\"name\":\"Lin\",\"score_n\":\"9\"}]");
        expect(PRAKTOR_DOCUMENT_CSV, "name,score_n\n\"Ada, A\",9\n", "score_n == 9",
            "[{\"name\":\"Ada, A\",\"score_n\":\"9\"}]");
    }
    it("deep copies borrowed JSON and does not mutate the root") {
        json_value_t *root = json_parse("{\"nested\":{\"value\":7}}", 22);
        json_value_t *result = NULL;
        char error[256];
        check(root != NULL);
        check(praktor_document_query_json(root, "nested", 6, &result, error, sizeof(error)) == SALTS_OK);
        check(result != json_object_get(root, "nested"));
        check(json_object_add_checked(result, "value", json_create_int64(9)));
        check(json_get_int(json_object_get(root, "nested"), "value", -1) == 7);
        json_free(root);
        check(json_get_int(result, "value", -1) == 9);
        json_free(result);
    }
    it("uses exact document lengths and supports unterminated path views") {
        const char document[] = "[7,9]unused";
        const char path[] = {'[', '1', ']', 'x'};
        const char binary_path[] = {'[', '1', ']', '\0', 'x'};
        json_value_t *result = NULL;
        check(praktor_document_query(PRAKTOR_DOCUMENT_JSON, document, 5, path, 3, &result, NULL, 0) == SALTS_OK);
        check(json_type(result) == JSON_NUMBER && strcmp(json_number_text(result, NULL), "9") == 0);
        json_free(result);
        check(praktor_document_query(PRAKTOR_DOCUMENT_JSON, document, 5, binary_path, sizeof(binary_path), &result, NULL, 0) == SALTS_OK);
        check(json_type(result) == JSON_NUMBER && strcmp(json_number_text(result, NULL), "9") == 0);
        json_free(result);
        expect(PRAKTOR_DOCUMENT_JSON, "{\"value\":\"a\\u0000b\"}", "value", "\"a\\u0000b\"");
    }
    it("rejects malformed documents and paths without publishing partial results") {
        const struct { praktor_document_format format; const char *document, *path; } cases[] = {
            {PRAKTOR_DOCUMENT_JSON, "{", "$"},
            {PRAKTOR_DOCUMENT_JSON, "{}", "$["},
            {PRAKTOR_DOCUMENT_YAML, "value: [\n", "/"},
            {PRAKTOR_DOCUMENT_YAML, "value: 7\n", "/value["},
            {PRAKTOR_DOCUMENT_XML, "<root>", "//root"},
            {PRAKTOR_DOCUMENT_XML, "<root/>", ""},
            {PRAKTOR_DOCUMENT_XML, "<root/>", "//*["},
            {PRAKTOR_DOCUMENT_CSV, "name,score_n\nAda,9\n", ""},
            {PRAKTOR_DOCUMENT_CSV, "name,score_n\nAda,9\n", "score_n >="}
        };
        json_value_t *sentinel = json_create_null();
        size_t index;
        check(sentinel != NULL);
        for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
            json_value_t *result = sentinel;
            char error[1024];
            int status = praktor_document_query(cases[index].format, cases[index].document,
                strlen(cases[index].document), cases[index].path, strlen(cases[index].path),
                &result, error, sizeof(error));
            info("case %zu: status %d: %s", index, status, error);
            check(status == SALTS_EINVAL && result == sentinel && error[0] != '\0');
        }
        json_free(sentinel);
    }
    it("checks C argument contracts and terminates short diagnostic buffers") {
        json_value_t *sentinel = json_create_null(), *result = sentinel;
        char error[2] = {'x', 'x'};
        check(sentinel != NULL);
        check(praktor_document_query(PRAKTOR_DOCUMENT_JSON, NULL, 1, NULL, 0, &result, error, 2) == SALTS_EINVAL);
        check(result == sentinel && error[0] != '\0' && error[1] == '\0');
        check(praktor_document_query(PRAKTOR_DOCUMENT_JSON, "{}", 2, NULL, 1, &result, NULL, 0) == SALTS_EINVAL);
        check(praktor_document_query_json(sentinel, "$", SIZE_MAX, &result, NULL, 0) == SALTS_EINVAL);
        check(praktor_document_query_json(sentinel, NULL, 0, NULL, NULL, 0) == SALTS_EINVAL);
        check(praktor_document_query_json(sentinel, NULL, 0, &result, NULL, 1) == SALTS_EINVAL);
        check(praktor_document_query((praktor_document_format)99, "{}", 2, NULL, 0, &result, NULL, 0) == SALTS_EINVAL);
        check(result == sentinel);
        json_free(sentinel);
    }
}
