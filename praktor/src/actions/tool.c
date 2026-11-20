#include "actions/tool.h"
#include "actions/variable_reference.h"
#include <core/clock.h>
#include <core/fmt.h>
#include <core/tstr.h>
#include <string.h>
#include <threads/threads.h>

int praktor_tool_admit(const json_value_t *definition, const json_value_t *inputs, char *error,
                       size_t capacity) {
  const json_value_t *args = json_object_get(definition, "args");
  if (!args || json_type(args) != JSON_ARRAY || json_array_size(args) > PRAKTOR_PROCESS_MAX_ARGS ||
      (inputs && json_type(inputs) != JSON_OBJECT))
    return SALTS_EINVAL;
  for (size_t i = 0u; i < json_array_size(args); ++i) {
    const char *text = json_string(json_array_get(args, i));
    if (!text)
      return SALTS_EINVAL;
    const vstr argument = vstr_from_cstr(text);
    const vstr key = praktor_variable_reference(argument);
    if (praktor_retired_variable_reference(argument)) {
      if (error && capacity) fmt_text(error, capacity, "Use $name for a tool input reference");
      return SALTS_EINVAL;
    }
    if (key.data && !json_string(json_object_get_v(inputs, key))) {
      if (error && capacity)
        fmt(error, capacity, "missing tool input {}", text);
      return SALTS_EINVAL;
    }
  }
  /* No silently ignored invocation fields: every supplied input must bind an
   * entire argv entry. Repeated references deliberately reuse the same value. */
  for (size_t i = 0u; i < json_object_size(inputs); ++i) {
    const char *key = json_object_key(inputs, i);
    const json_value_t *item = json_object_value(inputs,i);
    const char *value = json_string(item);
    if (!key || !*key || strlen(key) != json_object_key_len(inputs,i) ||
        (value && strlen(value) != json_string_len(item))) {
      if (error && capacity) fmt_text(error, capacity, "Tool inputs must be NUL-free text");
      return SALTS_EINVAL;
    }
    if (!value || praktor_retired_variable_reference(vstr_from_cstr(value))) {
      if (error && capacity) fmt_text(error, capacity, "Use $name for a tool value reference");
      return SALTS_EINVAL;
    }
    bool used = false;
    for (size_t j = 0u; j < json_array_size(args); ++j) {
      const char *text = json_string(json_array_get(args, j));
      const vstr input = praktor_variable_reference(vstr_from_cstr(text));
      if (input.data && vstr_eq(input, vstr_from_cstr(key))) {
        used = true;
        break;
      }
    }
    if (!used) {
      if (error && capacity)
        fmt(error, capacity, "unused tool input '{}'", key);
      return SALTS_EINVAL;
    }
  }
  return SALTS_OK;
}
static int resolve_value(praktor_tool_action *action, const char *text, tstr *out) {
  const vstr key = praktor_variable_reference(vstr_from_cstr(text));
  if (key.data) {
    size_t needed = 0u, actual = 0u;
    int rc = action->native.value_get(action->native.value_user, key.data, NULL, 0u, &needed);
    if ((rc != PRAKTOR_BT_VALUE_BUFFER_TOO_SMALL && rc != PRAKTOR_BT_VALUE_OK) ||
        needed > PRAKTOR_PROCESS_MAX_OUTPUT) {
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
static int start_job(praktor_tool_action *action,const praktor_tool_binding *binding) {
  const json_value_t *templates = json_object_get(binding->definition, "args");
  size_t count = json_array_size(templates), bytes = 0u;
  tstr args[PRAKTOR_PROCESS_MAX_ARGS + 1u] = {0};
  int rc = SALTS_OK;
  for (size_t i = 0u; i < count; ++i) {
    const char *text = json_string(json_array_get(templates, i));
    const vstr key = praktor_variable_reference(vstr_from_cstr(text));
    if (key.data) {
      text = json_string(json_object_get_v(binding->inputs, key));
      if (!text) {
        rc = SALTS_EINVAL;
        break;
      }
      rc = resolve_value(action, text, &args[i]);
    } else {
      args[i] = tstr_dup(text);
      if (!args[i])
        rc = SALTS_ENOMEM;
    }
    if (rc != SALTS_OK)
      break;
    size_t n = tstr_len(args[i]);
    if (n >= PRAKTOR_PROCESS_MAX_ARG_BYTES - bytes) {
      rc = SALTS_ENOBUFS;
      break;
    }
    bytes += n + 1u;
  }
  if (rc == SALTS_OK) {
    praktor_process_request request = {(const char *const *)args, count};
    rc = praktor_execution_service_start(&binding->service, &request, &action->job);
    if (rc == SALTS_OK) action->active = binding;
  }
  for (size_t i = 0u; i < count; ++i)
    tstr_free(args[i]);
  return rc;
}
static int publish_result(praktor_tool_action *action,const praktor_service_result *result,
    const praktor_leaf_request *request,salts_bt_node_status *status) {
  char exit_code[32];
  int n = fmt(exit_code, sizeof exit_code, "{}", result->exit_code);
  if (n <= 0 || (size_t)n >= sizeof exit_code)
    return SALTS_EINVAL;
  if (action->native.value_set(action->native.value_user, "shell_exit_code", exit_code) !=
      PRAKTOR_BT_VALUE_OK)
    return SALTS_ENOBUFS;
  action->has_exit_code = true;
  if (!result->success) {
    if (request->error && request->error_capacity)
      fmt_text(request->error,request->error_capacity,result->error);
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (action->native.value_set(action->native.value_user, "shell_output", result->output) !=
          PRAKTOR_BT_VALUE_OK ||
      action->native.value_set(action->native.value_user, "shell_stderr", result->error_output) !=
          PRAKTOR_BT_VALUE_OK)
    return SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
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
    action->has_exit_code = false;
    size_t i = 0u;
    while (i < action->count && action->bindings[i].node_index != request->node_index) ++i;
    if (i == action->count) return SALTS_EINVAL;
    int rc = start_job(action,&action->bindings[i]);
    if (rc != SALTS_OK) {
      if (request->error && request->error_capacity)
        fmt(request->error,request->error_capacity,"tool start failed ({})",rc);
      return rc;
    }
  }
  if (action->active->node_index != request->node_index) return SALTS_EBUSY;
  praktor_service_result result = {0};
  int rc = praktor_execution_service_poll(&action->active->service,&action->job,&result);
  if (rc != SALTS_OK) return rc;
  if (!result.done) {
    *status = SALTS_BT_RUNNING;
    return SALTS_OK;
  }
  rc = publish_result(action,&result,request,status);
  const int released = release_job(action);
  return released == SALTS_OK ? rc : released;
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
void praktor_tool_action_bind(praktor_tool_action *action,const praktor_tool_binding *bindings,
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
  const praktor_execution_service *service=&action->active->service;
  int rc = praktor_execution_service_cancel(service, &action->job);
  if (rc != SALTS_OK)
    return rc;
  uint64_t started = hrtime();
  for (;;) {
    praktor_service_result result = {0};
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
