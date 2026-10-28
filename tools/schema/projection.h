#ifndef PRAKTOR_SCHEMA_PROJECTION_H
#define PRAKTOR_SCHEMA_PROJECTION_H
#include <json_parser.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted repository IDL + editor policy -> generated editor schema.
 * Inputs are borrowed and never modified. On success *out owns a JSON root
 * released with json_free; on failure *out is NULL and error receives a
 * bounded diagnostic when supplied. Return SALTS_OK / EINVAL / ENOMEM.
 * Projection and overlay merge nesting is limited to 128; record references
 * stay references, never recursively expand schemas. No WorkflowValue or C++.
 */
int praktor_schema_project(const char *idl, size_t idl_size,
    const json_value_t *editor, json_value_t **out, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
