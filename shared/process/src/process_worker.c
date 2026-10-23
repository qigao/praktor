#include "praktor/process_worker.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct worker_state {
  cmeta_mutex_t mutex;
  cmeta_cond_t changed;
  cmeta_thread_t thread;
  const void *driver_token;
  bool completed, cancel_requested, streamed;
  praktor_process_state state;
  praktor_process_driver driver;
  cmeta_process_options_t options;
  char *metadata, *input;
  const char **arguments, **environment;
  size_t input_size, max_output_bytes;
  int (*thread_start)(void *user);
  praktor_process_emit emit;
  void *user;
  char error[192];
} worker_state;

const char *praktor_process_state_name(praktor_process_state state) {
  switch (state) {
    case PRAKTOR_PROCESS_STARTING: return "starting";
    case PRAKTOR_PROCESS_RUNNING: return "running";
    case PRAKTOR_PROCESS_CANCELLING: return "cancelling";
    case PRAKTOR_PROCESS_EXITED: return "exited";
    case PRAKTOR_PROCESS_SIGNALED: return "signaled";
    case PRAKTOR_PROCESS_TIMED_OUT: return "timed_out";
    case PRAKTOR_PROCESS_CANCELLED: return "cancelled";
    case PRAKTOR_PROCESS_SPAWN_FAILED: return "spawn_failed";
    case PRAKTOR_PROCESS_WAIT_FAILED: return "wait_failed";
    case PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED: return "output_limit_exceeded";
    default: return "unknown";
  }
}
void praktor_process_worker_config_init(praktor_process_worker_config *c) {
  if (!c) return;
  memset(c,0,sizeof(*c)); cmeta_process_options_init(&c->options);
  c->max_input_bytes = c->max_output_bytes = c->max_metadata_bytes = 16u*1024u*1024u;
  c->max_arguments = c->max_environment_entries = 4096;
}
static void free_admission(worker_state *s) {
  free(s->metadata); free(s->input); free(s->arguments); free(s->environment);
  s->metadata = s->input = NULL; s->arguments = s->environment = NULL;
  memset(&s->options,0,sizeof(s->options));
}
static void free_worker(worker_state *s) {
  free_admission(s); cmeta_cond_destroy(&s->changed); cmeta_mutex_destroy(&s->mutex); free(s);
}
static int cancelled(void *user) {
  worker_state *s = user; int result;
  cmeta_mutex_lock(&s->mutex); result = s->cancel_requested ? 1 : 0; cmeta_mutex_unlock(&s->mutex); return result;
}
static praktor_process_state terminal(cmeta_process_state_t state) {
  switch (state) {
    case SALTS_PROCESS_EXITED: return PRAKTOR_PROCESS_EXITED;
    case SALTS_PROCESS_SIGNALED: return PRAKTOR_PROCESS_SIGNALED;
    case SALTS_PROCESS_TIMED_OUT: return PRAKTOR_PROCESS_TIMED_OUT;
    case SALTS_PROCESS_TERMINATED: return PRAKTOR_PROCESS_CANCELLED;
    case SALTS_PROCESS_OUTPUT_LIMIT_EXCEEDED: return PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED;
    default: return PRAKTOR_PROCESS_WAIT_FAILED;
  }
}
static void run_worker(void *user) {
  worker_state *s = user;
  praktor_process_view view = {0};
  praktor_process_state state = PRAKTOR_PROCESS_SPAWN_FAILED;
  bool spawned = false;
  int r = SALTS_OK;
  cmeta_mutex_lock(&s->mutex); s->driver_token = cmeta_thread_current_token(); cmeta_mutex_unlock(&s->mutex);
  if (s->thread_start) r = s->thread_start(s->user);
  if (r != SALTS_OK) {
    state = PRAKTOR_PROCESS_WAIT_FAILED;
    (void)snprintf(s->error,sizeof(s->error),"Process worker thread setup failed (status %d)",r);
  } else if (cancelled(s)) state = PRAKTOR_PROCESS_CANCELLED;
  else {
    r = praktor_process_driver_start(&s->driver,&s->options,s->input,s->input_size,
                                     s->max_output_bytes,NULL,s->emit,s->user);
    spawned = r == SALTS_OK;
    /* Process has copied all metadata and stdin before admission returns. */
    free_admission(s);
    if (spawned) {
      cmeta_mutex_lock(&s->mutex);
      if (!s->cancel_requested) s->state = PRAKTOR_PROCESS_RUNNING;
      cmeta_mutex_unlock(&s->mutex);
      r = praktor_process_driver_run(&s->driver,cancelled,s);
    }
    if (s->driver.impl) {
      (void)praktor_process_driver_drain(&s->driver);
      (void)praktor_process_driver_view(&s->driver,&view);
      state = !spawned ? PRAKTOR_PROCESS_SPAWN_FAILED : view.output_limited
          ? PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED : view.failed || r != SALTS_OK
          ? PRAKTOR_PROCESS_WAIT_FAILED : terminal(view.result.state);
    } else (void)snprintf(s->error,sizeof(s->error),"Salts::Process admission failed (status %d)",r);
  }
  free_admission(s);
  cmeta_mutex_lock(&s->mutex);
  /* Decide cancellation and publish completion atomically with observers. */
  if (spawned && s->cancel_requested && state != PRAKTOR_PROCESS_TIMED_OUT &&
      state != PRAKTOR_PROCESS_WAIT_FAILED && state != PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED)
    state = PRAKTOR_PROCESS_CANCELLED;
  s->state = state; s->completed = true;
  cmeta_cond_broadcast(&s->changed); cmeta_mutex_unlock(&s->mutex);
}
static int measure_string(const char *text, size_t *used, size_t limit) {
  size_t n;
  if (!text) return SALTS_EINVAL;
  for (n = 0; n < limit-*used; ++n) if (!text[n]) { *used += n+1; return SALTS_OK; }
  return SALTS_ENOBUFS;
}
static int measure_list(const char *const *list, size_t limit, size_t *count, size_t *bytes, size_t budget) {
  size_t i; int r;
  *count = 0;
  if (!list) return SALTS_OK;
  for (i = 0; i <= limit; ++i) {
    if (!list[i]) {
      if (i+1 > (budget-*bytes)/sizeof(char *)) return SALTS_ENOBUFS;
      *bytes += (i+1)*sizeof(char *); *count = i; return SALTS_OK;
    }
    if (i == limit) break;
    r = measure_string(list[i],bytes,budget); if (r != SALTS_OK) return r;
  }
  return SALTS_ENOBUFS;
}
static char *copy_string(char **cursor, const char *text) {
  char *result = *cursor; size_t size = strlen(text)+1;
  memcpy(result,text,size); *cursor += size; return result;
}
int praktor_process_worker_start(praktor_process_worker *worker, const praktor_process_worker_config *c) {
  worker_state *s;
  size_t bytes = 0, args = 0, env = 0, i;
  char *cursor;
  int r;
  if (!worker || worker->impl || !c || !c->options.program || !*c->options.program ||
      (c->input_size && !c->input) || c->input_size > c->max_input_bytes || c->input_size == SIZE_MAX ||
      !c->max_metadata_bytes || c->max_metadata_bytes == SIZE_MAX || !c->max_output_bytes ||
      c->max_output_bytes > (SIZE_MAX-1)/4 || c->max_arguments >= SIZE_MAX/sizeof(char *) ||
      c->max_environment_entries >= SIZE_MAX/sizeof(char *)) return SALTS_EINVAL;
  r = measure_string(c->options.program,&bytes,c->max_metadata_bytes);
  if (r == SALTS_OK && c->options.cwd) r = measure_string(c->options.cwd,&bytes,c->max_metadata_bytes);
  if (r == SALTS_OK) r = measure_list(c->options.args,c->max_arguments,&args,&bytes,c->max_metadata_bytes);
  if (r == SALTS_OK) r = measure_list(c->options.env,c->max_environment_entries,&env,&bytes,c->max_metadata_bytes);
  if (r != SALTS_OK) return r;
  s = calloc(1,sizeof(*s)); if (!s) return SALTS_ENOMEM;
  bytes -= (c->options.args ? (args+1)*sizeof(char *) : 0);
  bytes -= (c->options.env ? (env+1)*sizeof(char *) : 0);
  s->metadata = malloc(bytes); s->input = malloc(c->input_size ? c->input_size : 1);
  s->arguments = c->options.args ? calloc(args+1,sizeof(*s->arguments)) : NULL;
  s->environment = c->options.env ? calloc(env+1,sizeof(*s->environment)) : NULL;
  if (!s->metadata || !s->input || (c->options.args && !s->arguments) || (c->options.env && !s->environment)) { free_worker(s); return SALTS_ENOMEM; }
  cursor = s->metadata; s->options = c->options;
  s->options.program = copy_string(&cursor,c->options.program);
  if (c->options.cwd) s->options.cwd = copy_string(&cursor,c->options.cwd);
  for (i = 0; i < args; ++i) s->arguments[i] = copy_string(&cursor,c->options.args[i]);
  for (i = 0; i < env; ++i) s->environment[i] = copy_string(&cursor,c->options.env[i]);
  s->options.args = s->arguments; s->options.env = s->environment;
  if (c->input_size) memcpy(s->input,c->input,c->input_size);
  s->input_size = c->input_size; s->max_output_bytes = c->max_output_bytes;
  s->emit = c->emit; s->user = c->user; s->thread_start = c->thread_start;
  s->streamed = c->emit != NULL; s->state = PRAKTOR_PROCESS_STARTING;
  cmeta_mutex_init(&s->mutex); cmeta_cond_init(&s->changed);
  if (!s->mutex || !s->changed) { free_worker(s); return SALTS_ENOMEM; }
  /* Publish before thread create: setup callbacks may observe this handle.
   * No caller may race start or destroy while start is still returning. */
  worker->impl = s;
  r = cmeta_thread_create(&s->thread,run_worker,s);
  if (r != SALTS_OK) { worker->impl = NULL; free_worker(s); }
  return r;
}
int praktor_process_worker_state(const praktor_process_worker *worker, praktor_process_state *state) {
  worker_state *s = worker ? worker->impl : NULL;
  if (!s || !state) return SALTS_EINVAL;
  cmeta_mutex_lock(&s->mutex); *state = s->state; cmeta_mutex_unlock(&s->mutex); return SALTS_OK;
}
int praktor_process_worker_cancel(praktor_process_worker *worker, bool *accepted) {
  worker_state *s = worker ? worker->impl : NULL;
  if (!s || !accepted) return SALTS_EINVAL;
  cmeta_mutex_lock(&s->mutex); *accepted = !s->completed && !s->cancel_requested;
  if (*accepted) { s->cancel_requested = true; s->state = PRAKTOR_PROCESS_CANCELLING; }
  cmeta_mutex_unlock(&s->mutex); return SALTS_OK;
}
int praktor_process_worker_wait(const praktor_process_worker *worker, uint64_t timeout_ns) {
  worker_state *s = worker ? worker->impl : NULL;
  uint64_t started = cmeta_hrtime(), elapsed, slice;
  int r = SALTS_OK;
  if (!s) return SALTS_EINVAL;
  cmeta_mutex_lock(&s->mutex);
  if (!s->completed && timeout_ns && s->driver_token == cmeta_thread_current_token()) r = SALTS_EBUSY;
  else while (!s->completed) {
    elapsed = cmeta_hrtime()-started;
    if (timeout_ns != UINT64_MAX && elapsed >= timeout_ns) { r = SALTS_ETIMEDOUT; break; }
    slice = UINT64_C(1000000000);
    if (timeout_ns != UINT64_MAX && timeout_ns-elapsed < slice) slice = timeout_ns-elapsed;
    r = cmeta_cond_timedwait(&s->changed,&s->mutex,slice);
    if (r && r != -ETIMEDOUT) break;
    r = SALTS_OK;
  }
  cmeta_mutex_unlock(&s->mutex); return r;
}
static const char *diagnostic(praktor_process_state state) {
  switch (state) {
    case PRAKTOR_PROCESS_TIMED_OUT: return "Command timed out";
    case PRAKTOR_PROCESS_CANCELLED: return "Command cancelled";
    case PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED: return "Captured process output exceeded the configured limit";
    case PRAKTOR_PROCESS_WAIT_FAILED: return "Salts::Process I/O or cleanup failed";
    default: return NULL;
  }
}
int praktor_process_worker_result(const praktor_process_worker *worker, praktor_process_worker_view *out) {
  worker_state *s = worker ? worker->impl : NULL;
  praktor_process_view view = {0};
  if (!s || !out) return SALTS_EINVAL;
  cmeta_mutex_lock(&s->mutex);
  if (!s->completed) { cmeta_mutex_unlock(&s->mutex); return SALTS_EBUSY; }
  if (s->driver.impl) (void)praktor_process_driver_view(&s->driver,&view);
  else { view.result.pid = -1; view.result.exit_code = -1; }
  out->state = s->state; out->result = view.result;
  out->stdout_data = view.stdout_data ? view.stdout_data : ""; out->stdout_size = view.stdout_size;
  out->stderr_data = view.stderr_data ? view.stderr_data : ""; out->stderr_size = view.stderr_size;
  out->error = s->error[0] ? s->error : view.error ? view.error : "";
  out->diagnostic = diagnostic(s->state); out->output_streamed_live = s->streamed;
  if (s->state == PRAKTOR_PROCESS_SPAWN_FAILED) { out->stderr_data = out->error; out->stderr_size = strlen(out->error); }
  if (out->diagnostic) out->result.exit_code = -1;
  cmeta_mutex_unlock(&s->mutex); return SALTS_OK;
}
int praktor_process_worker_destroy(praktor_process_worker *worker) {
  worker_state *s = worker ? worker->impl : NULL;
  bool accepted;
  int r;
  if (!worker) return SALTS_EINVAL;
  if (!s) return SALTS_OK;
  cmeta_mutex_lock(&s->mutex);
  if (!s->completed && s->driver_token == cmeta_thread_current_token()) { cmeta_mutex_unlock(&s->mutex); return SALTS_EBUSY; }
  cmeta_mutex_unlock(&s->mutex);
  (void)praktor_process_worker_cancel(worker,&accepted);
  if (s->thread) { r = cmeta_thread_join(&s->thread); if (r != SALTS_OK) return r; }
  if (s->driver.impl) { r = praktor_process_driver_release(&s->driver); if (r != SALTS_OK) return r; }
  worker->impl = NULL; free_worker(s); return SALTS_OK;
}
