#include "praktor/service_control.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct service_impl {
  praktor_service_config config;
  const char **argv;
  char *metadata;
  size_t used;
  praktor_service_args commands[3];
  const char *program;
  char message[1024];
  bool busy;
} service_impl;
typedef struct execution {
  service_impl *s;
  const praktor_service_request *request;
  const praktor_service_host *host;
  praktor_service_result result;
  uint64_t began, budget;
} execution;
static const char *const status_args[] = {"query","{service_name}"};
static const char *const start_args[] = {"start","{service_name}","{arguments}"};
static const char *const stop_args[] = {"stop","{service_name}","{arguments}"};
static const praktor_service_profile windows_scm = {
  "sc.exe",{status_args,2},{start_args,3},{stop_args,3}
};
const praktor_service_profile *praktor_service_windows_scm_profile(void) { return &windows_scm; }
const char *praktor_service_state_name(praktor_service_state state) {
  switch (state) {
    case PRAKTOR_SERVICE_STOPPED: return "stopped";
    case PRAKTOR_SERVICE_START_PENDING: return "start_pending";
    case PRAKTOR_SERVICE_STOP_PENDING: return "stop_pending";
    case PRAKTOR_SERVICE_RUNNING: return "running";
    default: return "";
  }
}
int praktor_service_parse_state(const char *output, size_t size, praktor_service_state *out) {
  size_t begin = 0;
  if (!out || (!output && size)) return SALTS_EINVAL;
  while (begin < size) {
    size_t end = begin, p, digit;
    unsigned value = 0;
    while (end < size && output[end] != '\n') ++end;
    p = begin;
    while (p < end && (output[p] == ' ' || output[p] == '\t' || output[p] == '\r')) ++p;
    if (end-p >= 5 && !memcmp(output+p,"STATE",5) &&
        (end-p == 5 || output[p+5] == ':' || output[p+5] == ' ' || output[p+5] == '\t')) {
      p += 5;
      while (p < end && output[p] != ':') ++p;
      if (p == end) return SALTS_EINVAL;
      ++p; while (p < end && (output[p] == ' ' || output[p] == '\t')) ++p;
      /* from_chars accepts a minus (including -0), but never a plus. Valid
       * service states are positive, so a minus cannot yield a supported value. */
      digit = p;
      while (p < end && output[p] >= '0' && output[p] <= '9') {
        unsigned next = (unsigned)(output[p]-'0');
        if (value > ((unsigned)INT_MAX-next)/10u) return SALTS_EINVAL;
        value = value*10u+next; ++p;
      }
      if (p == digit || (p < end && !isspace((unsigned char)output[p])) || value < 1 || value > 4) return SALTS_EINVAL;
      *out = (praktor_service_state)value; return SALTS_OK;
    }
    begin = end+1u;
  }
  return SALTS_EINVAL;
}
int praktor_service_control_init(praktor_service_control *control, const praktor_service_config *config) {
  service_impl *s;
  if (!control || control->impl || !config || !config->max_arguments ||
      config->max_arguments > SIZE_MAX/(3u*sizeof(char *)) ||
      !config->max_metadata_bytes || !config->max_output_bytes) return SALTS_EINVAL;
  s = calloc(1,sizeof(*s)); if (!s) return SALTS_ENOMEM;
  s->config = *config;
  s->argv = calloc(3u*config->max_arguments,sizeof(char *));
  s->metadata = malloc(config->max_metadata_bytes);
  if (!s->argv || !s->metadata) { free(s->argv); free(s->metadata); free(s); return SALTS_ENOMEM; }
  control->impl = s; return SALTS_OK;
}
void praktor_service_control_destroy(praktor_service_control *control) {
  service_impl *s;
  if (!control || !control->impl) return;
  s = control->impl;
  /* A callback cannot destroy the active single-driver owner. */
  if (s->busy) return;
  free(s->argv); free(s->metadata); free(s); control->impl = NULL;
}
static uint64_t now(const execution *e) { return e->host->now_ns ? e->host->now_ns(e->host->user) : cmeta_hrtime(); }
static uint64_t elapsed(const execution *e) { return now(e)-e->began; }
static bool fail(execution *e, praktor_service_error code, const char *phase, const char *message, bool state) {
  e->result.error = code; e->result.phase = phase; e->result.message = message;
  e->result.include_state = state; return false;
}
static bool text_size(const service_impl *s, const char *text, size_t *size) {
  size_t n = 0;
  if (!text) return false;
  while (n < s->config.max_metadata_bytes && text[n]) ++n;
  if (n == s->config.max_metadata_bytes) return false;
  *size = n; return true;
}
static bool add(service_impl *s, const char *text, size_t size) {
  if (size > s->config.max_metadata_bytes-s->used) return false;
  if (size) memcpy(s->metadata+s->used,text,size);
  s->used += size; return true;
}
static bool copy(service_impl *s, const char *text, const char **out) {
  size_t size;
  if (!text_size(s,text,&size)) return false;
  *out = s->metadata+s->used;
  return add(s,text,size+1u);
}
static bool render(service_impl *s, praktor_service_args input, const praktor_service_request *request, size_t slot) {
  size_t i, count = 0, name_size;
  const char **args = s->argv+slot*s->config.max_arguments;
  if (!text_size(s,request->name,&name_size) || input.count > s->config.max_arguments ||
      (input.count && !input.values)) return false;
  for (i = 0; i < input.count; ++i) {
    const char *text = input.values[i]; size_t size, p = 0;
    if (!text_size(s,text,&size)) return false;
    if (!strcmp(text,"{arguments}")) {
      size_t j;
      for (j = 0; j < request->arguments.count; ++j) {
        if (count == s->config.max_arguments || !copy(s,request->arguments.values[j],&args[count])) return false;
        ++count;
      }
      continue;
    }
    if (count == s->config.max_arguments) return false;
    args[count++] = s->metadata+s->used;
    while (p < size) {
      if (size-p >= 14u && !memcmp(text+p,"{service_name}",14)) {
        if (!add(s,request->name,name_size)) return false;
        p += 14u;
      } else if (!add(s,text+p++,1)) return false;
    }
    if (!add(s,"",1)) return false;
  }
  /* Account for the native argv pointer array as well as rendered bytes,
   * before any command can reach the Process worker's admission bound. */
  if ((count+1u)*sizeof(char *) > s->config.max_metadata_bytes-s->used) return false;
  s->used += (count+1u)*sizeof(char *);
  s->commands[slot].values = args; s->commands[slot].count = count; return true;
}
static bool prepare(execution *e) {
  const praktor_service_request *r = e->request;
  service_impl *s = e->s;
  size_t size, i, input_bytes = 0;
  s->used = 0;
  if ((unsigned)r->operation > PRAKTOR_PROCESS_RESTART || !r->name || !*r->name ||
      !r->profile_name || !*r->profile_name || r->timeout_ms <= 0 || r->timeout_ms > INT32_MAX ||
      r->poll_interval_ms <= 0 || r->poll_interval_ms > INT32_MAX ||
      r->arguments.count > s->config.max_arguments || (r->arguments.count && !r->arguments.values))
    return fail(e,PRAKTOR_SERVICE_INVALID,"validate","service parameters are invalid",false);
  if (!text_size(s,r->name,&size)) goto limit;
  input_bytes = size+1u;
  if (!text_size(s,r->profile_name,&size) || size+1u > s->config.max_metadata_bytes-input_bytes) goto limit;
  input_bytes += size+1u;
  for (i = 0; i < r->arguments.count; ++i) {
    if (!text_size(s,r->arguments.values[i],&size) || size+1u > s->config.max_metadata_bytes-input_bytes) goto limit;
    input_bytes += size+1u;
  }
  if (!r->profile) {
    (void)snprintf(s->message,sizeof(s->message),"unknown service command profile: %.900s",r->profile_name);
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,"profile",s->message,false);
  }
  if (!r->profile->program || !*r->profile->program || !copy(s,r->profile->program,&s->program) ||
      !render(s,r->profile->status,r,0) || !render(s,r->profile->start,r,1) || !render(s,r->profile->stop,r,2)) goto limit;
  e->budget = (uint64_t)r->timeout_ms*UINT64_C(1000000); return true;
 limit:
  return fail(e,PRAKTOR_SERVICE_INVALID,"validate","service metadata exceeds configured bounds",false);
}
static bool run(execution *e, size_t slot, const char *phase, praktor_service_response *out) {
  uint64_t used = elapsed(e), remaining;
  praktor_service_command command;
  int status;
  if (used >= e->budget) return fail(e,PRAKTOR_SERVICE_TIMEOUT,phase,"service operation exceeded its deadline",false);
  remaining = (e->budget-used)/UINT64_C(1000000);
  command.program = e->s->program; command.arguments = e->s->commands[slot];
  command.timeout_ms = (uint32_t)(remaining ? remaining : 1u);
  command.max_output_bytes = e->s->config.max_output_bytes;
  memset(out,0,sizeof(*out)); out->state = PRAKTOR_PROCESS_WAIT_FAILED;
  status = e->host->run(e->host->user,&command,out);
  if (status != SALTS_OK) {
    const char *message = out->failure_message ? out->failure_message : "failed to run service command";
    char copied[sizeof(e->s->message)];
    (void)snprintf(copied,sizeof(copied),"%.1023s",message);
    memcpy(e->s->message,copied,strlen(copied)+1u);
    return fail(e,PRAKTOR_SERVICE_SPAWN_FAILED,phase,e->s->message,false);
  }
  if (out->state == PRAKTOR_PROCESS_SPAWN_FAILED) return fail(e,PRAKTOR_SERVICE_SPAWN_FAILED,phase,"failed to spawn service command",false);
  if (out->state == PRAKTOR_PROCESS_TIMED_OUT) return fail(e,PRAKTOR_SERVICE_TIMEOUT,phase,"service command timed out",false);
  if (out->state == PRAKTOR_PROCESS_CANCELLED) return fail(e,PRAKTOR_SERVICE_CANCELLED,phase,"service command was cancelled",false);
  if (out->state != PRAKTOR_PROCESS_EXITED || out->exit_code != 0) {
    e->result.include_process = true; e->result.process_state = out->state; e->result.exit_code = out->exit_code;
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,phase,"service command failed",false);
  }
  if (slot) e->result.changed = true;
  /* A late successful command may already have mutated the service. Preserve
   * changed, but never allow its result to authorize another command. */
  if (elapsed(e) >= e->budget) return fail(e,PRAKTOR_SERVICE_TIMEOUT,phase,"service operation exceeded its deadline",false);
  if ((!out->output && out->output_size) || out->output_size > e->s->config.max_output_bytes)
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,phase,"service command output exceeds configured bounds",false);
  return true;
}
static bool query(execution *e, const char *phase) {
  praktor_service_response response;
  praktor_service_state state;
  if (!run(e,0,phase,&response)) return false;
  if (praktor_service_parse_state(response.output,response.output_size,&state) != SALTS_OK)
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,phase,"service status did not contain a supported numeric STATE field",false);
  e->result.state = state; return true;
}
static bool poll_for(execution *e, praktor_service_state target) {
  for (;;) {
    uint64_t used = elapsed(e), remaining;
    uint32_t sleep;
    if (used >= e->budget) {
      (void)snprintf(e->s->message,sizeof(e->s->message),"service did not reach %s before the deadline",praktor_service_state_name(target));
      return fail(e,PRAKTOR_SERVICE_TIMEOUT,"poll",e->s->message,true);
    }
    remaining = (e->budget-used+UINT64_C(999999))/UINT64_C(1000000);
    sleep = (uint32_t)((uint64_t)e->request->poll_interval_ms < remaining ? (uint64_t)e->request->poll_interval_ms : remaining);
    if (e->host->sleep_ms) e->host->sleep_ms(e->host->user,sleep); else cmeta_sleep_ms(sleep);
    if (!query(e,"poll")) return false;
    if (e->result.state == target) return true;
  }
}
static bool start(execution *e) {
  praktor_service_response response;
  if (e->result.state == PRAKTOR_SERVICE_RUNNING) return true;
  if (e->result.state == PRAKTOR_SERVICE_STOP_PENDING)
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,"transition","cannot start a service while it is stopping",true);
  if (e->result.state == PRAKTOR_SERVICE_STOPPED && !run(e,1,"start",&response)) return false;
  return poll_for(e,PRAKTOR_SERVICE_RUNNING);
}
static bool stop(execution *e) {
  praktor_service_response response;
  if (e->result.state == PRAKTOR_SERVICE_STOPPED) return true;
  if (e->result.state == PRAKTOR_SERVICE_START_PENDING)
    return fail(e,PRAKTOR_SERVICE_STATE_FAILED,"transition","cannot stop a service while it is starting",true);
  if (e->result.state == PRAKTOR_SERVICE_RUNNING && !run(e,2,"stop",&response)) return false;
  return poll_for(e,PRAKTOR_SERVICE_STOPPED);
}
int praktor_service_control_execute(praktor_service_control *control, const praktor_service_request *request,
                                    const praktor_service_host *host, praktor_service_result *out) {
  execution e;
  bool okay;
  if (!control || !control->impl || !request || !host || !host->run || !out ||
      (!!host->now_ns != !!host->sleep_ms)) return SALTS_EINVAL;
  if (((service_impl *)control->impl)->busy) return SALTS_EBUSY;
  memset(&e,0,sizeof(e)); e.s = control->impl; e.request = request; e.host = host;
  e.result.phase = ""; e.result.message = ""; e.s->busy = true; e.began = now(&e);
  okay = prepare(&e) && query(&e,"query");
  if (okay) switch (request->operation) {
    case PRAKTOR_PROCESS_STATUS: break;
    case PRAKTOR_PROCESS_START: okay = start(&e); break;
    case PRAKTOR_PROCESS_STOP: okay = stop(&e); break;
    case PRAKTOR_PROCESS_RESTART:
      if (e.result.state == PRAKTOR_SERVICE_START_PENDING) okay = poll_for(&e,PRAKTOR_SERVICE_RUNNING);
      okay = okay && stop(&e) && start(&e); break;
  }
  e.result.ok = okay; e.result.duration_ms = elapsed(&e)/UINT64_C(1000000);
  *out = e.result; e.s->busy = false; return SALTS_OK;
}
