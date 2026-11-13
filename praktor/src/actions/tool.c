#include "actions/tool.h"
#include <core/clock.h>
#include <core/fmt.h>
#include <core/tstr.h>
#include <string.h>
#include <threads/threads.h>

static bool reference(const char *text) {
  size_t n = strlen(text);
  return n >= 3u && text[0] == '{' && text[n - 1u] == '}';
}
static const json_value_t *input_for(const json_value_t *inputs, const char *text) {
  size_t n = strlen(text);
  return json_object_get_v(inputs, (vstr){text + 1u, n - 2u});
}
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
    if (reference(text) && !json_string(input_for(inputs, text))) {
      if (error && capacity)
        fmt(error, capacity, "missing tool input {}", text);
      return SALTS_EINVAL;
    }
  }
  /* No silently ignored invocation fields: every supplied input must bind an
   * entire argv entry. Repeated references deliberately reuse the same value. */
  for (size_t i = 0u; i < json_object_size(inputs); ++i) {
    const char *key = json_object_key(inputs, i);
    bool used = false;
    for (size_t j = 0u; j < json_array_size(args); ++j) {
      const char *text = json_string(json_array_get(args, j));
      size_t n = strlen(text);
      if (reference(text) && n == strlen(key) + 2u && !memcmp(text + 1u, key, n - 2u)) {
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
  if (reference(text)) {
    size_t n = strlen(text), needed = 0u, actual = 0u;
    tstr key = tstr_dup_len(text + 1u, n - 2u);
    if (!key)
      return SALTS_ENOMEM;
    int rc = action->native.value_get(action->native.value_user, key, NULL, 0u, &needed);
    if ((rc != PRAKTOR_BT_VALUE_BUFFER_TOO_SMALL && rc != PRAKTOR_BT_VALUE_OK) ||
        needed > PRAKTOR_PROCESS_MAX_OUTPUT) {
      tstr_free(key);
      return SALTS_EINVAL;
    }
    tstr value = tstr_new_len(NULL, needed);
    if (!value) {
      tstr_free(key);
      return SALTS_ENOMEM;
    }
    rc = action->native.value_get(action->native.value_user, key, value, needed + 1u, &actual);
    tstr_free(key);
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
static int start_job(praktor_tool_action *action) {
  const json_value_t *templates = json_object_get(action->definition, "args");
  size_t count = json_array_size(templates), bytes = 0u;
  tstr args[PRAKTOR_PROCESS_MAX_ARGS + 1u] = {0};
  int rc = SALTS_OK;
  for (size_t i = 0u; i < count; ++i) {
    const char *text = json_string(json_array_get(templates, i));
    if (reference(text)) {
      text = json_string(input_for(action->inputs, text));
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
    rc = praktor_execution_service_start(&action->service, &request, &action->job);
  }
  for (size_t i = 0u; i < count; ++i)
    tstr_free(args[i]);
  return rc;
}
static int tick(void *self, const praktor_leaf_request *request, salts_bt_node_status *status) {
  praktor_tool_action *action = self;
  if (strcmp(request->action, "Tool"))
    return action->native.tick_typed_leaf(action->native.user, request, status);
  if (request->in_parallel_branch)
    return SALTS_ENOTSUP;
  if (!action->job.impl) {
    int rc = start_job(action);
    if (rc != SALTS_OK) {
      fmt(action->error, sizeof action->error, "tool start failed ({})", rc);
      return rc;
    }
  }
  praktor_service_result result = {0};
  int rc = praktor_execution_service_poll(&action->service, &action->job, &result);
  if (rc != SALTS_OK)
    return rc;
  if (!result.done) {
    *status = SALTS_BT_RUNNING;
    return SALTS_OK;
  }
  char exit_code[32];
  int n = fmt(exit_code, sizeof exit_code, "{}", result.exit_code);
  if (n <= 0 || (size_t)n >= sizeof exit_code)
    return SALTS_EINVAL;
  if (action->native.value_set(action->native.value_user, "shell_exit_code", exit_code) !=
      PRAKTOR_BT_VALUE_OK)
    return SALTS_ENOBUFS;
  if (!result.success) {
    fmt_text(action->error, sizeof action->error, result.error);
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (action->native.value_set(action->native.value_user, "shell_output", result.output) !=
          PRAKTOR_BT_VALUE_OK ||
      action->native.value_set(action->native.value_user, "shell_stderr", result.error_output) !=
          PRAKTOR_BT_VALUE_OK)
    return SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}
static void halt(void *self, const praktor_leaf_request *request) {
  praktor_tool_action *action = self;
  if (strcmp(request->action, "Tool"))
    action->native.halt_typed_leaf(action->native.user, request);
  else if (action->job.impl)
    (void)praktor_execution_service_cancel(&action->service, &action->job);
}
void praktor_tool_action_bind(praktor_tool_action *action, praktor_execution_service service,
                              const json_value_t *definition, const json_value_t *inputs,
                              praktor_bt_host *hooks) {
  *action = (praktor_tool_action){
      .native = *hooks, .service = service, .definition = definition, .inputs = inputs};
  hooks->user = action;
  hooks->tick_typed_leaf = tick;
  hooks->halt_typed_leaf = halt;
}
int praktor_tool_action_close(praktor_tool_action *action) {
  if (!action || !action->job.impl)
    return SALTS_OK;
  int rc = praktor_execution_service_cancel(&action->service, &action->job);
  if (rc != SALTS_OK)
    return rc;
  uint64_t started = hrtime();
  for (;;) {
    praktor_service_result result = {0};
    rc = praktor_execution_service_poll(&action->service, &action->job, &result);
    if (rc != SALTS_OK)
      return rc;
    if (result.done)
      return praktor_execution_service_release(&action->service, &action->job);
    if (hrtime() - started >= UINT64_C(5000000000))
      return SALTS_EBUSY;
    sleep_ms(1u);
  }
}
