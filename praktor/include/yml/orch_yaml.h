#ifndef PRAKTOR_ORCH_YAML_H
#define PRAKTOR_ORCH_YAML_H
#include "yml/orch_tree.h"
#include "yml/yaml_document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Research-only named-form YAML to canonical CSTL-backed AST.
 * Supports named scalar, named mapping with params/child, named sequence.
 * Does not accept flat if/while/switch or task metadata. No execution.
 * The input document is borrowed; the returned tree is uniquely owned.
 * Errors leave the output untouched. */
int praktor_orch_from_named_yaml(praktor_yaml_node node, praktor_orch_tree **out,
    char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
