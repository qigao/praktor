#include "native_host.h"
#include <salts/clock.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_sleep_state {
  const void *session_owner;
  size_t node_index;
  uint64_t started_ns;
  uint64_t duration_ns;
  bool active;
} native_sleep_state;

typedef struct native_host_impl {
  praktor_c11_value_store values;
  size_t scratch_capacity;
  char *scratch;
  native_sleep_state *sleep_states;
  size_t max_running_leaves;
  uint64_t (*now_ns)(void *user);
  void *clock_user;
} native_host_impl;

static int value_result(int result) {
  switch (result) {
    case PRAKTOR_C11_VALUE_OK: return PRAKTOR_C11_BT_VALUE_OK;
    case PRAKTOR_C11_VALUE_ENOTFOUND: return PRAKTOR_C11_BT_VALUE_NOT_FOUND;
    case PRAKTOR_C11_VALUE_ENOBUFS: return PRAKTOR_C11_BT_VALUE_BUFFER_TOO_SMALL;
    default: return PRAKTOR_C11_BT_VALUE_ERROR;
  }
}
static int get_value(void *user, const char *key, char *value,
                     size_t capacity, size_t *size) {
  native_host_impl *impl = (native_host_impl *)user;
  return value_result(praktor_c11_value_store_get(&impl->values, key, value, capacity, size));
}
static int set_value(void *user, const char *key, const char *value) {
  native_host_impl *impl = (native_host_impl *)user;
  return value_result(praktor_c11_value_store_set(&impl->values, key, value));
}
static int contains_value(void *user, const char *key, int *contains) {
  native_host_impl *impl = (native_host_impl *)user;
  return value_result(praktor_c11_value_store_contains(&impl->values, key, contains));
}
static const praktor_c11_leaf_parameter *parameter(
    const praktor_c11_leaf_request *request, const char *name) {
  size_t i;
  for (i = 0u; i < request->parameter_count; ++i)
    if (strcmp(request->parameters[i].name, name) == 0) return &request->parameters[i];
  return NULL;
}
static int copy_text(char *out, size_t capacity, const char *text) {
  size_t length = 0u;
  if (text == NULL) return SALTS_EINVAL;
  while (length < capacity && text[length] != '\0') ++length;
  if (length == capacity) return SALTS_ENOBUFS;
  memcpy(out, text, length + 1u);
  return SALTS_OK;
}
/* Resolve exact {key} substitutions, matching the legacy parameter convention. */
static int resolve(native_host_impl *impl, const praktor_c11_leaf_parameter *param,
                    char *out, char *key_scratch) {
  size_t length, required = 0u;
  int written;
  if (param == NULL) return SALTS_EINVAL;
  switch (param->kind) {
    case PRAKTOR_C11_LEAF_BOOLEAN:
      return copy_text(out, impl->scratch_capacity, param->boolean ? "true" : "false");
    case PRAKTOR_C11_LEAF_NUMBER:
      if (!isfinite(param->number)) return SALTS_EINVAL;
      written = snprintf(out, impl->scratch_capacity, "%.6f", param->number);
      return written >= 0 && (size_t)written < impl->scratch_capacity ? SALTS_OK : SALTS_ENOBUFS;
    case PRAKTOR_C11_LEAF_STRING:
      if (param->text == NULL) return SALTS_EINVAL;
      length = strlen(param->text);
      if (length >= 3u && param->text[0] == '{' && param->text[length - 1u] == '}') {
        if (length - 2u >= impl->scratch_capacity) return SALTS_ENOBUFS;
        memcpy(key_scratch, param->text + 1, length - 2u);
        key_scratch[length - 2u] = '\0';
        return praktor_c11_value_store_get(&impl->values, key_scratch, out,
                                           impl->scratch_capacity, &required) == PRAKTOR_C11_VALUE_OK
            ? SALTS_OK : SALTS_EINVAL;
      }
      return copy_text(out, impl->scratch_capacity, param->text);
  }
  return SALTS_EINVAL;
}
static int parse_integer(const char *text, int *out) {
  char *end = NULL;
  double value;
  errno = 0;
  value = strtod(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE || !isfinite(value) ||
      value < INT_MIN || value > INT_MAX || fabs(value - trunc(value)) > 0.0) return SALTS_EINVAL;
  *out = (int)value;
  return SALTS_OK;
}
static native_sleep_state *find_sleep(native_host_impl *impl,
                                       const praktor_c11_leaf_request *request) {
  size_t i;
  for (i = 0u; i < impl->max_running_leaves; ++i) {
    native_sleep_state *state = &impl->sleep_states[i];
    if (state->active && state->session_owner == request->session_owner &&
        state->node_index == request->node_index) return state;
  }
  return NULL;
}

