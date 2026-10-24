#ifndef PRAKTOR_YAML_WORKFLOW_METADATA_H
#define PRAKTOR_YAML_WORKFLOW_METADATA_H
#include "yml/yaml_value.h"
#ifdef __cplusplus
extern "C" {
#endif
/* C-owned projection of workflow root scalar metadata. Validates the root key
 * whitelist, input_policy, and defaults map. Failure leaves *out unchanged. */
int praktor_yaml_workflow_metadata_create(praktor_yaml_node node,
    json_value_t **out, praktor_yaml_field_error *error);
#ifdef __cplusplus
}
#endif
#endif
