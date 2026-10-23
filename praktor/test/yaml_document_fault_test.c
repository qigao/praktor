/* Inject failures at the production module's public provider/owner boundaries.
 * Parsing and decoding still use the real YAML provider, with real allocations. */
#include "yml/yaml_document.h"
#include <fmt.h>
#include <stdlib.h>
#include <string.h>
#include <tinytest.h>

static unsigned failure_point, operation;
static tstr retained_source;
static bool parser_alive, wrong_order;
static bool injected(void) { return failure_point && ++operation == failure_point; }
static void *fault_alloc(size_t size) { return injected() ? NULL : malloc(size); }
static tstr fault_source(vstr source) {
    retained_source = injected() ? NULL : tstr_from_v(source);
    return retained_source;
}
static cyaml_doc_t *fault_parse(const char *source, size_t size, const cyaml_opts_t *options, cyaml_error_t *error) {
    cyaml_doc_t *result;
    if (injected()) { error->code = CYAML_ERR_NOMEM; return NULL; }
    result = cyaml_parse(source, size, options, error);
    parser_alive = result != NULL;
    return result;
}
static void ordered_parser_free(cyaml_doc_t *document) {
    if (document) {
        /* Source must still be readable while the provider is being destroyed. */
        wrong_order = wrong_order || cyaml_src(document) != retained_source ||
            memcmp(cyaml_src(document), "first:", 6) != 0;
        parser_alive = false;
    }
    cyaml_free(document);
}
static void ordered_text_free(tstr text) {
    if (text && text == retained_source && parser_alive) wrong_order = true;
    tstr_free(text);
}
static char *fault_decode(const cyaml_doc_t *document, const cyaml_node_t *node) {
    return injected() ? NULL : cyaml_scalar_str(document, node);
}
static tstr fault_copy(const char *text) { return injected() ? NULL : tstr_dup(text); }
static tstr fault_location(uint32_t line, uint32_t column) {
    return injected() ? NULL : tstr_format(" at line {}, column {}", line, column);
}

#define malloc fault_alloc
#define tstr_from_v fault_source
#define cyaml_parse fault_parse
#define cyaml_free ordered_parser_free
#define tstr_free ordered_text_free
#define cyaml_scalar_str fault_decode
#define tstr_dup fault_copy
#undef tstr_format
#define tstr_format(pattern, line, column) fault_location(line, column)
#include "../src/yml/yaml_document.c"
#undef malloc
#undef tstr_from_v
#undef cyaml_parse
#undef cyaml_free
#undef tstr_free
#undef cyaml_scalar_str
#undef tstr_dup
#undef tstr_format

static const char *source = "first: text\nvalue: '7'\n";
static praktor_yaml_document *owner(void) {
    praktor_yaml_document *result = NULL;
    check(praktor_yaml_document_create(vstr_from_cstr(source), &result, NULL, 0) == SALTS_OK);
    return result;
}

spec("C YAML owner and output rollback at every provider boundary") {
    it("releases partial construction and keeps the prior output unchanged") {
        unsigned point;
        for (point = 1; point < 8; ++point) {
            praktor_yaml_document previous = {0}, *result = &previous;
            char error[256];
            int status;
            operation = 0; failure_point = point; parser_alive = false; wrong_order = false;
            status = praktor_yaml_document_create(vstr_from_cstr(source), &result, error, sizeof(error));
            failure_point = 0;
            if (status == SALTS_OK) praktor_yaml_document_destroy(result);
            else check(status == SALTS_ENOMEM && result == &previous && error[0]);
            check(!parser_alive && !wrong_order);
            if (status == SALTS_OK) break;
        }
        check(point == 4);
    }
    it("keeps scalar and key output owners unchanged on decode/copy failure") {
        unsigned action, point;
        for (action = 0; action < 2; ++action) {
            for (point = 1; point < 8; ++point) {
                praktor_yaml_document *document = owner();
                praktor_yaml_node value = praktor_yaml_at(praktor_yaml_root(document), 1);
                tstr previous = tstr_dup("retained"), result = previous;
                int status;
                operation = 0; failure_point = point;
                status = action ? praktor_yaml_key(value, &result, NULL, 0) : praktor_yaml_scalar(value, &result, NULL, 0);
                failure_point = 0;
                if (status == SALTS_OK) { check(strcmp(result, action ? "value" : "7") == 0); tstr_free(result); }
                else check(status == SALTS_ENOMEM && result == previous);
                tstr_free(previous); praktor_yaml_document_destroy(document);
                check(!parser_alive && !wrong_order);
                if (status == SALTS_OK) break;
            }
            check(point == 3);
        }
    }
    it("keeps lookup handles unchanged if decoded-key location discovery fails") {
        unsigned point;
        for (point = 1; point < 12; ++point) {
            praktor_yaml_document *document = owner();
            praktor_yaml_node root = praktor_yaml_root(document), result = root;
            int status;
            operation = 0; failure_point = point;
            status = praktor_yaml_get(root, "value", &result, NULL, 0);
            failure_point = 0;
            if (status != SALTS_OK) {
                check(status == SALTS_ENOMEM && result.document == root.document && result.node == root.node && result.key_node == root.key_node);
            } else check(result.node == cyaml_get(root.document, root.node, "value"));
            praktor_yaml_document_destroy(document);
            check(!parser_alive && !wrong_order);
            if (status == SALTS_OK) break;
        }
        check(point == 5);
    }
    it("keeps integer and location outputs unchanged on allocation failure") {
        unsigned action, point;
        for (action = 0; action < 3; ++action) {
            for (point = 1; point < 8; ++point) {
                praktor_yaml_document *document = owner();
                praktor_yaml_node node = praktor_yaml_at(praktor_yaml_root(document), 1), empty = {0};
                tstr previous = tstr_dup("retained"), result = previous;
                int number = 71, status;
                operation = 0; failure_point = point;
                status = !action ? praktor_yaml_integer(node, &number, NULL, 0) :
                    praktor_yaml_location(action == 1 ? node : empty, &result);
                failure_point = 0;
                if (status != SALTS_OK) check(status == SALTS_ENOMEM && number == 71 && result == previous);
                else if (!action) check(number == 7);
                else tstr_free(result);
                tstr_free(previous); praktor_yaml_document_destroy(document);
                check(!parser_alive && !wrong_order);
                if (status == SALTS_OK) break;
            }
            check(point == (!action ? 3u : 2u));
        }
    }
}
