#ifndef PRAKTOR_YAML_TASK_METADATA_H
#define PRAKTOR_YAML_TASK_METADATA_H
#include "yml/yaml_task_fields.h"
#include "yml/yaml_value.h"
#ifdef __cplusplus
extern "C" {
#endif
int praktor_yaml_task_metadata_create(praktor_yaml_node node,
    json_value_t **out, praktor_yaml_field_error *error);
/* Parse the optional script after runner selection to preserve diagnostic order.
 * Success with absent script sets present=false and *out=NULL. Present script
 * returns an owned tstr. Outputs remain unchanged on failure. */
int praktor_yaml_task_script_read(praktor_yaml_node task_node, bool *present,
    tstr *out, praktor_yaml_field_error *error);
#ifdef __cplusplus
}
#endif
#endif