/* Supported duration forms match the legacy non-negative finite range and
 * truncation to integer milliseconds. Unknown suffixes are rejected rather
 * than silently interpreted as milliseconds. */
static int sleep_duration(native_host_impl *impl,
                           const praktor_c11_leaf_parameter *param,
                           uint64_t *out_duration_ns) {
  double value = 0.0, multiplier = 1.0;
  char *end = NULL;
  int result;
  if (param != NULL) {
    if (param->kind == PRAKTOR_C11_LEAF_NUMBER) {
      value = param->number;
    } else if (param->kind == PRAKTOR_C11_LEAF_STRING) {
      result = resolve(impl, param, impl->scratch,
                        impl->scratch + impl->scratch_capacity);
      if (result != SALTS_OK) return result;
      errno = 0;
      value = strtod(impl->scratch, &end);
      if (end == impl->scratch || errno == ERANGE) return SALTS_EINVAL;
      if (strcmp(end, "s") == 0) multiplier = 1000.0;
      else if (strcmp(end, "m") == 0) multiplier = 60000.0;
      else if (strcmp(end, "h") == 0) multiplier = 3600000.0;
      else if (*end != '\0' && strcmp(end, "ms") != 0) return SALTS_EINVAL;
    } else return SALTS_EINVAL;
  }
  value *= multiplier;
  if (!isfinite(value) || value < 0.0 || value > INT_MAX) return SALTS_EINVAL;
  *out_duration_ns = (uint64_t)value * UINT64_C(1000000);
  return SALTS_OK;
}

static uint64_t default_now_ns(void *user) { (void)user; return cmeta_hrtime(); }

static int tick_sleep(native_host_impl *impl, const praktor_c11_leaf_request *request,
                       salts_bt_node_status *status) {
  native_sleep_state *state;
  uint64_t now, duration = 0u;
  size_t i;
  int result;
  if (request->session_owner == NULL) return SALTS_EINVAL;
  state = find_sleep(impl, request);
  if (state == NULL) {
    result = sleep_duration(impl, parameter(request, "duration"), &duration);
    if (result != SALTS_OK) return result;
    if (duration == 0u) { *status = SALTS_BT_SUCCESS; return SALTS_OK; }
    for (i = 0u; i < impl->max_running_leaves; ++i) {
      if (!impl->sleep_states[i].active) { state = &impl->sleep_states[i]; break; }
    }
    if (state == NULL) return SALTS_ENOBUFS;
    state->session_owner = request->session_owner;
    state->node_index = request->node_index;
    state->duration_ns = duration;
    state->started_ns = impl->now_ns(impl->clock_user);
    state->active = true;
    *status = SALTS_BT_RUNNING;
    return SALTS_OK;
  }
  now = impl->now_ns(impl->clock_user);
  if (now < state->started_ns) return SALTS_EINVAL;
  if (now - state->started_ns < state->duration_ns) {
    *status = SALTS_BT_RUNNING;
  } else {
    memset(state, 0, sizeof(*state));
    *status = SALTS_BT_SUCCESS;
  }
  return SALTS_OK;
}

