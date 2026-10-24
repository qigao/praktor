#ifndef PRAKTOR_ORCH_YAML_H
#define PRAKTOR_ORCH_YAML_H
#include "yml/orch_tree.h"
#include "yml/yaml_document.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Qualification-only recursive YAML -> canonical CSTL-backed AST.
 * Supports legacy named scalar/mapping/sequence, nested flat if/while/switch,
 * set_variable and flat timeout/repeat/delay with child. Retired named forms
 * are rejected. This parser does not execute nodes or replace production C++.
 * Input YAML is borrowed; the returned tree is uniquely owned and independent
 * of the document. Failure never publishes a provisional output. */
int praktor_orch_from_named_yaml(praktor_yaml_node node, praktor_orch_tree **out,
    char *error, size_t error_capacity);
/* Task-level flat forms also support single-child decorators and run_once. */
int praktor_orch_from_flat_yaml(praktor_yaml_node node, const char *node_type,
    praktor_orch_tree **out, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
#endif
