#ifndef PRAKTOR_YAML_WORKFLOW_METADATA_H
#define PRAKTOR_YAML_WORKFLOW_METADATA_H
#include "yml/yaml_value.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PRAKTOR_WORKFLOW_MAX_SERVICES 16u
/* C-owned projection of workflow root scalar metadata. Validates the root key
 * whitelist and defaults map. Failure leaves *out unchanged. */
int praktor_yaml_workflow_metadata_create(praktor_yaml_node node,
    json_value_t **out, praktor_yaml_field_error *error);
/* Overlay optional inherited environment, then normalized document defaults,
 * onto initialized per-command defaults. Does not read or mutate environment.
 * All present values must be valid, including those overridden later. On
 * failure both values stay unchanged; error borrows a static diagnostic. */
int praktor_yaml_workflow_resolve_defaults(const json_value_t *metadata,
    const char *environment_timeout, const char *environment_stream,
    int *timeout_ms, bool *stream_output, const char **error);
#ifdef __cplusplus
}
#endif
#endif
