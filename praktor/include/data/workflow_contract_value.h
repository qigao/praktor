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

#ifdef __cplusplus
}
#endif
#endif
