#include "praktor/process_control.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <limits.h>
#include <string.h>

typedef struct control {
  const praktor_process_control_request *request;
  const praktor_process_control_host *host;
  praktor_process_control_result *result;
  uint64_t began;
} control;
static uint64_t now(control *c) {
  return c->host->now_ns ? c->host->now_ns(c->host->user) : cmeta_hrtime();
}
static bool fail(control *c, praktor_process_control_error error,
                  const char *phase, const char *message, int native_error) {
  c->result->error = error; c->result->phase = phase;
  c->result->message = message ? message : ""; c->result->native_error = native_error;
  return false;
}
static bool backend(control *c, praktor_process_backend_result r, const char *phase) {
  return r.ok || fail(c, r.message && strcmp(r.message,"unsupported_platform") == 0
      ? PRAKTOR_PROCESS_CONTROL_UNSUPPORTED_PLATFORM : PRAKTOR_PROCESS_CONTROL_BACKEND_FAILURE,
      phase,r.message,r.native_error);
}
static bool query(control *c, const char *phase) {
  praktor_process_snapshot snapshot = {0};
  if (!backend(c,c->host->query(c->host->user,c->request->image_name,&snapshot),phase)) return false;
  c->result->snapshot = snapshot;
  return true;
}
static bool expired(control *c, uint64_t began, uint64_t budget) {
  return now(c)-began >= budget;
}
static void pause_poll(control *c, uint64_t began, uint64_t budget) {
  uint64_t elapsed = now(c)-began, ms;
  if (elapsed >= budget) return;
  ms = (budget-elapsed)/UINT64_C(1000000);
  if (ms > c->host->poll_interval_ms) ms = c->host->poll_interval_ms;
  if (ms > UINT32_MAX) ms = UINT32_MAX;
  if (c->host->sleep_ms) c->host->sleep_ms(c->host->user,(uint32_t)ms);
  else cmeta_sleep_ms((uint32_t)ms);
}
static bool poll_for_stopped(control *c, uint64_t began, uint64_t budget, const char *phase) {
  for (;;) {
    if (expired(c,began,budget)) break;
    if (!query(c,phase)) return false;
    if (expired(c,began,budget)) break;
    if (!c->result->snapshot.running) return true;
    pause_poll(c,began,budget);
  }
  return fail(c,PRAKTOR_PROCESS_CONTROL_TIMEOUT,phase,
              "managed process did not reach not_running before the deadline",0);
}
static bool start(control *c) {
  uint64_t began, budget = (uint64_t)c->request->startup_timeout_ms*UINT64_C(1000000);
  praktor_process_snapshot started = {0};
  if (c->result->snapshot.running) return true;
  began = now(c);
  if (!backend(c,c->host->start(c->host->user,c->request,&started),"start")) return false;
  c->result->snapshot = started; c->result->changed = true;
  for (;;) {
    if (!query(c,"start_poll")) return false;
    if (c->result->snapshot.running && !expired(c,began,budget)) return true;
    if (expired(c,began,budget)) break;
    pause_poll(c,began,budget);
  }
  if (!query(c,"start_timeout_query")) { c->result->snapshot = started; return false; }
  if (c->result->snapshot.running) {
    /* Compensate only the original PID/session/token/image, never a replacement. */
    if (!backend(c,c->host->terminate(c->host->user,&started),"start_timeout_terminate")) return false;
    if (!poll_for_stopped(c,now(c),(uint64_t)c->request->stop_timeout_ms*UINT64_C(1000000),
                          "start_timeout_cleanup_poll")) return false;
  }
  return fail(c,PRAKTOR_PROCESS_CONTROL_TIMEOUT,"start_poll",
              "managed process startup exceeded the deadline",0);
}
static bool stop(control *c) {
  uint64_t budget = (uint64_t)c->request->stop_timeout_ms*UINT64_C(1000000);
  if (!c->result->snapshot.running) return true;
  if (!backend(c,c->host->request_stop(c->host->user,&c->result->snapshot),"request_stop")) return false;
  c->result->changed = true;
  if (poll_for_stopped(c,now(c),budget,"stop_poll")) return true;
  if (c->result->error != PRAKTOR_PROCESS_CONTROL_TIMEOUT || !c->request->force_terminate) return false;
  c->result->error = PRAKTOR_PROCESS_CONTROL_NONE; c->result->native_error = 0;
  c->result->phase = ""; c->result->message = "";
  if (!query(c,"force_query")) return false;
  if (!c->result->snapshot.running) return true;
  if (!backend(c,c->host->terminate(c->host->user,&c->result->snapshot),"terminate")) return false;
  return poll_for_stopped(c,now(c),budget,"terminate_poll");
}
int praktor_process_control_execute(const praktor_process_control_request *request,
                                    const praktor_process_control_host *host,
                                    praktor_process_control_result *result) {
  control c;
  bool valid;
  if (!request || !host || !result || !host->query || !host->start ||
      !host->request_stop || !host->terminate || !host->poll_interval_ms ||
      ((host->now_ns == NULL) != (host->sleep_ms == NULL))) return SALTS_EINVAL;
  memset(result,0,sizeof(*result)); result->phase = ""; result->message = "";
  c.request = request; c.host = host; c.result = result; c.began = now(&c);
  valid = request->operation >= PRAKTOR_PROCESS_STATUS && request->operation <= PRAKTOR_PROCESS_RESTART &&
      request->image_name && *request->image_name && request->startup_timeout_ms > 0 &&
      request->stop_timeout_ms > 0 &&
      (uint64_t)request->startup_timeout_ms <= UINT64_MAX/UINT64_C(1000000) &&
      (uint64_t)request->stop_timeout_ms <= UINT64_MAX/UINT64_C(1000000) &&
      ((request->operation != PRAKTOR_PROCESS_START && request->operation != PRAKTOR_PROCESS_RESTART) ||
       (request->executable && *request->executable));
  if (!valid) {
    fail(&c,PRAKTOR_PROCESS_CONTROL_INVALID_PARAMETERS,"validate","managed process parameters are invalid",0);
  } else if (query(&c,"query")) {
    switch (request->operation) {
      case PRAKTOR_PROCESS_STATUS: result->ok = true; break;
      case PRAKTOR_PROCESS_START: result->ok = start(&c); break;
      case PRAKTOR_PROCESS_STOP: result->ok = stop(&c); break;
      case PRAKTOR_PROCESS_RESTART: result->ok = stop(&c) && query(&c,"restart_query") && start(&c); break;
    }
  }
  result->duration_ms = (now(&c)-c.began)/UINT64_C(1000000);
  return SALTS_OK;
}
