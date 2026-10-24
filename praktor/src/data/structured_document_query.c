#include "data/structured_document_query.h"
#include <csv_parser.h>
#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <dsv_filter.h>
#include <fmt.h>
#include <stdint.h>
#include <string.h>
#include <tstr.h>
#include <xml_parser/xml_parser.h>

typedef struct query_error { char *text; size_t capacity; } query_error;

static int fail(query_error *error, int status, const char *message) {
    if (error->text && error->capacity)
        (void)fmt(error->text, error->capacity, "{}", message);
    return status;
}

/* Consume value on both success and failure. Checked JSON adoption itself
 * leaves ownership with the caller on failure. */
static int push(query_error *error, json_value_t *array, json_value_t *value) {
    if (!value || !json_array_add_checked(array, value)) {
        json_free(value);
        return fail(error, SALTS_ENOMEM, "Failed to append query result");
    }
    return SALTS_OK;
}

static int collapse(query_error *error, json_value_t *matches, json_value_t **out) {
    size_t count = json_array_size(matches);
    json_value_t *value;
    if (count > 1) { *out = matches; return SALTS_OK; }
    value = count ? json_clone(json_array_get(matches, 0)) : json_create_null();
    json_free(matches);
    if (!value) return fail(error, SALTS_ENOMEM, "Failed to allocate query result");
    *out = value;
    return SALTS_OK;
}

static int query_json(const json_value_t *root, const char *path,
    json_value_t **out, query_error *error) {
    json_path_result_t *result = json_path_query(root, path);
    json_value_t *matches;
    size_t index;
    int status = SALTS_OK;
    if (!result) {
        const char *message = json_path_get_error();
        return fail(error, SALTS_EINVAL, message ? message : "Invalid JSONPath expression");
    }
    matches = json_create_array();
    if (!matches) status = fail(error, SALTS_ENOMEM, "Failed to allocate query result");
    for (index = 0; status == SALTS_OK && index < json_path_result_size(result); ++index) {
        const json_value_t *value = json_path_result_get(result, index);
        status = push(error, matches, value ? json_clone(value) : json_create_null());
    }
    json_path_result_free(result);
    if (status != SALTS_OK) { json_free(matches); return status; }
    return collapse(error, matches, out);
}

static int query_yaml(const char *document, size_t size, const char *path,
    json_value_t **out, query_error *error) {
    cyaml_error_t diagnostic = {0};
    cyaml_doc_t *yaml = cyaml_parse(document, size, NULL, &diagnostic);
    cyaml_path_result_t result;
    json_value_t *matches;
    uint32_t index;
    int status = SALTS_OK;
    if (!yaml) return fail(error, SALTS_EINVAL,
        diagnostic.msg[0] ? diagnostic.msg : "Invalid YAML input");
    result = cyaml_path_query(yaml, NULL, path);
    if (result.error) {
        status = fail(error, SALTS_EINVAL, result.error);
        cyaml_path_result_free(&result);
        cyaml_free(yaml);
        return status;
    }
    matches = json_create_array();
    if (!matches) status = fail(error, SALTS_ENOMEM, "Failed to allocate query result");
    for (index = 0; status == SALTS_OK && index < cyaml_path_count(&result); ++index) {
        json_value_t *value = json_value_from_cyaml_node(yaml, cyaml_path_get(&result, index));
        if (!value) status = fail(error, SALTS_EIO, "Failed to convert YPATH result to JSON");
        else status = push(error, matches, value);
    }
    cyaml_path_result_free(&result);
    cyaml_free(yaml);
    if (status != SALTS_OK) { json_free(matches); return status; }
    return collapse(error, matches, out);
}

static int query_xml(const char *document, size_t size, const char *path,
    json_value_t **out, query_error *error) {
    salts_xml_document xml = {0};
    salts_xml_diagnostic diagnostic = {0};
    salts_xml_node_list result = {0};
    qvm_diagnostic_t query_diagnostic = {0};
    json_value_t *matches = NULL;
    int status = SALTS_OK;
    size_t index;
    if (salts_xml_parse(&xml, document, size, NULL, &diagnostic) != SALTS_XML_OK)
        return fail(error, SALTS_EINVAL, diagnostic.message[0] ? diagnostic.message : "Invalid XML input");
    if (!path[0]) { status = fail(error, SALTS_EINVAL, "XPath expression must not be empty"); goto cleanup; }
    if (salts_xml_document_xpath_query(&xml, path, &result, NULL, &query_diagnostic) != QVM_STATUS_OK) {
        status = fail(error, SALTS_EINVAL,
            query_diagnostic.message ? query_diagnostic.message : "XPath evaluation failed");
        goto cleanup;
    }
    matches = json_create_array();
    if (!matches) status = fail(error, SALTS_ENOMEM, "Failed to allocate query result");
    for (index = 0; status == SALTS_OK && index < salts_xml_node_list_size(&result); ++index) {
        salts_xml_node node = salts_xml_node_list_at(&result, index);
        salts_xml_string_view text = salts_xml_node_text_view(node);
        if (text.data) status = push(error, matches, json_create_string_n(text.data, text.size));
        else {
            size_t serialized_size = 0;
            char *serialized = salts_xml_node_serialize(node, &serialized_size);
            if (!serialized) status = fail(error, SALTS_EIO, "Failed to serialize XPath result");
            else {
                status = push(error, matches, json_create_string_n(serialized, serialized_size));
                salts_xml_owned_string_free(serialized);
            }
        }
    }
cleanup:
    salts_xml_node_list_destroy(&result);
    salts_xml_document_destroy(&xml);
    if (status != SALTS_OK) { json_free(matches); return status; }
    return collapse(error, matches, out);
}

