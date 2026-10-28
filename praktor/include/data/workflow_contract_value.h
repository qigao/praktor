#ifndef PRAKTOR_WORKFLOW_CONTRACT_VALUE_H
#define PRAKTOR_WORKFLOW_CONTRACT_VALUE_H

#include "data/workflow_value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* C11 workflow-contract value classification. Integer retains the established
 * WorkflowValue lexical rule: integer JSON tokens only, with exact signed or
 * unsigned 64-bit range; integral-looking floats such as 1.0 are not integers. */
bool praktor_contract_value_matches_type(const json_value_t *value,
                                         const char *type);

/* Enum equality matches the former WorkflowValue::to_string() comparison:
 * serialized JSON bytes, not coercive numeric equality. A null or empty enum
 * array means no restriction. On error, *out is unchanged. */
int praktor_contract_value_in_enum(const json_value_t *value,
                                   const json_value_t *enum_values,
                                   bool *out);

/* Build a schema fragment with owned JSON children. Inputs are borrowed;
 * output is uniquely owned. On failure, *out is NULL. */
int praktor_contract_schema_field(const char *type, const char *description,
                                  bool has_description,
                                  const json_value_t *default_value,
                                  const json_value_t *enum_values,
                                  json_value_t **out);

#ifdef __cplusplus
}
#endif
#endif
