#ifndef PRAKTOR_TOOL_ACTION_H
#define PRAKTOR_TOOL_ACTION_H
#include "actions/tree_adapter.h"
#include "providers/process.h"
#include <json_parser.h>
typedef struct praktor_tool_binding {
  size_t node_index;
  praktor_execution_service service;
  const json_value_t *definition;
  json_value_t *inputs;
} praktor_tool_binding;

/* One job per serial task driver. Bindings outlive close, and the native
 * host copies result values before the job is released. */
typedef struct praktor_tool_action {
  praktor_bt_host native;
  const praktor_tool_binding *bindings, *active;
  size_t count;
  praktor_service_job job;
  int halt_status;
  bool has_exit_code;
} praktor_tool_action;
int praktor_tool_admit(const json_value_t *definition, const json_value_t *inputs, char *error,
                       size_t capacity);
void praktor_tool_action_bind(praktor_tool_action *action,const praktor_tool_binding *bindings,
                              size_t count,
                              praktor_bt_host *hooks);
int praktor_tool_action_close(praktor_tool_action *action);
#endif
