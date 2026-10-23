#ifndef PRAKTOR_C11_PLAN_H
#define PRAKTOR_C11_PLAN_H
#include "session.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Compilation of one action/typed-parameter record, without owned storage. Output
 * borrows construction inputs until Session init clones them. A true prune
 * excludes descendants of statically terminal zero-budget controls. Callers
 * enforce global node/depth bounds and pass the original child count so arity
 * is checked BEFORE pruning. Session init validates final preorder topology.
 * Action/parameter names and parameter text are NUL-terminated; text_sizes
 * may preserve embedded NULs but must describe valid input storage.
 * Output is unchanged on error. Error text is optional and bounded. */
int praktor_c11_compile_node(const salts_bt_tree_config *limits,
    const char *action, const praktor_c11_leaf_parameter *parameters,
    size_t parameter_count, const size_t *text_sizes,
    size_t child_count, size_t parent_index,
    praktor_c11_session_node *output, bool *prune,
    char *error, size_t error_capacity);
/* Shared strict scalar parsing, including integral floating-point text.
 * Text requires a trailing NUL at text[size]; embedded NULs cannot hide a
 * suffix. Parameter text follows the same contract. */
int praktor_c11_parse_integer(const char *text, size_t size, int *output);
int praktor_c11_parameter_integer(const praktor_c11_leaf_parameter *parameter,
                                  size_t text_size, int *output);
#ifdef __cplusplus
}
#endif
#endif
