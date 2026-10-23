#include "session_internal.h"
#include <json_parser.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

static bool equals_fold(session_text text, const char *literal) {
  size_t i, n = strlen(literal);
  if (text.size != n) return false;
  for (i = 0; i < n; ++i)
    if (tolower((unsigned char)text.data[i]) != (unsigned char)literal[i]) return false;
  return true;
}
int session_key(session_impl *s, size_t node, const char *name,
                const char *fallback, size_t scratch, session_text *out) {
  int r = session_resolve(s, node, name, fallback, scratch, out);
  if (r == SALTS_OK && memchr(out->data, '\0', out->size))
    return session_error(s, SALTS_EINVAL, "C11 key '%s' contains NUL", name);
  return r;
}
int session_restore_pipeline(session_impl *s, size_t node) {
  size_t descendant = node, parent = s->nodes[node].tree.parent_index;
  while (parent != PRAKTOR_C11_BT_NO_PARENT) {
    session_node_state *st = &s->state[parent];
    if (st->pipeline && st->restore_tick != s->generation) {
      size_t i, ordinal = 0;
      for (i = parent + 1; i < s->count; ++i) {
        if (s->nodes[i].tree.parent_index == parent) {
          if (ordinal++ == st->active_child && i == descendant) {
            int r = session_set(s, "pipeline_input", session_cached(s, parent, SESSION_PIPE_DATA));
            if (r != SALTS_OK) return r;
            st->restore_tick = s->generation;
            return SALTS_OK;
          }
        }
      }
    }
    descendant = parent; parent = s->nodes[parent].tree.parent_index;
  }
  return SALTS_OK;
}
static int restore(session_impl *s, size_t node) {
  return s->state[node].branch ? SALTS_OK : session_restore_pipeline(s, node);
}
int session_dynamic(void *user, size_t node, salts_bt_node_parameters *params) {
  session_impl *s = user;
  const praktor_c11_bt_node_desc *d = &s->nodes[node].tree;
  const char *key = NULL;
  int value = 0, fallback = 0, r;
  if (s->state[node].branch) {
    if (!s->config.host.resolve_dynamic_parameters)
      return session_error(s, SALTS_ENOTSUP, "C11 Parallel dynamic parameters require a host branch-aware resolver");
    return session_hook_status(s, s->config.host.resolve_dynamic_parameters(s->config.host.user, node, params),
                               "C11 Parallel dynamic parameter resolver failed");
  }
  if (d->dynamic_parameter_mask & SALTS_BT_DYNAMIC_PARAMETER_LIMIT) {
    switch (d->kind) {
      case SALTS_BT_NODE_RETRY: key = "num_attempts"; fallback = 3; break;
      case SALTS_BT_NODE_REPEAT: key = "num_cycles"; break;
      case SALTS_BT_NODE_WHILE_DO: key = "max_iterations"; fallback = 100; break;
      case SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE: key = "max_iterations"; fallback = 1000; break;
      default: return SALTS_EINVAL;
    }
    r = (d->kind == SALTS_BT_NODE_REPEAT && !session_parameter(s, node, key))
        ? SALTS_EINVAL : session_integer(s, node, key, fallback, &value);
    if (r == SALTS_OK && (value < 0 || (size_t)value > s->config.tree.max_control_iterations)) r = SALTS_EINVAL;
    if (r == SALTS_OK) params->limit = (size_t)value;
  } else if (d->dynamic_parameter_mask & SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS) {
    if (d->kind == SALTS_BT_NODE_TIMEOUT) { key = "timeout_ms"; fallback = 1000; }
    else if (d->kind == SALTS_BT_NODE_DELAY) { key = "delay_ms"; fallback = 100; }
    else return SALTS_EINVAL;
    r = session_integer(s, node, key, fallback, &value);
    if (r == SALTS_OK && value < 0) r = SALTS_EINVAL;
    if (r == SALTS_OK) params->duration_ns = (uint64_t)value * UINT64_C(1000000);
  } else r = SALTS_EINVAL;
  if (r != SALTS_OK) return session_error(s, r, "C11 dynamic control parameter resolution failed: %s", key ? key : "invalid mask");
  return SALTS_OK;
}
int session_condition(void *user, size_t node, salts_bt_node_status *status) {
  session_impl *s = user;
  const praktor_c11_leaf_parameter *p;
  session_text text;
  bool truth;
  int r;
  if (s->state[node].branch && !s->config.host.condition)
    return session_error(s, SALTS_ENOTSUP, "C11 Parallel condition requires a host branch-aware condition hook");
  r = restore(s, node); if (r != SALTS_OK) return r;
  if (s->config.host.condition) return s->config.host.condition(s->config.host.user, node, status);
  p = session_parameter(s, node, "condition");
  if (!p) return session_error(s, SALTS_EINVAL, "condition callback requested without scalar condition parameter");
  if (p->kind == PRAKTOR_C11_LEAF_BOOLEAN) truth = p->boolean;
  else if (p->kind == PRAKTOR_C11_LEAF_NUMBER) truth = fpclassify(p->number) != FP_ZERO;
  else {
    r = session_resolve(s, node, "condition", NULL, 0, &text); if (r != SALTS_OK) return r;
    truth = text.size && !equals_fold(text, "0") && !equals_fold(text, "false") &&
        !equals_fold(text, "no") && !equals_fold(text, "off") && !equals_fold(text, "null");
  }
  *status = truth ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE; return SALTS_OK;
}
int session_select(void *user, size_t node, size_t *child) {
  session_impl *s = user;
  int r;
  if (s->state[node].branch && !s->config.host.select_child)
    return session_error(s, SALTS_ENOTSUP, "C11 Parallel Switch requires a host branch-aware selection hook");
  r = restore(s, node); if (r != SALTS_OK) return r;
  return s->config.host.select_child ? s->config.host.select_child(s->config.host.user, node, child)
                                      : session_switch(s, node, child);
}
static void clear_pipeline(session_impl *s, size_t node) {
  s->state[node].pipeline = false; s->state[node].restore_tick = 0;
  session_uncache(s, node, SESSION_PIPE_DATA); session_uncache(s, node, SESSION_PIPE_KEY);
}
int session_pipeline_prepare(void *user, size_t node, size_t child) {
  session_impl *s = user;
  session_node_state *st = &s->state[node];
  session_text key, data = {"", 0};
  bool has;
  int r = restore(s, node); if (r != SALTS_OK) return r;
  if (s->config.host.pipeline_prepare)
    return session_hook_status(s, s->config.host.pipeline_prepare(s->config.host.user, node, child), "C11 PipelineSequence prepare hook failed");
  if (st->branch) return session_error(s, SALTS_ENOTSUP, "C11 Parallel PipelineSequence requires host branch-aware hooks");
  if (child >= st->child_count) return SALTS_EINVAL;
  if (!st->pipeline) {
    r = session_key(s, node, "output_key", "pipeline_result", 0, &key);
    if (r == SALTS_OK) r = session_cache(s, node, SESSION_PIPE_KEY, key);
    if (r == SALTS_OK) r = session_key(s, node, "input_key", "pipeline_input", 0, &key);
    if (r == SALTS_OK) r = session_has(s, key.data, &has);
    if (r == SALTS_OK && has) r = session_get(s, key.data, 1, &data);
    if (r == SALTS_OK) r = session_cache(s, node, SESSION_PIPE_DATA, data);
    if (r != SALTS_OK) { clear_pipeline(s, node); return r; }
    st->pipeline = true;
  }
  st->active_child = child;
  return session_set(s, "pipeline_input", session_cached(s, node, SESSION_PIPE_DATA));
}
int session_pipeline_complete(void *user, size_t node, size_t child, salts_bt_node_status status) {
  session_impl *s = user;
  session_node_state *st = &s->state[node];
  session_text data;
  bool has;
  int r = SALTS_OK;
  if (s->config.host.pipeline_complete)
    return session_hook_status(s, s->config.host.pipeline_complete(s->config.host.user, node, child, status), "C11 PipelineSequence complete hook failed");
  if (st->branch) return SALTS_ENOTSUP;
  if (!st->pipeline || child >= st->child_count) return SALTS_EINVAL;
  if (status == SALTS_BT_SUCCESS) {
    r = session_has(s, "pipeline_output", &has);
    if (r == SALTS_OK && has) {
      r = session_get(s, "pipeline_output", 0, &data);
      if (r == SALTS_OK) r = session_cache(s, node, SESSION_PIPE_DATA, data);
    }
    if (r == SALTS_OK && child + 1 == st->child_count) {
      r = session_set(s, session_cached(s, node, SESSION_PIPE_KEY).data, session_cached(s, node, SESSION_PIPE_DATA));
      clear_pipeline(s, node);
    }
  } else clear_pipeline(s, node);
  return r;
}
void session_pipeline_abort(void *user, size_t node, size_t child) {
  session_impl *s = user;
  if (s->config.host.pipeline_abort) s->config.host.pipeline_abort(s->config.host.user, node, child);
  else clear_pipeline(s, node);
}
int session_entry_changed(void *user, size_t node, bool *changed) {
  session_impl *s = user;
  session_text key, current = {"", 0}, previous;
  bool has;
  int r = restore(s, node); if (r != SALTS_OK) return r;
  if (s->config.host.entry_changed)
    return session_hook_status(s, s->config.host.entry_changed(s->config.host.user, node, changed), "C11 EntryUpdated hook failed");
  if (s->state[node].branch) return session_error(s, SALTS_ENOTSUP, "C11 Parallel EntryUpdated requires a host branch-aware observation hook");
  r = session_key(s, node, "watch_key", NULL, 0, &key);
  if (r == SALTS_OK && !key.size) r = session_error(s, SALTS_EINVAL, "EntryUpdated requires a non-empty 'watch_key'");
  if (r == SALTS_OK) r = session_has(s, key.data, &has);
  if (r == SALTS_OK && has) r = session_get(s, key.data, 1, &current);
  if (r != SALTS_OK) return r;
  previous = session_cached(s, node, SESSION_ENTRY_VALUE);
  *changed = !s->state[node].observed || current.size != previous.size || memcmp(current.data, previous.data, current.size);
  r = session_cache(s, node, SESSION_ENTRY_VALUE, current);
  if (r == SALTS_OK) s->state[node].observed = true;
  return r;
}
int session_queue_begin(void *user, size_t node, bool *item) {
  session_impl *s = user;
  session_text key, input, item_key;
  json_value_t *doc = NULL, *tail = NULL, *copy;
  char *first = NULL, *remaining = NULL;
  size_t i, count, first_size = 0, remaining_size = 0;
  bool has;
  int r = restore(s, node); if (r != SALTS_OK) return r;
  if (s->config.host.queue_begin)
    return session_hook_status(s, s->config.host.queue_begin(s->config.host.user, node, item), "C11 ConsumeQueue begin hook failed");
  if (s->state[node].branch) return session_error(s, SALTS_ENOTSUP, "C11 Parallel ConsumeQueue requires host branch-aware hooks");
  s->state[node].queue_active = s->state[node].queue_more = false; *item = false;
  r = session_key(s, node, "queue_key", "task_queue", 0, &key);
  if (r == SALTS_OK) r = session_has(s, key.data, &has);
  if (r != SALTS_OK || !has) return r;
  r = session_get(s, key.data, 1, &input); if (r != SALTS_OK) return r;
  doc = json_parse(input.data, input.size);
  if (!doc || json_type(doc) != JSON_ARRAY) {
    session_error(s, SALTS_OK, "ConsumeQueue payload rejected: payload must be a valid JSON array");
    goto done;
  }
  count = json_array_size(doc); if (!count) goto done;
  first = json_serialize(json_array_get(doc, 0), &first_size);
  tail = json_create_array();
  if (!first || !tail) { r = SALTS_ENOMEM; goto done; }
  for (i = 1; i < count; ++i) {
    copy = json_clone(json_array_get(doc, i));
    if (!copy || !json_array_add_checked(tail, copy)) {
      if (copy) json_free(copy);
      r = SALTS_ENOMEM; goto done;
    }
  }
  remaining = json_serialize(tail, &remaining_size);
  if (!remaining) { r = SALTS_ENOMEM; goto done; }
  /* Resolve item key in separate scratch so queue_key stays valid. Resolve
   * only after successful parsing, matching legacy missing/empty behavior. */
  r = session_key(s, node, "item_key", "current_item", 2, &item_key);
  if (r == SALTS_OK) { session_text text = {remaining, remaining_size}; r = session_set(s, key.data, text); }
  if (r == SALTS_OK) { session_text text = {first, first_size}; r = session_set(s, item_key.data, text); }
  if (r == SALTS_OK) { s->state[node].queue_active = true; s->state[node].queue_more = count > 1; *item = true; }
done:
  if (first) json_serialize_free(first);
  if (remaining) json_serialize_free(remaining);
  if (tail) json_free(tail);
  if (doc) json_free(doc);
  return r;
}
int session_queue_finish(void *user, size_t node, bool *more) {
  session_impl *s = user;
  if (s->config.host.queue_finish)
    return session_hook_status(s, s->config.host.queue_finish(s->config.host.user, node, more), "C11 ConsumeQueue finish hook failed");
  if (s->state[node].branch) return SALTS_ENOTSUP;
  if (!s->state[node].queue_active) return session_error(s, SALTS_EINVAL, "ConsumeQueue finish called without an active queue item");
  *more = s->state[node].queue_more; s->state[node].queue_active = s->state[node].queue_more = false; return SALTS_OK;
}
void session_queue_abort(void *user, size_t node) {
  session_impl *s = user;
  if (s->config.host.queue_abort) s->config.host.queue_abort(s->config.host.user, node);
  else s->state[node].queue_active = s->state[node].queue_more = false;
}
