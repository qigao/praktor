#ifndef PRAKTOR_STRUCTURED_DOCUMENT_QUERY_H
#define PRAKTOR_STRUCTURED_DOCUMENT_QUERY_H

#include <json_parser.h>
#include <salts/error_codes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum praktor_document_format {
    PRAKTOR_DOCUMENT_JSON,
    PRAKTOR_DOCUMENT_YAML,
    PRAKTOR_DOCUMENT_XML,
    PRAKTOR_DOCUMENT_CSV
} praktor_document_format;

/* Internal migration API, not part of the installed Praktor SDK.
 * Inputs are borrowed for the duration of the call. NULL with length zero is
 * an empty input. Documents use their exact byte length; paths retain the
 * existing C-provider convention (the first NUL terminates the expression).
 * JSON paths default to "$" and accept an omitted "$"/"$." prefix; YAML paths
 * default to "/". XML and CSV require nonempty paths.
 * JSON/YAML/XML return null for no matches, the value for one, an array for
 * many. CSV always returns an array of string-valued row objects.
 * On SALTS_OK, *out is an independent owned JSON tree (free with json_free).
 * On failure *out is untouched: SALTS_EINVAL for invalid input/query,
 * SALTS_ENOMEM for result/path allocation, SALTS_EIO for provider conversion
 * or evaluation failure. Provider parse/query failures retain their existing
 * classification and diagnostics. error may be NULL when error_capacity=0;
 * otherwise it receives a possibly truncated, NUL-terminated diagnostic.
 * Output/error storage must not alias inputs or each other. */
int praktor_document_query(praktor_document_format format,
    const char *document, size_t document_size, const char *path, size_t path_size,
    json_value_t **out, char *error, size_t error_capacity);

/* Query a borrowed JSON root without parsing or mutating it. The result remains
 * valid after the root is freed. Other rules are identical to the call above. */
int praktor_document_query_json(const json_value_t *root,
    const char *path, size_t path_size, json_value_t **out,
    char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif
#endif