static int tick_parse_lines(native_host_impl *impl,
                             const praktor_c11_leaf_request *request,
                             salts_bt_node_status *status) {
  const praktor_c11_leaf_parameter *param;
  char *key = impl->scratch;
  char *input = key + impl->scratch_capacity;
  char *temporary = input + impl->scratch_capacity;
  char count_text[3u * sizeof(size_t) + 1u];
  char *cursor;
  size_t required = 0u, key_size, written = 0u, count = 0u;
  int result, count_size;
  praktor_c11_value_write writes[2];
  param = parameter(request, "filter");
  if (param != NULL) {
    result = resolve(impl, param, input, temporary);
    if (result != SALTS_OK) return result;
    /* Legacy uses std::regex_search, including its invalid-regex fallback.
     * Never silently substitute a different matching language. */
    if (*input != '\0') return SALTS_ENOTSUP;
  }
  param = parameter(request, "input_key");
  result = param == NULL ? copy_text(key, impl->scratch_capacity, "shell_output")
                         : resolve(impl, param, key, temporary);
  if (result != SALTS_OK) return result;
  result = praktor_c11_value_store_get(&impl->values, key, input,
                                       impl->scratch_capacity, &required);
  if (result == PRAKTOR_C11_VALUE_ENOTFOUND) {
    *status = SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  if (result != PRAKTOR_C11_VALUE_OK) return SALTS_EINVAL;
  param = parameter(request, "output_key");
  result = param == NULL ? copy_text(key, impl->scratch_capacity, "parsed_lines")
                         : resolve(impl, param, key, temporary);
  if (result != SALTS_OK) return result;
  key_size = strlen(key);
  if (impl->scratch_capacity - key_size <= sizeof("_count") - 1u)
    return SALTS_ENOBUFS;
  memcpy(temporary, key, key_size);
  memcpy(temporary + key_size, "_count", sizeof("_count"));
  /* Match getline + Legacy's !result.empty() join exactly: CR is retained,
   * a final LF is not another line, and leading empty lines count but do not
   * add separators before the first nonempty line. Compact in place. */
  cursor = input;
  while (*cursor != '\0') {
    char *end = strchr(cursor, '\n');
    size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
    char *next = end == NULL ? cursor + length : end + 1;
    ++count;
    if (written != 0u) input[written++] = '\n';
    memmove(input + written, cursor, length);
    written += length;
    cursor = next;
  }
  input[written] = '\0';
  count_size = snprintf(count_text, sizeof(count_text), "%zu", count);
  if (count_size < 0 || (size_t)count_size >= sizeof(count_text)) return SALTS_ENOBUFS;
  writes[0].key = key; writes[0].value = input;
  writes[1].key = temporary; writes[1].value = count_text;
  result = praktor_c11_value_store_set_many(&impl->values, writes, 2u);
  if (result != PRAKTOR_C11_VALUE_OK)
    return result == PRAKTOR_C11_VALUE_EINVAL ? SALTS_EINVAL : SALTS_ENOBUFS;
  *status = SALTS_BT_SUCCESS;
  return SALTS_OK;
}

static int tick_leaf(void *user, const praktor_c11_leaf_request *request,
                      salts_bt_node_status *status) {
  native_host_impl *impl = (native_host_impl *)user;
  const praktor_c11_leaf_parameter *param;
  char *key, *value, *temporary;
  int result, contains = 0, expected = 0, actual = 0;
  size_t required = 0u, i, j;
  if (impl == NULL || request == NULL || request->action == NULL || status == NULL ||
      (request->parameter_count != 0u && request->parameters == NULL)) return SALTS_EINVAL;
  /* This host owns one root store, not branch-local transactions. */
  if (request->in_parallel_branch) return SALTS_ENOTSUP;
  for (i = 0u; i < request->parameter_count; ++i) {
    if (request->parameters[i].name == NULL) return SALTS_EINVAL;
    for (j = 0u; j < i; ++j)
      if (strcmp(request->parameters[i].name, request->parameters[j].name) == 0) return SALTS_EINVAL;
  }
  if (strcmp(request->action, "Sleep") == 0) return tick_sleep(impl, request, status);
  if (find_sleep(impl, request) != NULL) return SALTS_EINVAL;
  if (strcmp(request->action, "ParseLines") == 0) return tick_parse_lines(impl, request, status);
  key = impl->scratch;
  value = key + impl->scratch_capacity;
  temporary = value + impl->scratch_capacity;
  if (strcmp(request->action, "SetVariable") == 0) {
    param = parameter(request, "key");
    if (param == NULL) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    result = resolve(impl, param, key, temporary);
    if (result != SALTS_OK) return result;
    param = parameter(request, "value");
    if (param != NULL) {
      result = resolve(impl, param, value, temporary);
    } else if ((param = parameter(request, "from")) != NULL) {
      result = resolve(impl, param, temporary, value);
      if (result != SALTS_OK) return result;
      result = praktor_c11_value_store_contains(&impl->values, temporary, &contains);
      if (result != PRAKTOR_C11_VALUE_OK) return SALTS_EINVAL;
      if (!contains) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
      result = praktor_c11_value_store_get(&impl->values, temporary, value,
                                           impl->scratch_capacity, &required) == PRAKTOR_C11_VALUE_OK
          ? SALTS_OK : SALTS_EINVAL;
    } else { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    if (result != SALTS_OK) return result;
    result = praktor_c11_value_store_set(&impl->values, key, value);
    if (result != PRAKTOR_C11_VALUE_OK) return SALTS_ENOBUFS;
    *status = SALTS_BT_SUCCESS;
    return SALTS_OK;
  }
  if (strcmp(request->action, "CheckExitCode") == 0) {
    param = parameter(request, "input_key");
    result = param == NULL ? copy_text(key, impl->scratch_capacity, "shell_exit_code")
                           : resolve(impl, param, key, temporary);
    if (result != SALTS_OK) return result;
    param = parameter(request, "expected");
    if (param != NULL) {
      if (param->kind == PRAKTOR_C11_LEAF_BOOLEAN) return SALTS_EINVAL;
      if (param->kind == PRAKTOR_C11_LEAF_NUMBER) {
        if (!isfinite(param->number) || param->number < INT_MIN || param->number > INT_MAX ||
            fabs(param->number - trunc(param->number)) > 0.0) return SALTS_EINVAL;
        expected = (int)param->number;
      } else {
        result = resolve(impl, param, value, temporary);
        if (result != SALTS_OK || parse_integer(value, &expected) != SALTS_OK) return SALTS_EINVAL;
      }
    }
    result = praktor_c11_value_store_get(&impl->values, key, value,
                                         impl->scratch_capacity, &required);
    if (result == PRAKTOR_C11_VALUE_ENOTFOUND) { *status = SALTS_BT_FAILURE; return SALTS_OK; }
    if (result != PRAKTOR_C11_VALUE_OK || parse_integer(value, &actual) != SALTS_OK) return SALTS_EINVAL;
    *status = actual == expected ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
    return SALTS_OK;
  }
  return SALTS_ENOTSUP;
}
static void halt_typed_leaf(void *user, const praktor_c11_leaf_request *request) {
  native_host_impl *impl = (native_host_impl *)user;
  native_sleep_state *state;
  if (impl == NULL || request == NULL || request->session_owner == NULL) return;
  state = find_sleep(impl, request);
  if (state != NULL) memset(state, 0, sizeof(*state));
}

int praktor_c11_native_host_init_with_config(
    praktor_c11_native_host *host, const praktor_c11_native_host_config *config) {
  native_host_impl *impl;
  size_t scratch_bytes;
  int result;
  if (host == NULL || host->impl != NULL || config == NULL) return SALTS_EINVAL;
  scratch_bytes = config->max_key_bytes > config->max_value_bytes
      ? config->max_key_bytes : config->max_value_bytes;
  if (scratch_bytes == SIZE_MAX || scratch_bytes + 1u > SIZE_MAX / 3u ||
      config->max_running_leaves > SIZE_MAX / sizeof(native_sleep_state)) return SALTS_EINVAL;
  impl = (native_host_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  result = praktor_c11_value_store_init(&impl->values, config->value_capacity,
                                        config->max_key_bytes, config->max_value_bytes);
  if (result != PRAKTOR_C11_VALUE_OK) {
    free(impl);
    return result == PRAKTOR_C11_VALUE_ENOMEM ? SALTS_ENOMEM : SALTS_EINVAL;
  }
  impl->scratch_capacity = scratch_bytes + 1u;
  impl->scratch = (char *)calloc(3u, impl->scratch_capacity);
  if (config->max_running_leaves != 0u)
    impl->sleep_states = (native_sleep_state *)calloc(config->max_running_leaves, sizeof(native_sleep_state));
  if (impl->scratch == NULL ||
      (config->max_running_leaves != 0u && impl->sleep_states == NULL)) {
    praktor_c11_value_store_destroy(&impl->values);
    free(impl->scratch); free(impl->sleep_states); free(impl);
    return SALTS_ENOMEM;
  }
  impl->max_running_leaves = config->max_running_leaves;
  impl->now_ns = config->now_ns == NULL ? default_now_ns : config->now_ns;
  impl->clock_user = config->clock_user;
  host->impl = impl;
  return SALTS_OK;
}

int praktor_c11_native_host_init(praktor_c11_native_host *host, size_t capacity,
                                 size_t max_key_bytes, size_t max_value_bytes) {
  const praktor_c11_native_host_config config = {
      capacity, max_key_bytes, max_value_bytes, 0u, NULL, NULL};
  return praktor_c11_native_host_init_with_config(host, &config);
}
void praktor_c11_native_host_destroy(praktor_c11_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  if (impl == NULL) return;
  praktor_c11_value_store_destroy(&impl->values);
  free(impl->scratch);
  free(impl->sleep_states);
  free(impl);
  host->impl = NULL;
}
int praktor_c11_native_host_bind(praktor_c11_native_host *host, praktor_c11_bt_host *out_hooks) {
  if (host == NULL || host->impl == NULL || out_hooks == NULL) return SALTS_EINVAL;
  memset(out_hooks, 0, sizeof(*out_hooks));
  out_hooks->user = host->impl;
  out_hooks->value_user = host->impl;
  out_hooks->value_get = get_value;
  out_hooks->value_set = set_value;
  out_hooks->value_contains = contains_value;
  out_hooks->tick_typed_leaf = tick_leaf;
  out_hooks->halt_typed_leaf = halt_typed_leaf;
  return SALTS_OK;
}
praktor_c11_value_store *praktor_c11_native_host_values(praktor_c11_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  return impl == NULL ? NULL : &impl->values;
}
