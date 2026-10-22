#include "session_internal.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int session_error(session_impl *s, int result, const char *format, ...) {
  va_list args;
  va_start(args, format); (void)vsnprintf(s->error, sizeof(s->error), format, args); va_end(args);
  return result;
}
int session_hook_status(session_impl *s, int result, const char *operation) {
  if (result != SALTS_OK && s->error[0] == '\0')
    session_error(s, result, "%s: returned status %d", operation, result);
  return result;
}
int session_buffer_reserve(session_buffer *buffer, size_t bytes, size_t limit) {
  char *data;
  size_t capacity;
  if (bytes > limit || limit == SIZE_MAX) return SALTS_ENOBUFS;
  if (bytes + 1u <= buffer->capacity) return SALTS_OK;
  capacity = buffer->capacity ? buffer->capacity : 256u;
  while (capacity <= bytes && capacity <= (limit + 1u) / 2u) capacity *= 2u;
  if (capacity <= bytes || capacity > limit + 1u) capacity = bytes + 1u;
  data = (char *)realloc(buffer->data, capacity);
  if (data == NULL) return SALTS_ENOMEM;
  buffer->data = data; buffer->capacity = capacity; return SALTS_OK;
}
static const praktor_c11_bt_host *values(session_impl *s) {
  return s->config.host.value_get ? &s->config.host : &s->config.compatibility_values;
}
int session_get(session_impl *s, const char *key, size_t scratch, session_text *out) {
  const praktor_c11_bt_host *v = values(s);
  size_t required = 0u, actual = 0u;
  int r;
  if (v->value_get == NULL || scratch >= 3u) return SALTS_ENOTSUP;
  r = v->value_get(v->value_user, key, NULL, 0u, &required);
  if (r == PRAKTOR_C11_BT_VALUE_NOT_FOUND)
    return session_error(s, SALTS_EINVAL, "C11 host value not found: %s", key);
  if (r == PRAKTOR_C11_BT_VALUE_OK && required == 0u) {
    out->data = ""; out->size = 0u; return SALTS_OK;
  }
  if (r != PRAKTOR_C11_BT_VALUE_BUFFER_TOO_SMALL)
    return session_error(s, SALTS_EIO, "C11 host value size query failed: %s", key);
  r = session_buffer_reserve(&s->scratch[scratch], required, s->config.max_value_bytes);
  if (r != SALTS_OK) return session_error(s, r, "C11 host value exceeds configured bound: %s", key);
  s->scratch[scratch].data[required] = '\0';
  r = v->value_get(v->value_user, key, s->scratch[scratch].data,
                    s->scratch[scratch].capacity, &actual);
  if (r != PRAKTOR_C11_BT_VALUE_OK || actual != required || s->scratch[scratch].data[required] != '\0')
    return session_error(s, SALTS_EIO, "C11 host value read failed: %s", key);
  out->data = s->scratch[scratch].data; out->size = actual; return SALTS_OK;
}
int session_has(session_impl *s, const char *key, bool *out) {
  const praktor_c11_bt_host *v = values(s);
  int yes = 0;
  if (v->value_contains == NULL) return SALTS_ENOTSUP;
  if (v->value_contains(v->value_user, key, &yes) != PRAKTOR_C11_BT_VALUE_OK)
    return session_error(s, SALTS_EIO, "C11 host value contains query failed: %s", key);
  *out = yes != 0; return SALTS_OK;
}
int session_set(session_impl *s, const char *key, session_text value) {
  const praktor_c11_bt_host *v = values(s);
  int r;
  if (value.size > s->config.max_value_bytes) return SALTS_ENOBUFS;
  if (s->config.host.value_get == NULL && s->config.compatibility_set_bytes != NULL) {
    r = s->config.compatibility_set_bytes(s->config.compatibility_user, key, value.data, value.size);
  } else {
    if (v->value_set == NULL) return SALTS_ENOTSUP;
    if (memchr(value.data, '\0', value.size) != NULL)
      return session_error(s, SALTS_EINVAL, "C11 text value store rejects embedded NUL: %s", key);
    r = v->value_set(v->value_user, key, value.data);
  }
  return r == PRAKTOR_C11_BT_VALUE_OK ? SALTS_OK
      : session_error(s, SALTS_EIO, "C11 host value write failed: %s", key);
}
const praktor_c11_leaf_parameter *session_parameter(session_impl *s, size_t node, const char *name) {
  size_t i;
  for (i = 0u; i < s->nodes[node].parameter_count; ++i)
    if (!strcmp(s->nodes[node].parameters[i].name, name)) return &s->nodes[node].parameters[i];
  return NULL;
}
int session_resolve(session_impl *s, size_t node, const char *name,
                    const char *fallback, size_t scratch, session_text *out) {
  const praktor_c11_leaf_parameter *p = session_parameter(s, node, name);
  session_buffer *b;
  size_t size;
  int r, n;
  if (scratch >= 3u) return SALTS_EINVAL;
  b = &s->scratch[scratch];
  if (p == NULL) {
    if (fallback == NULL) return session_error(s, SALTS_EINVAL, "Parameter '%s' is required", name);
    out->data = fallback; out->size = strlen(fallback); return out->size > s->config.max_value_bytes ? SALTS_ENOBUFS : SALTS_OK;
  }
  if (p->kind == PRAKTOR_C11_LEAF_BOOLEAN) {
    out->data = p->boolean ? "true" : "false"; out->size = p->boolean ? 4u : 5u; return SALTS_OK;
  }
  if (p->kind == PRAKTOR_C11_LEAF_NUMBER) {
    if (!isfinite(p->number)) return SALTS_EINVAL;
    n = snprintf(NULL, 0u, "%.6f", p->number);
    if (n < 0) return SALTS_EINVAL;
    r = session_buffer_reserve(b, (size_t)n, s->config.max_value_bytes);
    if (r != SALTS_OK) return r;
    (void)snprintf(b->data, b->capacity, "%.6f", p->number);
    out->data = b->data; out->size = (size_t)n; return SALTS_OK;
  }
  size = s->text_sizes[p - s->parameters];
  if (size > s->config.max_value_bytes) return SALTS_ENOBUFS;
  if (size >= 3u && p->text[0] == '{' && p->text[size - 1u] == '}') {
    r = session_buffer_reserve(&s->scratch[3], size - 2u, s->config.max_value_bytes);
    if (r != SALTS_OK) return r;
    memcpy(s->scratch[3].data, p->text + 1, size - 2u); s->scratch[3].data[size - 2u] = '\0';
    if (memchr(s->scratch[3].data, '\0', size - 2u)) return SALTS_EINVAL;
    return session_get(s, s->scratch[3].data, scratch, out);
  }
  out->data = p->text; out->size = size; return SALTS_OK;
}
static int parse_integer(session_text text, int *out) {
  char *end;
  double value;
  errno = 0; value = strtod(text.data, &end);
  if (end == text.data || (size_t)(end - text.data) != text.size || errno == ERANGE ||
      !isfinite(value) || value < INT_MIN || value > INT_MAX || fabs(value - trunc(value)) > 0.0)
    return SALTS_EINVAL;
  *out = (int)value; return SALTS_OK;
}
int session_integer(session_impl *s, size_t node, const char *name, int fallback, int *out) {
  const praktor_c11_leaf_parameter *p = session_parameter(s, node, name);
  session_text value;
  int r;
  if (p == NULL) { *out = fallback; return SALTS_OK; }
  if (p->kind == PRAKTOR_C11_LEAF_BOOLEAN) return SALTS_EINVAL;
  if (p->kind == PRAKTOR_C11_LEAF_NUMBER) {
    if (!isfinite(p->number) || p->number < INT_MIN || p->number > INT_MAX || fabs(p->number - trunc(p->number)) > 0.0)
      return SALTS_EINVAL;
    *out = (int)p->number; return SALTS_OK;
  }
  /* Numeric control substitutions historically include the empty key ({}).
   * Ordinary string ports and Switch keep their separate >=3-byte rule. */
  if (s->text_sizes[p - s->parameters] == 2u && p->text[0] == '{' && p->text[1] == '}')
    r = session_get(s, "", 0u, &value);
  else r = session_resolve(s, node, name, NULL, 0u, &value);
  return r == SALTS_OK ? parse_integer(value, out) : r;
}
int session_switch(session_impl *s, size_t node, size_t *out) {
  const praktor_c11_leaf_parameter *p = session_parameter(s, node, "variable");
  session_text value;
  int result, parsed;
  bool contains = false;
  if (p == NULL || p->kind == PRAKTOR_C11_LEAF_BOOLEAN) return SALTS_EINVAL;
  if (p->kind == PRAKTOR_C11_LEAF_NUMBER) result = session_integer(s, node, "variable", 0, &parsed);
  else {
    result = session_resolve(s, node, "variable", NULL, 0u, &value);
    if (result != SALTS_OK) return result;
    /* Legacy bare text may name a key, including a resolved self-reference. */
    if (value.size == s->text_sizes[p - s->parameters] && !memcmp(value.data, p->text, value.size)) {
      if (memchr(value.data, '\0', value.size)) return SALTS_EINVAL;
      result = session_has(s, value.data, &contains);
      if (result != SALTS_OK) return result;
      if (contains) result = session_get(s, value.data, 1u, &value);
    }
    if (result == SALTS_OK) result = parse_integer(value, &parsed);
  }
  if (result != SALTS_OK) return session_error(s, result, "C11 Switch selector failed: variable must resolve to a 32-bit integer");
  *out = parsed < 0 ? s->state[node].child_count : (size_t)parsed; return SALTS_OK;
}
session_text session_cached(session_impl *s, size_t node, size_t slot) {
  session_slot *v = &s->slots[node * SESSION_SLOTS + slot];
  session_text result = {v->present ? s->pool.data + v->offset : "", v->present ? v->size : 0u};
  return result;
}
void session_uncache(session_impl *s, size_t node, size_t slot) {
  session_slot *v = &s->slots[node * SESSION_SLOTS + slot];
  size_t end, removed, i;
  if (!v->present) return;
  removed = v->size + 1u; end = v->offset + removed;
  memmove(s->pool.data + v->offset, s->pool.data + end, s->used - end);
  for (i = 0u; i < s->count * SESSION_SLOTS; ++i)
    if (s->slots[i].present && s->slots[i].offset >= end) s->slots[i].offset -= removed;
  s->used -= removed; memset(v, 0, sizeof(*v));
}
int session_cache(session_impl *s, size_t node, size_t slot, session_text value) {
  session_slot *v = &s->slots[node * SESSION_SLOTS + slot];
  size_t retained = s->used - (v->present ? v->size + 1u : 0u);
  int r;
  if (value.size >= s->config.max_state_bytes || retained > s->config.max_state_bytes - value.size - 1u)
    return session_error(s, SALTS_ENOBUFS, "C11 Session cached state exceeds configured byte budget");
  /* Callers supply plan/scratch/JSON memory, never a borrowed pool view. */
  r = session_buffer_reserve(&s->pool, retained + value.size + 1u, s->config.max_state_bytes);
  if (r != SALTS_OK) return r;
  session_uncache(s, node, slot);
  v->offset = s->used; v->size = value.size; v->present = true;
  memcpy(s->pool.data + s->used, value.data, value.size); s->used += value.size;
  s->pool.data[s->used++] = '\0'; return SALTS_OK;
}
