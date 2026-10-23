#ifndef PRAKTOR_YAML_DOCUMENT_H
#define PRAKTOR_YAML_DOCUMENT_H

#include <cyaml.h>
#include <salts/error_codes.h>
#include <tstr.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API, not an addition to the installed SDK ABI. A unique
 * document owner retains a copy of the source borrowed by the real YAML parser.
 * Transfer its pointer rather than copying it; destroy frees nodes before text.
 * Create leaves *out untouched on failure. Input lengths must fit YAML spans. */
typedef struct praktor_yaml_document praktor_yaml_document;
int praktor_yaml_document_create(vstr source, praktor_yaml_document **out,
    char *error, size_t capacity);
void praktor_yaml_document_destroy(praktor_yaml_document *document);

/* Immutable borrowed handles, valid only while their document owner lives.
 * Moving the owner pointer preserves handles. Independent documents may be used
 * concurrently; this layer never mutates provider nodes or shared state. */
typedef struct praktor_yaml_node {
    const cyaml_doc_t *document;
    cyaml_node_t *node;
    cyaml_node_t *key_node;
} praktor_yaml_node;

#define PRAKTOR_YAML_MAX_ALIAS_HOPS 128u
#define PRAKTOR_YAML_LOCATION_DEPTH 32u

praktor_yaml_node praktor_yaml_root(const praktor_yaml_document *document);
bool praktor_yaml_valid(praktor_yaml_node node);
bool praktor_yaml_is_map(praktor_yaml_node node);
bool praktor_yaml_is_seq(praktor_yaml_node node);
bool praktor_yaml_has_val(praktor_yaml_node node);
bool praktor_yaml_is_string_scalar(praktor_yaml_node node);
size_t praktor_yaml_count(praktor_yaml_node node);
praktor_yaml_node praktor_yaml_at(praktor_yaml_node node, size_t index);
/* C-string key lookup deliberately retains cyaml_get's raw-span semantics and
 * the legacy decoded-key location search. Missing keys return ENOENT; output
 * is untouched on failure. has() uses the provider without decoding/allocating. */
bool praktor_yaml_has(praktor_yaml_node node, const char *key);
int praktor_yaml_get(praktor_yaml_node node, const char *key,
    praktor_yaml_node *out, char *error, size_t capacity);

/* Successful text outputs are unique tstr owners released with tstr_free().
 * Outputs are untouched on failure; previous output owners remain the caller's
 * responsibility. Scalar decoding retains legacy truncation at decoded NUL,
 * because the supported public provider API returns a C string without length.
 * Location text is empty or " at line N, column N", preferring the mapping key. */
int praktor_yaml_scalar(praktor_yaml_node node, tstr *out, char *error, size_t capacity);
int praktor_yaml_key(praktor_yaml_node node, tstr *out, char *error, size_t capacity);
int praktor_yaml_location(praktor_yaml_node node, tstr *out);
/* Strict decoded decimal int (no +, spaces, bases, separators or fractions),
 * including quoted integers, matching the prior from_chars<int> contract. */
int praktor_yaml_integer(praktor_yaml_node node, int *out, char *error, size_t capacity);
/* Optional semantic integer 0/1 only; ENOENT for other values/types. */
int praktor_yaml_bool_integer(praktor_yaml_node node, bool *out);

#ifdef __cplusplus
}
#endif
#endif
