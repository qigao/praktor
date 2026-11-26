#ifndef PRAKTOR_TOOL_ACTION_H
#define PRAKTOR_TOOL_ACTION_H
#include "actions/tree_adapter.h"
#include "providers/execution.h"
typedef struct praktor_tool_binding {
  size_t node_index;
  praktor_execution_service service;
  json_value_t *inputs;
  const char *output_key;
} praktor_tool_binding;

/* One job per serial task driver. Bindings outlive close, and the native
 * host copies result values before the job is released. */
typedef struct praktor_tool_action {
  praktor_bt_host native;
  praktor_tool_binding *bindings, *active;
  size_t count;
  praktor_service_job job;
  int halt_status;
  praktor_runtime_status terminal_status;
  bool published_output;
} praktor_tool_action;
void praktor_tool_action_bind(praktor_tool_action *action,praktor_tool_binding *bindings,
                              size_t count,
                              praktor_bt_host *hooks);
int praktor_tool_action_close(praktor_tool_action *action);
#endif
