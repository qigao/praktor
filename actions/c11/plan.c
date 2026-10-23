#include "plan.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int praktor_c11_parse_integer(const char *text, size_t size, int *out) {
  char *end;
  double value;
  if (!text || !out) return SALTS_EINVAL;
  /* Input has a trailing NUL, as required by the C scalar string contract. */
  errno = 0; value = strtod(text,&end);
  if (end == text || (size_t)(end-text) != size || errno == ERANGE || !isfinite(value) ||
      value < INT_MIN || value > INT_MAX || fabs(value-trunc(value)) > 0.0) return SALTS_EINVAL;
  *out = (int)value; return SALTS_OK;
}
int praktor_c11_parameter_integer(const praktor_c11_leaf_parameter *p, size_t size, int *out) {
  if (!p || !out || p->kind == PRAKTOR_C11_LEAF_BOOLEAN) return SALTS_EINVAL;
  if (p->kind == PRAKTOR_C11_LEAF_STRING) return praktor_c11_parse_integer(p->text,size,out);
  if (p->kind != PRAKTOR_C11_LEAF_NUMBER || !isfinite(p->number) || p->number < INT_MIN || p->number > INT_MAX ||
      fabs(p->number-trunc(p->number)) > 0.0) return SALTS_EINVAL;
  *out = (int)p->number; return SALTS_OK;
}
static const praktor_c11_leaf_parameter *find(const praktor_c11_session_node *n, const char *key) {
  size_t i;
  for (i = 0; i < n->parameter_count; ++i) if (!strcmp(n->parameters[i].name,key)) return n->parameters+i;
  return NULL;
}
static size_t text_size(const praktor_c11_session_node *n, const praktor_c11_leaf_parameter *p) {
  return n->text_sizes ? n->text_sizes[p-n->parameters] : strlen(p->text);
}
static bool substituted(const praktor_c11_session_node *n, const praktor_c11_leaf_parameter *p) {
  size_t size;
  if (!p || p->kind != PRAKTOR_C11_LEAF_STRING) return false;
  size = text_size(n,p); return size >= 2 && p->text[0] == '{' && p->text[size-1] == '}';
}
static int fail(char *error, size_t capacity, const char *action, const char *detail) {
  if (error && capacity) (void)snprintf(error,capacity,"Node '%s': %s",action ? action : "",detail);
  return SALTS_EINVAL;
}
static salts_bt_node_kind kind(const char *action) {
  static const struct { const char *name; salts_bt_node_kind kind; } names[] = {
    {"Sequence",SALTS_BT_NODE_SEQUENCE},{"Fallback",SALTS_BT_NODE_FALLBACK},{"Selector",SALTS_BT_NODE_FALLBACK},
    {"Parallel",SALTS_BT_NODE_PARALLEL},{"ReactiveSequence",SALTS_BT_NODE_REACTIVE_SEQUENCE},
    {"ReactiveFallback",SALTS_BT_NODE_REACTIVE_FALLBACK},{"Retry",SALTS_BT_NODE_RETRY},{"Repeat",SALTS_BT_NODE_REPEAT},
    {"Switch",SALTS_BT_NODE_SWITCH},{"IfThenElse",SALTS_BT_NODE_IF_THEN_ELSE},{"Precondition",SALTS_BT_NODE_PRECONDITION},
    {"WhileDo",SALTS_BT_NODE_WHILE_DO},{"Inverter",SALTS_BT_NODE_INVERTER},{"ForceSuccess",SALTS_BT_NODE_FORCE_SUCCESS},
    {"ForceFailure",SALTS_BT_NODE_FORCE_FAILURE},{"Timeout",SALTS_BT_NODE_TIMEOUT},{"Delay",SALTS_BT_NODE_DELAY},
    {"KeepRunningUntilFailure",SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE},{"RunOnce",SALTS_BT_NODE_RUN_ONCE},
    {"PipelineSequence",SALTS_BT_NODE_PIPELINE_SEQUENCE},{"EntryUpdated",SALTS_BT_NODE_ENTRY_UPDATED},{"ConsumeQueue",SALTS_BT_NODE_CONSUME_QUEUE}
  };
  size_t i;
  for (i = 0; i < sizeof(names)/sizeof(names[0]); ++i) if (!strcmp(action,names[i].name)) return names[i].kind;
  return SALTS_BT_NODE_LEAF;
}
int praktor_c11_compile_node(const salts_bt_tree_config *limits, const char *action,
    const praktor_c11_leaf_parameter *parameters, size_t count, const size_t *sizes,
    size_t children, size_t parent, praktor_c11_session_node *output, bool *prune,
    char *error, size_t capacity) {
  praktor_c11_session_node n = {0};
  const praktor_c11_leaf_parameter *p;
  const char *key = NULL;
  bool scalar, dynamic, skip = false;
  int value = 0, fallback = 0, r;
  size_t i;
  if (error && capacity) error[0] = '\0';
  if (!limits || !action || !*action || !output || !prune || (count && !parameters) ||
      !limits->max_nodes || limits->max_nodes > UINT32_MAX || !limits->max_depth || limits->max_depth > SALTS_BT_MAX_DEPTH ||
      !limits->max_control_iterations || limits->max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS)
    return fail(error,capacity,action,"invalid compilation input or BehaviorTree limits");
  for (i = 0; i < count; ++i) {
    p = parameters+i;
    if (!p->name || p->kind < PRAKTOR_C11_LEAF_STRING || p->kind > PRAKTOR_C11_LEAF_BOOLEAN ||
        (p->kind == PRAKTOR_C11_LEAF_STRING && !p->text)) return fail(error,capacity,action,"invalid typed parameter");
  }
  n.action = action; n.parameters = parameters; n.parameter_count = count; n.text_sizes = sizes;
  n.tree.parent_index = parent; n.tree.kind = kind(action);
  scalar = find(&n,"condition") != NULL;
  if (!strcmp(action,"SubTree")) return fail(error,capacity,action,"SubTree must be expanded before plan compilation");
  switch (n.tree.kind) {
    case SALTS_BT_NODE_LEAF:
      if (children) return fail(error,capacity,action,"unsupported control has children; refusing to reinterpret as a leaf");
      break;
    case SALTS_BT_NODE_IF_THEN_ELSE:
      if (children < (scalar ? 1u : 2u) || children > (scalar ? 2u : 3u))
        return fail(error,capacity,action,"IfThenElse requires 1-2 children with a scalar condition or 2-3 with an in-tree condition");
      n.tree.condition_callback = scalar; break;
    case SALTS_BT_NODE_WHILE_DO:
      if (children != (scalar ? 1u : 2u)) return fail(error,capacity,action,"WhileDo requires one child with a scalar condition or two with an in-tree condition");
      n.tree.condition_callback = scalar; break;
    case SALTS_BT_NODE_PRECONDITION:
      if (!scalar) return fail(error,capacity,action,"Precondition requires a scalar 'condition' parameter");
      n.tree.condition_callback = true;
      if (children != 1) return fail(error,capacity,action,"must have exactly one child");
      break;
    case SALTS_BT_NODE_INVERTER: case SALTS_BT_NODE_FORCE_SUCCESS: case SALTS_BT_NODE_FORCE_FAILURE:
    case SALTS_BT_NODE_RETRY: case SALTS_BT_NODE_REPEAT: case SALTS_BT_NODE_TIMEOUT: case SALTS_BT_NODE_DELAY:
    case SALTS_BT_NODE_RUN_ONCE: case SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE: case SALTS_BT_NODE_ENTRY_UPDATED:
    case SALTS_BT_NODE_CONSUME_QUEUE:
      if (children != 1) return fail(error,capacity,action,"must have exactly one child");
      break;
    default: break;
  }
  if (n.tree.kind == SALTS_BT_NODE_SWITCH && !find(&n,"variable")) return fail(error,capacity,action,"Switch requires parameter 'variable'");
  if (n.tree.kind == SALTS_BT_NODE_ENTRY_UPDATED && !find(&n,"watch_key")) return fail(error,capacity,action,"EntryUpdated requires parameter 'watch_key'");
  if (!children && (n.tree.kind == SALTS_BT_NODE_PARALLEL || n.tree.kind == SALTS_BT_NODE_REACTIVE_SEQUENCE ||
                   n.tree.kind == SALTS_BT_NODE_REACTIVE_FALLBACK || n.tree.kind == SALTS_BT_NODE_SWITCH)) {
    n.terminal = n.tree.kind == SALTS_BT_NODE_SWITCH ? PRAKTOR_C11_TERMINAL_SWITCH_FAILURE :
        n.tree.kind == SALTS_BT_NODE_REACTIVE_FALLBACK ? PRAKTOR_C11_TERMINAL_FAILURE : PRAKTOR_C11_TERMINAL_SUCCESS;
    n.tree.kind = SALTS_BT_NODE_LEAF; skip = true;
  } else if (n.tree.kind == SALTS_BT_NODE_PARALLEL) n.tree.parallel_hooks = true;
  switch (n.tree.kind) {
    case SALTS_BT_NODE_RETRY: key = "num_attempts"; fallback = 3; break;
    case SALTS_BT_NODE_REPEAT: key = "num_cycles"; break;
    case SALTS_BT_NODE_WHILE_DO: key = "max_iterations"; fallback = 100; break;
    case SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE: key = "max_iterations"; fallback = 1000; break;
    case SALTS_BT_NODE_TIMEOUT: key = "timeout_ms"; fallback = 1000; break;
    case SALTS_BT_NODE_DELAY: key = "delay_ms"; fallback = 100; break;
    default: break;
  }
  if (key) {
    p = find(&n,key); dynamic = substituted(&n,p);
    if (!p && n.tree.kind == SALTS_BT_NODE_REPEAT) return fail(error,capacity,action,"Repeat requires parameter 'num_cycles'");
    value = fallback;
    if (dynamic && n.tree.kind == SALTS_BT_NODE_REPEAT) value = 0;
    else if (dynamic && n.tree.kind != SALTS_BT_NODE_TIMEOUT && n.tree.kind != SALTS_BT_NODE_DELAY &&
             (size_t)value > limits->max_control_iterations) value = (int)limits->max_control_iterations;
    if (!dynamic && p) {
      r = praktor_c11_parameter_integer(p,p->kind == PRAKTOR_C11_LEAF_STRING ? text_size(&n,p) : 0,&value);
      if (r != SALTS_OK) return fail(error,capacity,action,"control parameter must be a 32-bit integer");
    }
    if (value < 0) return fail(error,capacity,action,"control parameter must be non-negative");
    if (n.tree.kind == SALTS_BT_NODE_TIMEOUT || n.tree.kind == SALTS_BT_NODE_DELAY) {
      n.tree.duration_ns = (uint64_t)value*UINT64_C(1000000);
      if (dynamic) n.tree.dynamic_parameter_mask = SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS;
    } else {
      if ((size_t)value > limits->max_control_iterations) return fail(error,capacity,action,"control parameter exceeds the configured iteration budget");
      if (dynamic) n.tree.dynamic_parameter_mask = SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
      if (!value && !dynamic && n.tree.kind != SALTS_BT_NODE_REPEAT) {
        n.terminal = n.tree.kind == SALTS_BT_NODE_WHILE_DO ? PRAKTOR_C11_TERMINAL_SUCCESS : PRAKTOR_C11_TERMINAL_FAILURE;
        n.tree.kind = SALTS_BT_NODE_LEAF; n.tree.condition_callback = false; skip = true;
      } else n.tree.limit = (size_t)value;
    }
  }
  *output = n; *prune = skip; return SALTS_OK;
}
