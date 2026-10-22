#include "session_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OWNER_NONE, OWNER_HOST, OWNER_COMPATIBILITY, OWNER_PROCESS };
static praktor_process_view process_view(session_impl *s, size_t node) {
  praktor_process_view view = {0};
  if (s->state[node].process.impl) (void)praktor_process_driver_view(&s->state[node].process, &view);
  return view;
}
static void free_storage(session_impl *s) {
  size_t i;
  for (i = 0; i < 4; ++i) free(s->scratch[i].data);
  free(s->pool.data); free(s->slots); free(s->plan_text); free(s->text_sizes);
  free(s->parameters); free(s->state); free(s->nodes); free(s);
}
static int close_process(session_impl *s, size_t node) {
  int r;
  praktor_process_view view;
  if (!s->state[node].process.impl) return SALTS_OK;
  r = praktor_process_driver_close(&s->state[node].process, s->config.shell_close_budget_ms);
  view = process_view(s, node);
  if (r != SALTS_OK && !s->error[0])
    session_error(s, r, "%s", view.error && *view.error ? view.error : "Salts::Process cleanup failed: active I/O leases");
  return r;
}
static void finalize_process(session_impl *s, size_t node) {
  size_t slot;
  if (process_view(s, node).active) { s->state[node].leaf_owner = OWNER_PROCESS; return; }
  if (s->state[node].process.impl && praktor_process_driver_release(&s->state[node].process) != SALTS_OK) {
    session_error(s, SALTS_EIO, "Salts::Process release failed"); return;
  }
  for (slot = SESSION_SHELL_CMD; slot < SESSION_SLOTS; ++slot) session_uncache(s, node, slot);
  s->state[node].leaf_owner = OWNER_NONE;
}
static int publish_process(session_impl *s, size_t node, int exit_code, bool success) {
  praktor_process_view view = process_view(s, node);
  session_text output = {view.stdout_data ? view.stdout_data : "", view.stdout_size};
  session_text error = {view.stderr_data ? view.stderr_data : "", view.stderr_size};
  char exit_text[32];
  session_text code;
  const char *key;
  int r;
  (void)snprintf(exit_text, sizeof(exit_text), "%d", exit_code);
  code.data = exit_text; code.size = strlen(exit_text);
  key = session_cached(s, node, SESSION_SHELL_OUT).data;
  r = session_set(s, s->slots[node*SESSION_SLOTS+SESSION_SHELL_OUT].present ? key : "shell_output", output);
  key = session_cached(s, node, SESSION_SHELL_ERR).data;
  if (r == SALTS_OK) r = session_set(s, s->slots[node*SESSION_SLOTS+SESSION_SHELL_ERR].present ? key : "shell_stderr", error);
  key = session_cached(s, node, SESSION_SHELL_EXIT).data;
  if (r == SALTS_OK) r = session_set(s, s->slots[node*SESSION_SLOTS+SESSION_SHELL_EXIT].present ? key : "shell_exit_code", code);
  if (r == SALTS_OK && !success) r = session_set(s, "shell_error", error);
  return r;
}
static int cache_parameter(session_impl *s, size_t node, size_t slot,
                           const char *name, const char *fallback, bool key) {
  session_text value;
  int r = key ? session_key(s, node, name, fallback, 0, &value)
              : session_resolve(s, node, name, fallback, 0, &value);
  return r == SALTS_OK ? session_cache(s, node, slot, value) : r;
}
static int start_process(session_impl *s, size_t node) {
  cmeta_process_options_t options;
  const char *const *environment = NULL;
  session_text command, input, cwd, text;
  const praktor_c11_leaf_parameter *stream;
  bool emit = true;
  int timeout, r;
  size_t i;
  static const struct { size_t slot; const char *name, *fallback; bool key; } ports[] = {
    {SESSION_SHELL_CMD,"cmd",NULL,true}, {SESSION_SHELL_INPUT,"input","",false},
    {SESSION_SHELL_CWD,"working_dir","",true}, {SESSION_SHELL_OUT,"output_key","shell_output",true},
    {SESSION_SHELL_ERR,"stderr_key","shell_stderr",true}, {SESSION_SHELL_EXIT,"exit_code_key","shell_exit_code",true}
  };
  for (i = 0; i < sizeof(ports)/sizeof(ports[0]); ++i) {
    r = cache_parameter(s, node, ports[i].slot, ports[i].name, ports[i].fallback, ports[i].key);
    if (r != SALTS_OK) return r;
  }
  command = session_cached(s, node, SESSION_SHELL_CMD);
  if (!command.size) return session_error(s, SALTS_EINVAL, "Shell command must not be empty");
  stream = session_parameter(s, node, "stream_output");
  if (stream) {
    if (stream->kind == PRAKTOR_C11_LEAF_BOOLEAN) emit = stream->boolean;
    else {
      if (stream->kind != PRAKTOR_C11_LEAF_STRING) return session_error(s, SALTS_EINVAL, "Shell stream_output must be a boolean");
      r = session_resolve(s, node, "stream_output", NULL, 0, &text); if (r != SALTS_OK) return r;
      if (text.size == 4 && tolower((unsigned char)text.data[0]) == 't' &&
          tolower((unsigned char)text.data[1]) == 'r' && tolower((unsigned char)text.data[2]) == 'u' && tolower((unsigned char)text.data[3]) == 'e') emit = true;
      else if (text.size == 5 && tolower((unsigned char)text.data[0]) == 'f' &&
          tolower((unsigned char)text.data[1]) == 'a' && tolower((unsigned char)text.data[2]) == 'l' &&
          tolower((unsigned char)text.data[3]) == 's' && tolower((unsigned char)text.data[4]) == 'e') emit = false;
      else return session_error(s, SALTS_EINVAL, "Shell stream_output must be true or false");
    }
  }
  r = session_integer(s, node, "timeout", 30000, &timeout);
  if (r != SALTS_OK || timeout <= 0) return session_error(s, SALTS_EINVAL, "Shell timeout must be positive and an integer");
  if (!s->config.retainer) return session_error(s, SALTS_ENOTSUP, "C11 Shell requires an explicit Process recovery owner");
  if (s->config.environment) {
    r = s->config.environment(s->config.process_user, &environment); if (r != SALTS_OK) return r;
  }
  /* Fetch cached views only after every pool mutation. Salts copies options
   * and the driver copies binary stdin before admitting native operations. */
  command = session_cached(s, node, SESSION_SHELL_CMD);
  input = session_cached(s, node, SESSION_SHELL_INPUT);
  cwd = session_cached(s, node, SESSION_SHELL_CWD);
  cmeta_process_options_init(&options);
  options.program = command.data; options.env = environment;
  options.cwd = cwd.size ? cwd.data : NULL;
  options.flags = SALTS_PROCESS_SHELL_COMMAND; options.timeout_ms = (uint64_t)timeout;
  if (emit && s->config.emit) {
    if (s->config.max_value_bytes < 2 || command.size > s->config.max_value_bytes - 2) return SALTS_ENOBUFS;
    r = session_buffer_reserve(&s->scratch[0], command.size + 2, s->config.max_value_bytes);
    if (r != SALTS_OK) return r;
    memcpy(s->scratch[0].data, "$ ", 2); memcpy(s->scratch[0].data + 2, command.data, command.size);
    s->scratch[0].data[command.size + 2] = '\0';
  }
  r = praktor_process_driver_start(&s->state[node].process, &options, input.data, input.size,
      s->config.max_value_bytes, s->config.retainer,
      emit ? s->config.emit : NULL, s->config.process_user);
  if (r != SALTS_OK) {
    praktor_process_view view = process_view(s, node);
    return session_error(s, r, "%s", view.error ? view.error : "C11 Shell recovery capacity unavailable");
  }
  if (emit && s->config.emit)
    r = s->config.emit(s->config.process_user, s->scratch[0].data, command.size + 2);
  return r;
}
static int tick_process(session_impl *s, size_t node, salts_bt_node_status *status) {
  bool done = false, success;
  int r, cleanup, exit_code;
  praktor_process_view view;
  if (!s->state[node].process.impl) {
    r = start_process(s, node);
    if (r != SALTS_OK) {
      if (!s->error[0]) session_error(s, r, "Salts::Process Shell admission failed with status %d", r);
      (void)close_process(s, node);
      (void)publish_process(s, node, -1, false);
      finalize_process(s, node); *status = SALTS_BT_FAILURE; return SALTS_OK;
    }
  }
  s->state[node].leaf_owner = OWNER_PROCESS;
  r = praktor_process_driver_tick(&s->state[node].process, &done);
  if (r != SALTS_OK) {
    session_error(s, r, "C11 Process driver tick failed"); (void)close_process(s, node);
    finalize_process(s, node); *status = SALTS_BT_FAILURE; return SALTS_OK;
  }
  if (!done) { *status = SALTS_BT_RUNNING; return SALTS_OK; }
  view = process_view(s, node);
  success = !view.failed && !view.output_limited && view.result.state == SALTS_PROCESS_EXITED && view.result.exit_code == 0;
  exit_code = view.failed || view.output_limited ? -1 : view.result.exit_code;
  cleanup = close_process(s, node); view = process_view(s, node);
  if (!success || cleanup != SALTS_OK)
    session_error(s, SALTS_EIO, "%s", view.error && *view.error ? view.error :
      view.result.state == SALTS_PROCESS_TIMED_OUT ? "Shell command timed out" : "Shell command exited unsuccessfully");
  r = publish_process(s, node, exit_code, success && cleanup == SALTS_OK);
  finalize_process(s, node);
  *status = success && cleanup == SALTS_OK && r == SALTS_OK ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
  return SALTS_OK;
}
static praktor_c11_leaf_request request(session_impl *s, size_t node) {
  praktor_c11_session_node *n = &s->nodes[node];
  praktor_c11_leaf_request out = {node, n->action, n->parameters, n->parameter_count, s->state[node].branch, s};
  return out;
}
static void dispatch_halt(session_impl *s, size_t node) {
  praktor_c11_bt_host *h = &s->config.host;
  if (h->halt_typed_leaf) { praktor_c11_leaf_request req = request(s, node); h->halt_typed_leaf(h->user, &req); }
  else if (h->halt_leaf) h->halt_leaf(h->user, node);
}
static void halt_leaf(void *user, size_t node) {
  session_impl *s = user;
  unsigned char owner = s->state[node].leaf_owner;
  s->state[node].leaf_owner = OWNER_NONE;
  if (owner == OWNER_PROCESS || process_view(s, node).active) {
    (void)close_process(s, node); finalize_process(s, node);
  } else if (owner == OWNER_HOST || owner == OWNER_COMPATIBILITY) dispatch_halt(s, node);
}
static bool valid_status(salts_bt_node_status status) {
  return status == SALTS_BT_RUNNING || status == SALTS_BT_SUCCESS || status == SALTS_BT_FAILURE;
}
static int tick_leaf(void *user, size_t node, salts_bt_node_status *status) {
  session_impl *s = user;
  praktor_c11_bt_host *h = &s->config.host;
  praktor_c11_session_node *n = &s->nodes[node];
  unsigned char owner = s->state[node].leaf_owner;
  int r;
  if (n->terminal == PRAKTOR_C11_TERMINAL_SUCCESS || n->terminal == PRAKTOR_C11_TERMINAL_FAILURE) {
    *status = n->terminal == PRAKTOR_C11_TERMINAL_SUCCESS ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE; return SALTS_OK;
  }
  if (n->terminal == PRAKTOR_C11_TERMINAL_SWITCH_FAILURE) {
    size_t ignored;
    if (s->state[node].branch) return session_error(s, SALTS_ENOTSUP, "C11 Parallel empty Switch requires a host branch-aware selector");
    r = session_switch(s, node, &ignored); if (r != SALTS_OK) return session_error(s, r, "C11 empty Switch selector failed");
    *status = SALTS_BT_FAILURE; return SALTS_OK;
  }
  if (!s->state[node].branch) { r = session_restore_pipeline(s, node); if (r != SALTS_OK) return r; }
  if (owner == OWNER_PROCESS) return tick_process(s, node, status);
  if (owner != OWNER_COMPATIBILITY && (h->tick_leaf || h->tick_typed_leaf)) {
    if (h->tick_typed_leaf) { praktor_c11_leaf_request req = request(s, node); r = h->tick_typed_leaf(h->user, &req, status); }
    else r = h->tick_leaf(h->user, node, status);
    if (r == SALTS_OK && valid_status(*status)) {
      s->state[node].leaf_owner = *status == SALTS_BT_RUNNING ? OWNER_HOST : OWNER_NONE; return SALTS_OK;
    }
    if (r != SALTS_ENOTSUP || owner == OWNER_HOST) {
      s->state[node].leaf_owner = OWNER_NONE; dispatch_halt(s, node);
      if (r == SALTS_ENOTSUP) return session_error(s, r, "host leaf hook declined active node '%s'; refusing to switch execution owners", n->action);
      if (r != SALTS_OK) return session_error(s, r, "host leaf hook returned status %d for node '%s'", r, n->action);
      return session_error(s, SALTS_EINVAL, "host leaf hook returned an invalid node status for '%s'", n->action);
    }
  }
  if (owner != OWNER_COMPATIBILITY) {
    if (h->value_get && strcmp(n->action, "Shell"))
      return session_error(s, SALTS_ENOTSUP, "host-owned C11 values require host leaf dispatch; refusing legacy Executor fallback for '%s'", n->action);
    if (s->state[node].branch) return session_error(s, SALTS_ENOTSUP, "C11 Parallel leaf '%s' requires host branch-aware tick_leaf dispatch; refusing shared Blackboard/ExecutionContext fallback", n->action);
    if (!strcmp(n->action, "Shell")) return tick_process(s, node, status);
  }
  if (!s->config.compatibility_tick) return session_error(s, SALTS_ENOTSUP, "C11 leaf '%s' has no explicit compatibility dispatcher", n->action);
  r = s->config.compatibility_tick(s->config.compatibility_user, node, status);
  if (r != SALTS_OK) return r;
  if (!valid_status(*status)) return SALTS_EINVAL;
  if (*status == SALTS_BT_RUNNING && !h->halt_leaf && !h->halt_typed_leaf)
    return session_error(s, SALTS_ENOTSUP, "registered action leaf '%s' returned RUNNING without a halt_leaf hook", n->action);
  s->state[node].leaf_owner = *status == SALTS_BT_RUNNING ? OWNER_COMPATIBILITY : OWNER_NONE;
  return SALTS_OK;
}
/* All Parallel data and commit policy belongs to explicit branch-aware hooks. */
static int parallel_enter(void *user, size_t n, size_t child, bool first) {
  session_impl *s = user; int r = session_restore_pipeline(s,n);
  return r == SALTS_OK ? session_hook_status(s,s->config.host.parallel_enter(s->config.host.user,n,child,first),"C11 Parallel enter hook failed") : r;
}
static int parallel_step(void *user, size_t n, size_t child, salts_bt_node_status status) {
  session_impl *s = user; int r = session_restore_pipeline(s,n);
  return r == SALTS_OK ? session_hook_status(s,s->config.host.parallel_step(s->config.host.user,n,child,status),"C11 Parallel step hook failed") : r;
}
static int parallel_join(void *user, size_t n, salts_bt_node_status status) {
  session_impl *s = user; int r = session_restore_pipeline(s,n);
  return r == SALTS_OK ? session_hook_status(s,s->config.host.parallel_join(s->config.host.user,n,status),"C11 Parallel join hook failed") : r;
}
static int parallel_finish(void *user, size_t n, size_t child, salts_bt_node_status status) {
  session_impl *s = user; int r = session_restore_pipeline(s,n);
  return r == SALTS_OK ? session_hook_status(s,s->config.host.parallel_finish(s->config.host.user,n,child,status),"C11 Parallel finish hook failed") : r;
}
static void parallel_abort(void *user, size_t n, size_t child) {
  session_impl *s = user; s->config.host.parallel_abort(s->config.host.user,n,child);
}
static bool value_contract(const praktor_c11_bt_host *h) {
  bool any = h->value_get || h->value_set || h->value_contains;
  return !any || (h->value_get && h->value_set && h->value_contains && h->value_user);
}
static int contracts(session_impl *s, const praktor_c11_session_node *nodes) {
  const praktor_c11_bt_host *h = &s->config.host;
  size_t i;
  bool tick = h->tick_leaf || h->tick_typed_leaf, halt = h->halt_leaf || h->halt_typed_leaf;
  if ((h->tick_leaf && h->tick_typed_leaf) || (h->halt_leaf && h->halt_typed_leaf) ||
      (h->halt_typed_leaf && !h->tick_typed_leaf) || (tick && !halt) ||
      !value_contract(h) || !value_contract(&s->config.compatibility_values)) return SALTS_EINVAL;
  if (h->value_get && (!tick || !halt)) return SALTS_EINVAL;
  if (!h->value_get && !s->config.compatibility_values.value_get) return SALTS_EINVAL;
  if (!s->config.compatibility_tick && (!h->value_get || !tick || !halt)) return SALTS_EINVAL;
  for (i = 0; i < s->count; ++i) {
    salts_bt_node_kind k = nodes[i].tree.kind;
    if (k == SALTS_BT_NODE_PARALLEL && nodes[i].tree.parallel_hooks &&
        (!h->parallel_enter || !h->parallel_step || !h->parallel_join || !h->parallel_finish || !h->parallel_abort)) return SALTS_EINVAL;
    if (k == SALTS_BT_NODE_PIPELINE_SEQUENCE && (h->pipeline_prepare || h->pipeline_complete || h->pipeline_abort) &&
        (!h->pipeline_prepare || !h->pipeline_complete || !h->pipeline_abort)) return SALTS_EINVAL;
    if (k == SALTS_BT_NODE_CONSUME_QUEUE && (h->queue_begin || h->queue_finish || h->queue_abort) &&
        (!h->queue_begin || !h->queue_finish || !h->queue_abort)) return SALTS_EINVAL;
  }
  return SALTS_OK;
}
void praktor_c11_session_config_init(praktor_c11_session_config *config) {
  if (!config) return;
  memset(config,0,sizeof(*config)); salts_bt_tree_config_init(&config->tree);
  config->max_plan_bytes = config->max_value_bytes = config->max_state_bytes = 16u*1024u*1024u;
  config->max_parameters_per_node = 64; config->shell_close_budget_ms = 3000;
}
static int add_bytes(size_t *used, size_t count, size_t width, size_t limit) {
  if (count > (limit - *used)/width) return SALTS_ENOBUFS;
  *used += count*width; return SALTS_OK;
}
static int string_bytes(const char *text, size_t *size, size_t limit) {
  size_t n;
  if (!text) return SALTS_EINVAL;
  for (n = 0; n < limit; ++n) if (!text[n]) { *size = n; return SALTS_OK; }
  return SALTS_ENOBUFS;
}
static char *copy_text(char **cursor, const char *text, size_t size) {
  char *start = *cursor; memcpy(start,text,size); start[size] = '\0'; *cursor += size+1; return start;
}
int praktor_c11_session_init(praktor_c11_session *session, const praktor_c11_session_config *config,
                             const praktor_c11_session_node *nodes, size_t count) {
  session_impl *s;
  praktor_c11_bt_host runtime = {0};
  praktor_c11_bt_node_desc *descs = NULL;
  size_t used = 0, text_bytes = 0, parameter_count = 0, i, j, n, pindex = 0;
  char *cursor;
  int r = SALTS_OK;
  if (!session || session->impl || !config || !nodes || !count || count > config->tree.max_nodes ||
      !config->max_plan_bytes || config->max_plan_bytes == SIZE_MAX || !config->max_value_bytes || !config->max_state_bytes ||
      config->max_value_bytes == SIZE_MAX || config->max_state_bytes == SIZE_MAX || !config->max_parameters_per_node)
    return SALTS_EINVAL;
  if (count > SIZE_MAX / SESSION_SLOTS / sizeof(session_slot)) return SALTS_ENOBUFS;
  if (add_bytes(&used,count,sizeof(*nodes),config->max_plan_bytes) != SALTS_OK) return SALTS_ENOBUFS;
  for (i = 0; i < count; ++i) {
    if (nodes[i].terminal < PRAKTOR_C11_TERMINAL_RUNTIME || nodes[i].terminal > PRAKTOR_C11_TERMINAL_SWITCH_FAILURE ||
        nodes[i].parameter_count > config->max_parameters_per_node || (nodes[i].parameter_count && !nodes[i].parameters)) return SALTS_EINVAL;
    r = string_bytes(nodes[i].action,&n,config->max_plan_bytes); if (r != SALTS_OK) return r;
    if (add_bytes(&text_bytes,n+1,1,config->max_plan_bytes) != SALTS_OK ||
        add_bytes(&parameter_count,nodes[i].parameter_count,1,config->max_plan_bytes) != SALTS_OK) return SALTS_ENOBUFS;
    for (j = 0; j < nodes[i].parameter_count; ++j) {
      const praktor_c11_leaf_parameter *p = &nodes[i].parameters[j];
      if (p->kind < PRAKTOR_C11_LEAF_STRING || p->kind > PRAKTOR_C11_LEAF_BOOLEAN) return SALTS_EINVAL;
      r = string_bytes(p->name,&n,config->max_plan_bytes); if (r != SALTS_OK) return r;
      if (add_bytes(&text_bytes,n+1,1,config->max_plan_bytes) != SALTS_OK) return SALTS_ENOBUFS;
      if (p->kind == PRAKTOR_C11_LEAF_STRING) {
        if (!p->text) return SALTS_EINVAL;
        if (nodes[i].text_sizes) n = nodes[i].text_sizes[j];
        else { r = string_bytes(p->text,&n,config->max_plan_bytes); if (r != SALTS_OK) return r; }
        if (n == SIZE_MAX || add_bytes(&text_bytes,n+1,1,config->max_plan_bytes) != SALTS_OK) return SALTS_ENOBUFS;
      }
    }
  }
  if (add_bytes(&used,parameter_count,sizeof(praktor_c11_leaf_parameter)+sizeof(size_t),config->max_plan_bytes) != SALTS_OK ||
      add_bytes(&used,text_bytes,1,config->max_plan_bytes) != SALTS_OK) return SALTS_ENOBUFS;
  s = calloc(1,sizeof(*s)); if (!s) return SALTS_ENOMEM;
  s->config = *config; s->count = count;
  r = contracts(s,nodes); if (r != SALTS_OK) goto fail;
  s->nodes = calloc(count,sizeof(*s->nodes)); s->state = calloc(count,sizeof(*s->state));
  s->slots = calloc(count*SESSION_SLOTS,sizeof(*s->slots)); s->plan_text = malloc(text_bytes);
  s->parameters = parameter_count ? calloc(parameter_count,sizeof(*s->parameters)) : NULL;
  s->text_sizes = parameter_count ? calloc(parameter_count,sizeof(*s->text_sizes)) : NULL;
  descs = calloc(count,sizeof(*descs));
  if (!s->nodes || !s->state || !s->slots || !s->plan_text || !descs || (parameter_count && (!s->parameters || !s->text_sizes))) { r = SALTS_ENOMEM; goto fail; }
  cursor = s->plan_text;
  for (i = 0; i < count; ++i) {
    s->nodes[i] = nodes[i]; descs[i] = nodes[i].tree;
    s->nodes[i].action = copy_text(&cursor,nodes[i].action,strlen(nodes[i].action));
    s->nodes[i].parameters = parameter_count ? s->parameters+pindex : NULL;
    s->nodes[i].text_sizes = parameter_count ? s->text_sizes+pindex : NULL;
    for (j = 0; j < nodes[i].parameter_count; ++j,++pindex) {
      const praktor_c11_leaf_parameter *p = &nodes[i].parameters[j];
      s->parameters[pindex] = *p;
      s->parameters[pindex].name = copy_text(&cursor,p->name,strlen(p->name));
      if (p->kind == PRAKTOR_C11_LEAF_STRING) {
        n = nodes[i].text_sizes ? nodes[i].text_sizes[j] : strlen(p->text);
        s->text_sizes[pindex] = n; s->parameters[pindex].text = copy_text(&cursor,p->text,n);
      } else s->parameters[pindex].text = NULL;
    }
  }
  runtime.tick_leaf = tick_leaf; runtime.halt_leaf = halt_leaf; runtime.condition = session_condition;
  runtime.select_child = session_select; runtime.resolve_dynamic_parameters = session_dynamic;
  runtime.parallel_enter = parallel_enter; runtime.parallel_step = parallel_step; runtime.parallel_join = parallel_join;
  runtime.parallel_finish = parallel_finish; runtime.parallel_abort = parallel_abort;
  runtime.pipeline_prepare = session_pipeline_prepare; runtime.pipeline_complete = session_pipeline_complete; runtime.pipeline_abort = session_pipeline_abort;
  runtime.entry_changed = session_entry_changed; runtime.queue_begin = session_queue_begin;
  runtime.queue_finish = session_queue_finish; runtime.queue_abort = session_queue_abort; runtime.user = s;
  r = praktor_c11_bt_tree_init(&s->runtime,&config->tree,descs,count,&runtime);
  if (r != SALTS_OK) goto fail;
  for (i = 1; i < count; ++i) {
    size_t parent = nodes[i].tree.parent_index;
    ++s->state[parent].child_count;
    s->state[i].branch = s->state[parent].branch || (nodes[parent].tree.kind == SALTS_BT_NODE_PARALLEL && nodes[parent].tree.parallel_hooks);
  }
  free(descs); session->impl = s; return SALTS_OK;
fail:
  free(descs); free_storage(s); return r;
}
static bool pending(session_impl *s) {
  size_t i;
  for (i = 0; i < s->count; ++i) { praktor_process_view view = process_view(s,i); if (view.active && view.closing) return true; }
  return false;
}
int praktor_c11_session_tick(praktor_c11_session *session, salts_bt_node_status *status) {
  session_impl *s = session ? session->impl : NULL;
  int r;
  if (!s || !status) return SALTS_EINVAL;
  if (s->busy) return SALTS_EBUSY;
  s->error[0] = '\0'; if (++s->generation == 0) ++s->generation;
  if (pending(s)) return session_error(s,SALTS_EBUSY,"C11 Shell shutdown is still pending recovery ownership");
  s->busy = true; r = praktor_c11_bt_tree_tick(&s->runtime,status); s->busy = false;
  return session_hook_status(s,r,"C11 behavior tree tick failed");
}
static int clear_execution(praktor_c11_session *session, bool cancel) {
  session_impl *s = session ? session->impl : NULL;
  size_t i;
  int r;
  if (!s) return SALTS_EINVAL;
  if (s->busy) return SALTS_EBUSY;
  s->error[0] = '\0'; s->busy = true;
  r = cancel ? praktor_c11_bt_tree_cancel(&s->runtime) : praktor_c11_bt_tree_reset(&s->runtime);
  s->busy = false;
  if (r != SALTS_OK) return session_hook_status(s,r,"C11 behavior tree reset/cancel failed");
  for (i = 0; i < s->count; ++i) {
    session_node_state *st = &s->state[i];
    st->pipeline = st->observed = st->queue_active = st->queue_more = false; st->restore_tick = 0;
    st->leaf_owner = process_view(s,i).active ? OWNER_PROCESS : OWNER_NONE;
    session_uncache(s,i,SESSION_PIPE_DATA); session_uncache(s,i,SESSION_PIPE_KEY); session_uncache(s,i,SESSION_ENTRY_VALUE);
  }
  if (pending(s)) return session_error(s,SALTS_EBUSY,"C11 Shell has active I/O leases for recovery owner");
  return s->error[0] ? SALTS_EIO : SALTS_OK;
}
int praktor_c11_session_reset(praktor_c11_session *s) { return clear_execution(s,false); }
int praktor_c11_session_cancel(praktor_c11_session *s) { return clear_execution(s,true); }
int praktor_c11_session_validate(const praktor_c11_session *session) {
  const session_impl *s = session ? session->impl : NULL;
  return s ? praktor_c11_bt_tree_validate(&s->runtime) : SALTS_EINVAL;
}
int praktor_c11_session_destroy(praktor_c11_session *session) {
  session_impl *s = session ? session->impl : NULL;
  size_t i;
  int r;
  if (!session) return SALTS_EINVAL;
  if (!s) return SALTS_OK;
  if (s->busy) return SALTS_EBUSY;
  s->busy = true;
  if (s->runtime.impl) {
    r = praktor_c11_bt_tree_destroy(&s->runtime);
    if (r != SALTS_OK) { s->busy = false; return r; }
  }
  for (i = 0; i < s->count; ++i) if (s->state[i].process.impl) {
    if (!process_view(s,i).closing) (void)close_process(s,i);
    r = praktor_process_driver_release(&s->state[i].process);
    if (r != SALTS_OK) { s->busy = false; return session_hook_status(s,r,"C11 Process recovery transfer failed"); }
  }
  session->impl = NULL; free_storage(s); return SALTS_OK;
}
const char *praktor_c11_session_error(const praktor_c11_session *session) {
  const session_impl *s = session ? session->impl : NULL;
  return s ? s->error : "";
}
