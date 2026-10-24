#ifndef PRAKTOR_YAML_CONTRACT_H
#define PRAKTOR_YAML_CONTRACT_H
#include "yml/yaml_value.h"
#include "data/workflow_contract_value.h"
#ifdef __cplusplus
extern "C" {
#endif
/* C-owned JSON projection for workflow inputs/outputs. YAML traversal, field
 * validation, and structured-value conversion happen in C. First duplicate
 * field name wins, matching the existing map::emplace contract. */
int praktor_yaml_contract_fields_create(praktor_yaml_node node, bool output,
    json_value_t **out, praktor_yaml_field_error *error);
#ifdef __cplusplus
}
#endif
#endif
