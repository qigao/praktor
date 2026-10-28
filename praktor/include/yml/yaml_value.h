#ifndef PRAKTOR_YAML_VALUE_H
#define PRAKTOR_YAML_VALUE_H
#include "yml/yaml_fields.h"
#include <json_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal migration API, not installed SDK ABI. Convert contract defaults,
 * enums and HostTool arguments into a unique Salts JSON root. Input nodes are
 * borrowed; output survives document destruction. Failure frees all provisional
 * values and leaves *out untouched. Error ownership follows yaml_fields.h.
 * Independent documents/results can be used concurrently; no shared mutation.
 *
 * Preserve provider string classification, decoded-NUL truncation, signed
 * decimal int64 then strtod conversion, and last decoded duplicate key wins.
 * YAML null nodes retain the previous contract-value refusal. Mapping child
 * values are converted before keys, preserving C++17 RHS error precedence.
 * No 10 KiB scalar field limit is imposed on structured contract values.
 *
 * Root depth is 1; each alias expansion counts toward depth and the total value
 * budget. Excess fails closed (EINVAL) rather than recursing without a bound.
 * These limits bound traversal, not provider DOM/string allocation bytes. */
#define PRAKTOR_YAML_VALUE_MAX_DEPTH 128u
#define PRAKTOR_YAML_VALUE_MAX_NODES 16384u
int praktor_yaml_value(praktor_yaml_node node, json_value_t **out,
    praktor_yaml_field_error *error);

#ifdef __cplusplus
}
#endif
#endif
