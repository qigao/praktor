#include "actions/tool.h"
#include "actions/variable_reference.h"
#include <core/clock.h>
#include <core/fmt.h>
#include <core/tstr.h>
#include <string.h>
#include <threads/threads.h>

static int resolve_value(void *user, const char *text, size_t max_reference_bytes, tstr *out) {
  const praktor_tool_action *action = user;
  const vstr key = praktor_variable_reference(vstr_from_cstr(text));
  if (key.data) {
    size_t needed = 0u, actual = 0u;
    int rc = action->native.value_get(action->native.value_user, key.data, NULL, 0u, &needed);
    if ((rc != PRAKTOR_BT_VALUE_BUFFER_TOO_SMALL && rc != PRAKTOR_BT_VALUE_OK) ||
        needed == SIZE_MAX || needed > max_reference_bytes) {
      return SALTS_EINVAL;
    }
    tstr value = tstr_new_len(NULL, needed);
    if (!value) {
      return SALTS_ENOMEM;
    }
    rc = action->native.value_get(action->native.value_user, key.data, value, needed + 1u, &actual);
    if (rc != PRAKTOR_BT_VALUE_OK || actual != needed || memchr(value, '\0', actual)) {
      tstr_free(value);
      return SALTS_EINVAL;
    }
    *out = value;
    return SALTS_OK;
  }
  *out = tstr_dup(text);
  return *out ? SALTS_OK : SALTS_ENOMEM;
}
static int publish_result(praktor_tool_action *action,const praktor_execution_result *result,
    const praktor_leaf_request *request,salts_bt_node_status *status) {
  for (size_t i = 0u; i < result->output_count; ++i) {
    const praktor_execution_output *output = &result->outputs[i];
    if (action->native.value_set(action->native.value_user, output->key, output->text) !=
        PRAKTOR_BT_VALUE_OK)
      return SALTS_ENOBUFS;
    action->published_output = true;
  }
  if (!result->success && result->error && request->error && request->error_capacity)
    fmt_text(request->error, request->error_capacity, result->error);
  *status = result->success ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
  return SALTS_OK;
}
static int release_job(praktor_tool_action *action) {
  int rc = praktor_execution_service_release(&action->active->service,&action->job);
  if (rc == SALTS_OK) action->active = NULL;
  return rc;
}
static int tick(void *self,const praktor_leaf_request *request,salts_bt_node_status *status) {
  praktor_tool_action *action = self;
  if (action->halt_status != SALTS_OK) return action->halt_status;
  if (strcmp(request->action,"Tool"))
    return action->native.tick_typed_leaf(action->native.user,request,status);
  if (request->in_parallel_branch) return SALTS_ENOTSUP;
  if (!action->active) {
    action->published_output = false;
    size_t i = 0u;
    while (i < action->count && action->bindings[i].node_index != request->node_index) ++i;
    if (i == action->count) return SALTS_EINVAL;
    praktor_tool_binding *binding = &action->bindings[i];
    const praktor_execution_request inputs = {binding->inputs, resolve_value, action};
    int rc = praktor_execution_service_start(&binding->service, &inputs, &action->job);
    if (rc != SALTS_OK) {
      if (request->error && request->error_capacity)
        fmt(request->error,request->error_capacity,"tool start failed ({})",rc);
      return rc;
    }
    action->active = binding;
  }
  if (action->active->node_index != request->node_index) return SALTS_EBUSY;
  praktor_execution_result result = {0};
  int rc = praktor_execution_service_poll(&action->active->service,&action->job,&result);
  if (rc != SALTS_OK) return rc;
  if (!result.done) {
    *status = SALTS_BT_RUNNING;
    return SALTS_OK;
  }
  rc = publish_result(action,&result,request,status);
  action->terminal_status = result.terminal_status;
  const int released = release_job(action);
  if (released != SALTS_OK) return released;
  if (rc != SALTS_OK) return rc;
  return result.terminal_status == PRAKTOR_RUNTIME_OK ? SALTS_OK : SALTS_EIO;
}
static void halt(void *self, const praktor_leaf_request *request) {
  praktor_tool_action *action = self;
  if (strcmp(request->action, "Tool"))
    action->native.halt_typed_leaf(action->native.user, request);
  else if (action->active && action->active->node_index == request->node_index) {
    const int rc = praktor_tool_action_close(action);
    if (rc != SALTS_OK) action->halt_status = rc;
  }
}
void praktor_tool_action_bind(praktor_tool_action *action,praktor_tool_binding *bindings,
                              size_t count,
                              praktor_bt_host *hooks) {
  *action = (praktor_tool_action){.native=*hooks,.bindings=bindings,.count=count};
  hooks->user = action;
  hooks->tick_typed_leaf = tick;
  hooks->halt_typed_leaf = halt;
}
int praktor_tool_action_close(praktor_tool_action *action) {
  if (!action || !action->job.impl)
    return SALTS_OK;
  praktor_execution_service *service=&action->active->service;
  int rc = praktor_execution_service_cancel(service, &action->job);
  if (rc != SALTS_OK)
    return rc;
  uint64_t started = hrtime();
  for (;;) {
    praktor_execution_result result = {0};
    rc = praktor_execution_service_poll(service, &action->job, &result);
    if (rc != SALTS_OK)
      return rc;
    if (result.done)
      return release_job(action);
    if (hrtime() - started >= UINT64_C(5000000000))
      return SALTS_EBUSY;
    sleep_ms(1u);
  }
}
