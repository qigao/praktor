#ifndef PRAKTOR_YAML_FIELDS_H
#define PRAKTOR_YAML_FIELDS_H

#include "yml/yaml_document.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API, outside the installed SDK ABI. Field readers retain
 * the task grammar's decoded 10 KiB limit and key-preferred source locations. */
#define PRAKTOR_YAML_MAX_PARAMETER_LENGTH (10u * 1024u)

/* Unique diagnostic owner: initialize to {0}, never copy, and dispose after use.
 * Each reader disposes the previous diagnostic before reporting a new result.
 * Text is length-aware (including caller-supplied NULs), without truncation.
 * On diagnostic allocation failure text is NULL and origin is ALLOCATION.
 * NULL suppresses diagnostics. Error storage must not alias inputs or outputs.
 * All failed value outputs remain untouched. */
typedef struct praktor_yaml_field_error {
    tstr text;
    praktor_yaml_failure origin;
} praktor_yaml_field_error;
void praktor_yaml_field_error_dispose(praktor_yaml_field_error *error);

/* message is the borrowed shape/type error, e.g. "service.name must be a string".
 * Successful text is an independent tstr owner, freed by the caller. Decode
 * failures retain the document layer's provider/allocation classification. */
int praktor_yaml_field_scalar(praktor_yaml_node node, vstr message, tstr *out,
    praktor_yaml_field_error *error);
int praktor_yaml_field_string(praktor_yaml_node node, vstr message, tstr *out,
    praktor_yaml_field_error *error);

typedef enum praktor_yaml_boolean_policy {
    PRAKTOR_YAML_BOOLEAN_STRICT = 0,
    PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE
} praktor_yaml_boolean_policy;
/* Strict: case-insensitive true/yes/1 and false/no/0, without trimming.
 * Force-terminate additionally accepts on/off, provider semantic integer 0/1,
 * then the historic signed/base-prefixed/underscore integer compatibility rule.
 * field_name is borrowed and appears verbatim in the diagnostic. */
int praktor_yaml_field_boolean(praktor_yaml_node node, vstr field_name,
    praktor_yaml_boolean_policy policy, bool *out, praktor_yaml_field_error *error);

/* Pure compatibility predicate: optional sign, lowercase 0b/0o/0x prefix,
 * underscores after the first decimal digit or prefix, checked uint64 parsing,
 * and only nonnegative 0/1 (including -0). No whitespace or case folding.
 * ENOENT means unsupported syntax/value; EINVAL means invalid view/output. */
int praktor_yaml_force_terminate_integer(vstr value, bool *out);

#ifdef __cplusplus
}
#endif
#endif
