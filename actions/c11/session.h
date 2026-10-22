#ifndef PRAKTOR_C11_SESSION_H
#define PRAKTOR_C11_SESSION_H
#include "tree_adapter.h"
struct cflow_process_retainer;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_c11_session { void *impl; } praktor_c11_session;
typedef enum praktor_c11_terminal_action {
  PRAKTOR_C11_TERMINAL_RUNTIME = 0,
  PRAKTOR_C11_TERMINAL_SUCCESS = 1,
  PRAKTOR_C11_TERMINAL_FAILURE = 2,
  PRAKTOR_C11_TERMINAL_SWITCH_FAILURE = 3
} praktor_c11_terminal_action;
/* Preorder compiled input. All strings/arrays are copied during init.
 * Optional text_sizes preserves binary string ports; NULL means C strings. */
typedef struct praktor_c11_session_node {
  praktor_c11_bt_node_desc tree;
  const char *action;
  const praktor_c11_leaf_parameter *parameters;
  size_t parameter_count;
  const size_t *text_sizes;
  praktor_c11_terminal_action terminal;
} praktor_c11_session_node;
typedef struct praktor_c11_session_config {
  salts_bt_tree_config tree;
  size_t max_plan_bytes, max_value_bytes, max_state_bytes, max_parameters_per_node;
  uint32_t shell_close_budget_ms;
  struct cflow_process_retainer *retainer;
  praktor_c11_bt_host host;
  /* Explicit compatibility bridge; independent C callers leave these zero.
   * No C++ object or execution implementation is linked by the C core. */
  praktor_c11_bt_host compatibility_values;
  int (*compatibility_tick)(void *user, size_t node, salts_bt_node_status *status);
  int (*compatibility_set_bytes)(void *user, const char *key, const char *data, size_t size);
  void *compatibility_user;
  int (*environment)(void *user, const char *const **entries);
  int (*emit)(void *user, const char *line, size_t size);
  void *process_user;
} praktor_c11_session_config;
void praktor_c11_session_config_init(praktor_c11_session_config *config);
/* Single driver; callbacks must not throw or reenter. Output remains empty on
 * failed init. Limits bound owned plan bytes, each value and aggregate cached
 * state. Scratch grows within max_value_bytes; JSON queue work is per visit. */
int praktor_c11_session_init(praktor_c11_session *session,
    const praktor_c11_session_config *config,
    const praktor_c11_session_node *nodes, size_t count);
int praktor_c11_session_tick(praktor_c11_session *session, salts_bt_node_status *status);
int praktor_c11_session_reset(praktor_c11_session *session);
int praktor_c11_session_cancel(praktor_c11_session *session);
int praktor_c11_session_validate(const praktor_c11_session *session);
/* Close before reserved retainer transfer. Failure retains the handle; retry
 * or keep driving its external recovery owner. Never drops live leases. */
int praktor_c11_session_destroy(praktor_c11_session *session);
const char *praktor_c11_session_error(const praktor_c11_session *session);
#ifdef __cplusplus
}
#endif
#endif
