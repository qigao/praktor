#ifndef PRAKTOR_TOOL_ACTION_H
#define PRAKTOR_TOOL_ACTION_H
#include "actions/tree_adapter.h"
#include "providers/process.h"
#include <json_parser.h>
/* Per-activation bridge; definitions and inputs are borrowed from immutable
 * owners through close. The native host remains the workflow value authority. */
typedef struct praktor_tool_action {
  praktor_bt_host native;
  praktor_execution_service service;
  praktor_service_job job;
  const json_value_t *definition, *inputs;
  char error[256];
} praktor_tool_action;
int praktor_tool_admit(const json_value_t *definition, const json_value_t *inputs, char *error,
                       size_t capacity);
void praktor_tool_action_bind(praktor_tool_action *action, praktor_execution_service service,
                              const json_value_t *definition, const json_value_t *inputs,
                              praktor_bt_host *hooks);
int praktor_tool_action_close(praktor_tool_action *action);
#endif
