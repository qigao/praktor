#ifndef PRAKTOR_C11_SESSION_INTERNAL_H
#define PRAKTOR_C11_SESSION_INTERNAL_H
#include "session.h"
#include "praktor/process_driver.h"
typedef struct session_text { const char *data; size_t size; } session_text;
typedef struct session_buffer { char *data; size_t capacity; } session_buffer;
typedef struct session_slot { size_t offset, size; bool present; } session_slot;
enum { SESSION_PIPE_DATA, SESSION_PIPE_KEY, SESSION_ENTRY_VALUE,
       SESSION_SHELL_CMD, SESSION_SHELL_INPUT, SESSION_SHELL_CWD,
       SESSION_SHELL_OUT, SESSION_SHELL_ERR, SESSION_SHELL_EXIT, SESSION_SLOTS };
typedef struct session_node_state {
  bool branch, pipeline, observed, queue_active, queue_more;
  unsigned char leaf_owner;
  size_t child_count, active_child;
  uint64_t restore_tick;
  praktor_process_driver process;
} session_node_state;
typedef struct session_impl {
  praktor_c11_session_config config;
  praktor_c11_session_node *nodes;
  session_node_state *state;
  praktor_c11_leaf_parameter *parameters;
  size_t *text_sizes;
  char *plan_text;
  session_slot *slots;
  session_buffer scratch[4], pool;
  size_t count, used;
  uint64_t generation;
  bool busy;
  praktor_c11_bt_tree runtime;
  char error[512];
} session_impl;
int session_error(session_impl *s, int result, const char *format, ...);
int session_buffer_reserve(session_buffer *buffer, size_t bytes, size_t limit);
int session_get(session_impl *s, const char *key, size_t scratch, session_text *out);
int session_has(session_impl *s, const char *key, bool *out);
int session_set(session_impl *s, const char *key, session_text value);
int session_resolve(session_impl *s, size_t node, const char *name,
                    const char *default_text, size_t scratch, session_text *out);
const praktor_c11_leaf_parameter *session_parameter(session_impl *s, size_t node, const char *name);
int session_integer(session_impl *s, size_t node, const char *name, int fallback, int *out);
int session_switch(session_impl *s, size_t node, size_t *out);
int session_cache(session_impl *s, size_t node, size_t slot, session_text value);
session_text session_cached(session_impl *s, size_t node, size_t slot);
void session_uncache(session_impl *s, size_t node, size_t slot);
int session_key(session_impl *s, size_t node, const char *name, const char *fallback, size_t scratch, session_text *out);
int session_restore_pipeline(session_impl *s, size_t node);
int session_dynamic(void *user, size_t node, salts_bt_node_parameters *params);
int session_condition(void *user, size_t node, salts_bt_node_status *status);
int session_select(void *user, size_t node, size_t *child);
int session_pipeline_prepare(void *user, size_t node, size_t child);
int session_pipeline_complete(void *user, size_t node, size_t child, salts_bt_node_status status);
void session_pipeline_abort(void *user, size_t node, size_t child);
int session_entry_changed(void *user, size_t node, bool *changed);
int session_queue_begin(void *user, size_t node, bool *item);
int session_queue_finish(void *user, size_t node, bool *more);
void session_queue_abort(void *user, size_t node);
int session_hook_status(session_impl *s, int result, const char *operation);
#endif
