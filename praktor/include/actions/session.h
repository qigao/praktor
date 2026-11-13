#ifndef PRAKTOR_SESSION_H
#define PRAKTOR_SESSION_H
#include "actions/tree_adapter.h"
struct praktor_process_retainer;
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_session { void *impl; } praktor_session;
typedef enum praktor_terminal_action {
  PRAKTOR_TERMINAL_RUNTIME = 0,
  PRAKTOR_TERMINAL_SUCCESS = 1,
  PRAKTOR_TERMINAL_FAILURE = 2,
  PRAKTOR_TERMINAL_SWITCH_FAILURE = 3
} praktor_terminal_action;
/* Preorder compiled input. All strings/arrays are copied during init.
 * Optional text_sizes preserves binary string ports; NULL means C strings. */
typedef struct praktor_session_node {
  praktor_bt_node_desc tree;
  const char *action;
  const praktor_leaf_parameter *parameters;
  size_t parameter_count;
  const size_t *text_sizes;
  praktor_terminal_action terminal;
} praktor_session_node;
typedef struct praktor_session_config {
  salts_bt_tree_config tree;
  size_t max_plan_bytes, max_value_bytes, max_state_bytes, max_parameters_per_node;
  uint32_t shell_close_budget_ms;
  struct praktor_process_retainer *retainer;
  praktor_bt_host host;
  int (*environment)(void *user, const char *const **entries);
  int (*emit)(void *user, const char *line, size_t size);
  void *process_user;
} praktor_session_config;
void praktor_session_config_init(praktor_session_config *config);
/* Single driver; callbacks must not throw or reenter. Output remains empty on
 * failed init. Limits bound owned plan bytes, each value and aggregate cached
 * state. Scratch grows within max_value_bytes; JSON queue work is per visit. */
int praktor_session_init(praktor_session *session,
    const praktor_session_config *config,
    const praktor_session_node *nodes, size_t count);
int praktor_session_tick(praktor_session *session, salts_bt_node_status *status);
int praktor_session_reset(praktor_session *session);
int praktor_session_cancel(praktor_session *session);
int praktor_session_validate(const praktor_session *session);
/* Close before reserved retainer transfer. Failure retains the handle; retry
 * or keep driving its external recovery owner. Never drops live leases. */
int praktor_session_destroy(praktor_session *session);
const char *praktor_session_error(const praktor_session *session);
#ifdef __cplusplus
}
#endif
#endif
