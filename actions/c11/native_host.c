#include "native_host.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct native_host_impl {
  praktor_c11_value_store values;
  size_t scratch_capacity;
  char *scratch;
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
static int tick_leaf(void *user, const praktor_c11_leaf_request *request,
                      salts_bt_node_status *status) {
  native_host_impl *impl = (native_host_impl *)user;
  const praktor_c11_leaf_parameter *param;
  char *key, *value, *temporary;
  int result, contains = 0, expected = 0, actual = 0;
  size_t required = 0u, i, j;
  if (impl == NULL || request == NULL || request->action == NULL || status == NULL ||
      (request->parameter_count != 0u && request->parameters == NULL)) return SALTS_EINVAL;
  for (i = 0u; i < request->parameter_count; ++i) {
    if (request->parameters[i].name == NULL) return SALTS_EINVAL;
    for (j = 0u; j < i; ++j)
      if (strcmp(request->parameters[i].name, request->parameters[j].name) == 0) return SALTS_EINVAL;
  }
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
static void halt_leaf(void *user, size_t index) { (void)user; (void)index; }

int praktor_c11_native_host_init(praktor_c11_native_host *host, size_t capacity,
                                 size_t max_key_bytes, size_t max_value_bytes) {
  native_host_impl *impl;
  size_t scratch_bytes = max_key_bytes > max_value_bytes ? max_key_bytes : max_value_bytes;
  int result;
  if (host == NULL || host->impl != NULL || scratch_bytes == SIZE_MAX ||
      scratch_bytes + 1u > SIZE_MAX / 3u) return SALTS_EINVAL;
  impl = (native_host_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  result = praktor_c11_value_store_init(&impl->values, capacity, max_key_bytes, max_value_bytes);
  if (result != PRAKTOR_C11_VALUE_OK) { free(impl); return result == PRAKTOR_C11_VALUE_ENOMEM ? SALTS_ENOMEM : SALTS_EINVAL; }
  impl->scratch_capacity = scratch_bytes + 1u;
  impl->scratch = (char *)calloc(3u, impl->scratch_capacity);
  if (impl->scratch == NULL) { praktor_c11_value_store_destroy(&impl->values); free(impl); return SALTS_ENOMEM; }
  host->impl = impl;
  return SALTS_OK;
}
void praktor_c11_native_host_destroy(praktor_c11_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  if (impl == NULL) return;
  praktor_c11_value_store_destroy(&impl->values);
  free(impl->scratch);
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
  out_hooks->halt_leaf = halt_leaf;
  return SALTS_OK;
}
praktor_c11_value_store *praktor_c11_native_host_values(praktor_c11_native_host *host) {
  native_host_impl *impl = host == NULL ? NULL : (native_host_impl *)host->impl;
  return impl == NULL ? NULL : &impl->values;
}