static int query_csv(const char *document, size_t size, const char *path,
    json_value_t **out, query_error *error) {
    csv_doc_t *csv = csv_parse(document, size);
    dsv_filter_t *filter;
    json_value_t *rows = NULL;
    int status = SALTS_OK;
    size_t row, column;
    if (!csv) {
        const char *message = csv_get_error();
        return fail(error, SALTS_EINVAL, message ? message : "Invalid CSV input");
    }
    filter = dsv_filter_create(csv, 0);
    if (!filter) { csv_free(csv); return fail(error, SALTS_EIO, "Failed to create CSVPath filter"); }
    if (!path[0] || !dsv_filter_compile(filter, path)) {
        const char *message = dsv_filter_error(filter);
        status = fail(error, SALTS_EINVAL, !path[0] ? "CSVPath expression must not be empty" :
            (message ? message : "Invalid CSVPath expression"));
        goto cleanup;
    }
    rows = json_create_array();
    if (!rows) status = fail(error, SALTS_ENOMEM, "Failed to allocate query result");
    for (row = 1; status == SALTS_OK && row < csv_row_count(csv); ++row) {
        int match = dsv_filter_check_row(filter, row);
        json_value_t *record;
        if (match < 0) {
            const char *message = dsv_filter_error(filter);
            status = fail(error, SALTS_EIO, message ? message : "CSVPath evaluation failed");
            break;
        }
        if (!match) continue;
        record = json_create_object();
        if (!record) { status = fail(error, SALTS_ENOMEM, "Failed to allocate CSV row"); break; }
        for (column = 0; column < csv_column_count(csv); ++column) {
            const char *name = csv_get(csv, 0, column);
            const char *text = csv_get(csv, row, column);
            json_value_t *value = json_create_string(text ? text : "");
            if (!value || !json_object_add_checked(record, name ? name : "", value)) {
                json_free(value);
                status = fail(error, SALTS_ENOMEM, "Failed to set CSV row field");
                break;
            }
        }
        if (status == SALTS_OK) status = push(error, rows, record);
        else json_free(record);
    }
cleanup:
    dsv_filter_destroy(filter);
    csv_free(csv);
    if (status != SALTS_OK) { json_free(rows); return status; }
    *out = rows;
    return SALTS_OK;
}

/* Build the provider's terminated expression with the same defaults/prefixes
 * as the former C++ implementation. Do not trim or rewrite query syntax. */
static tstr expression(praktor_document_format format, const char *path, size_t size) {
    const char *prefix = "";
    tstr result, grown;
    if (!size) return tstr_dup(format == PRAKTOR_DOCUMENT_JSON ? "$" :
        (format == PRAKTOR_DOCUMENT_YAML ? "/" : ""));
    if (format == PRAKTOR_DOCUMENT_JSON && path[0] != '$') prefix = path[0] == '[' ? "$" : "$.";
    result = tstr_dup(prefix);
    if (!result) return NULL;
    grown = tstr_cat_len(result, path, size);
    if (!grown) { tstr_free(result); return NULL; }
    return grown;
}

static int arguments(const char *path, size_t size, json_value_t **out, query_error *error) {
    if (error->text && error->capacity) error->text[0] = '\0';
    if ((!path && size) || size > SIZE_MAX - 3 || !out || (!error->text && error->capacity))
        return fail(error, SALTS_EINVAL, "Invalid document query arguments");
    return SALTS_OK;
}

int praktor_document_query_json(const json_value_t *root, const char *path,
    size_t path_size, json_value_t **out, char *error, size_t error_capacity) {
    query_error diagnostic = {error, error_capacity};
    tstr query;
    int status = arguments(path, path_size, out, &diagnostic);
    if (status != SALTS_OK) return status;
    query = expression(PRAKTOR_DOCUMENT_JSON, path, path_size);
    if (!query) return fail(&diagnostic, SALTS_ENOMEM, "Failed to allocate query expression");
    status = query_json(root, query, out, &diagnostic);
    tstr_free(query);
    return status;
}

int praktor_document_query(praktor_document_format format, const char *document,
    size_t document_size, const char *path, size_t path_size, json_value_t **out,
    char *error, size_t error_capacity) {
    query_error diagnostic = {error, error_capacity};
    tstr query;
    int status = arguments(path, path_size, out, &diagnostic);
    if (status != SALTS_OK) return status;
    if (!document && document_size) return fail(&diagnostic, SALTS_EINVAL, "Invalid document query arguments");
    if (format < PRAKTOR_DOCUMENT_JSON || format > PRAKTOR_DOCUMENT_CSV)
        return fail(&diagnostic, SALTS_EINVAL, "Unsupported structured document format");
    query = expression(format, path, path_size);
    if (!query) return fail(&diagnostic, SALTS_ENOMEM, "Failed to allocate query expression");
    if (!document) document = "";
    switch (format) {
    case PRAKTOR_DOCUMENT_JSON: {
        json_value_t *root = json_parse(document, document_size);
        if (!root) status = fail(&diagnostic, SALTS_EINVAL, "Invalid JSON input");
        else { status = query_json(root, query, out, &diagnostic); json_free(root); }
        break;
    }
    case PRAKTOR_DOCUMENT_YAML: status = query_yaml(document, document_size, query, out, &diagnostic); break;
    case PRAKTOR_DOCUMENT_XML: status = query_xml(document, document_size, query, out, &diagnostic); break;
    case PRAKTOR_DOCUMENT_CSV: status = query_csv(document, document_size, query, out, &diagnostic); break;
    }
    tstr_free(query);
    return status;
}
