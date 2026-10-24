#ifndef PRAKTOR_ORCH_TREE_H
#define PRAKTOR_ORCH_TREE_H
#include "yml/command_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <salts/error_codes.h>
#include <vstr.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Internal migration API, outside the installed SDK ABI. Catalog views borrow
 * immutable static data. Lookup ignores underscores and folds unsigned bytes
 * using the same C locale policy as the legacy parser; binary names retain
 * lengths. Failed lookup leaves *out untouched. No allocation or registry Host. */
typedef enum praktor_orch_type {
#define PRAKTOR_ORCH_NODE(id, name, control, children, required, optional) PRAKTOR_ORCH_##id,
#include "yml/orch_nodes.def"
#undef PRAKTOR_ORCH_NODE
    PRAKTOR_ORCH_TYPE_COUNT
} praktor_orch_type;
typedef struct praktor_orch_spec {
    const char *name;
    bool control, allows_children;
    const char *required[3], *optional[7]; /* NULL terminated, immutable. */
} praktor_orch_spec;
const praktor_orch_spec *praktor_orch_spec_for(praktor_orch_type type);
int praktor_orch_find_type(vstr name, praktor_orch_type *out);

/* Borrowed constructor input. Only the selected command form and highest
 * priority present postprocessor are read: regex > JSON > lines > key/value >
 * JSON output fallback. Defaults/validation of YAML remain with yaml_action_params;
 * this constructor also preserves manually constructed empty scalar/list values,
 * empty output keys and binary text. No environment/working-dir policy is added. */
typedef struct praktor_orch_command {
    bool sequence;
    vstr command;
    const vstr *commands;
    size_t command_count;
    praktor_yaml_output_format output_format;
    struct { bool present; vstr pattern, output_key; int capture_group; } regex;
    struct { bool present; vstr path, output_key; } json;
    struct { bool present; vstr filter, output_key; } lines;
    struct { bool present; vstr delimiter, line_separator, output_key; } keyvalue;
} praktor_orch_command;
#define PRAKTOR_ORCH_MAX_COMMANDS 4096u
#define PRAKTOR_ORCH_MAX_NODES (PRAKTOR_ORCH_MAX_COMMANDS + 2u)
#define PRAKTOR_ORCH_MAX_PARAMS 8u
#define PRAKTOR_ORCH_MAX_BYTES (1024u * 1024u)
typedef struct praktor_orch_tree praktor_orch_tree;
typedef struct praktor_orch_node praktor_orch_node;
/* Unique owner. Copies all parameter values, retaining no input/document/runtime
 * references. Publish only on success; failure leaves *out untouched and frees
 * provisional state. Transfer the pointer, never copy ownership; destroy(NULL)
 * is valid. Immutable views survive input destruction and pointer transfer, and
 * expire on owner destruction. Independent owners may be used concurrently;
 * destruction must not overlap a read of that owner. Cleanup is iterative.
 *
 * Admission: 4096 commands, 4098 nodes, eight parameters per node and 1 MiB owned
 * parameter text including terminators/generated capture-group/default values.
 * Keys and node names borrow the catalog/static literals. Exceeding a bound
 * fails closed; no truncation/retry. Container/allocator overhead is outside the
 * text budget. Returns SALTS_OK, EINVAL (input/capacity), ENOMEM (owned allocation).
 * Optional diagnostic buffer is borrowed and NUL-terminated when capacity > 0;
 * NULL requires zero capacity. Error storage must be disjoint from input/output.
 * Example: from_command(&input, &tree, error, sizeof error); consume root/child/
 * parameter views, then destroy(tree). This constructs a tree, not an executor. */
int praktor_orch_from_command(const praktor_orch_command *input,
    praktor_orch_tree **out, char *error, size_t error_capacity);
/* Ordered n-ary AST from a flat parent-before-child sequence. This uses the
 * existing Salts CSTL vector-backed Praktor owner, not the indexed BTree.
 * First node is root with parent_index=SIZE_MAX; all later parents have a
 * smaller index. Input keys and values are borrowed, output is uniquely owned.
 * Keys are canonicalized against static catalog literals; all values copied.
 * Required params, structural child counts and admission budgets are checked.
 * Failure destroys provisional nodes and leaves *out unchanged. */
#define PRAKTOR_ORCH_ROOT_PARENT SIZE_MAX
typedef struct praktor_orch_input_param { vstr key, value; } praktor_orch_input_param;
typedef struct praktor_orch_flat_node {
    praktor_orch_type type;
    size_t parent_index;
    const praktor_orch_input_param *params;
    size_t param_count;
} praktor_orch_flat_node;
int praktor_orch_from_flat_nodes(const praktor_orch_flat_node *nodes, size_t count,
    praktor_orch_tree **out, char *error, size_t error_capacity);
void praktor_orch_tree_destroy(praktor_orch_tree *tree);
const praktor_orch_node *praktor_orch_tree_root(const praktor_orch_tree *tree);
size_t praktor_orch_tree_size(const praktor_orch_tree *tree);
praktor_orch_type praktor_orch_node_type(const praktor_orch_node *node); /* NULL -> TYPE_COUNT. */
size_t praktor_orch_child_count(const praktor_orch_node *node);
size_t praktor_orch_param_count(const praktor_orch_node *node);
/* Allocation-free, strong output on EINVAL/ENOENT. Parameters iterate in original
 * insertion order, preserving the legacy unordered-map projection's iteration. */
typedef struct praktor_orch_param { vstr key, value; } praktor_orch_param;
int praktor_orch_child_at(const praktor_orch_node *node, size_t index, const praktor_orch_node **out);
int praktor_orch_param_at(const praktor_orch_node *node, size_t index, praktor_orch_param *out);
#ifdef __cplusplus
}
#endif
#endif
