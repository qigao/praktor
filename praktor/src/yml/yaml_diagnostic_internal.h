#ifndef PRAKTOR_YAML_DIAGNOSTIC_INTERNAL_H
#define PRAKTOR_YAML_DIAGNOSTIC_INTERNAL_H
#include "yml/yaml_fields.h"

/* Shared C diagnostic mechanics, not installed API. Callers clear the unique
 * error owner on entry. fail consumes text even when diagnostics are suppressed;
 * NULL text denotes allocation failure. decode retains provider error origins. */
int praktor_yaml_field_fail(praktor_yaml_field_error *error, int status,
    praktor_yaml_failure origin, tstr text);
int praktor_yaml_field_fail_at(praktor_yaml_node node, vstr message,
    praktor_yaml_field_error *error);
int praktor_yaml_field_decode(praktor_yaml_node node, bool key, tstr *out,
    praktor_yaml_field_error *error);
/* Lookup retains the temporary facade's missing-key -> empty handle behavior. */
int praktor_yaml_field_get(praktor_yaml_node node, const char *key,
    praktor_yaml_node *out, praktor_yaml_field_error *error);
int praktor_yaml_field_integer(praktor_yaml_node node, int *out,
    praktor_yaml_field_error *error);
#endif
