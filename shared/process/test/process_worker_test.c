#include "praktor/process_worker.h"
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct gate { cmeta_mutex_t mutex; cmeta_cond_t changed; bool entered, release; } gate;
static void gate_init(gate *g) {
  memset(g,0,sizeof(*g)); cmeta_mutex_init(&g->mutex); cmeta_cond_init(&g->changed);
  check(g->mutex != NULL && g->changed != NULL);
}
static int gated_setup(void *user) {
  gate *g = user;
  cmeta_mutex_lock(&g->mutex); g->entered = true; cmeta_cond_broadcast(&g->changed);
  while (!g->release) cmeta_cond_wait(&g->changed,&g->mutex);
  cmeta_mutex_unlock(&g->mutex); return SALTS_OK;
}
static bool gate_wait(gate *g) {
  bool entered; uint64_t started = cmeta_hrtime();
  cmeta_mutex_lock(&g->mutex);
  while (!g->entered && cmeta_hrtime()-started < UINT64_C(6000000000))
    (void)cmeta_cond_timedwait(&g->changed,&g->mutex,UINT64_C(10000000));
  entered = g->entered; cmeta_mutex_unlock(&g->mutex); return entered;
}
static void gate_release(gate *g) {
  cmeta_mutex_lock(&g->mutex); g->release = true; cmeta_cond_broadcast(&g->changed); cmeta_mutex_unlock(&g->mutex);
}
static void gate_destroy(gate *g) { cmeta_cond_destroy(&g->changed); cmeta_mutex_destroy(&g->mutex); }
static praktor_process_worker_config command(const char *text) {
  praktor_process_worker_config c; praktor_process_worker_config_init(&c);
  c.options.program = text; c.options.flags = SALTS_PROCESS_SHELL_COMMAND; c.options.timeout_ms = 10000;
  c.max_output_bytes = 65536; return c;
}
static const char *sleep_command(void) {
#if defined(_WIN32)
  return "ping -n 6 127.0.0.1 > NUL";
#else
  return "sleep 5";
#endif
}
static praktor_process_worker_view finish(praktor_process_worker *worker) {
  praktor_process_worker_view view;
  check(praktor_process_worker_wait(worker,UINT64_C(12000000000)) == SALTS_OK);
  check(praktor_process_worker_result(worker,&view) == SALTS_OK); return view;
}
static int reject_setup(void *user) { ++*(int *)user; return SALTS_EIO; }
static int reject_emit(void *user, const char *text, size_t size) {
  (void)user; (void)text; (void)size; return SALTS_EIO;
}
typedef struct observer { praktor_process_worker *worker; int error; praktor_process_state state; } observer;
static void observe(void *user) {
  observer *o = user; int r;
  do {
    praktor_process_worker_view view;
    if (praktor_process_worker_state(o->worker,&o->state) != SALTS_OK) { o->error = 1; return; }
    r = praktor_process_worker_result(o->worker,&view);
    if (r != SALTS_OK && r != SALTS_EBUSY) { o->error = 2; return; }
    r = praktor_process_worker_wait(o->worker,UINT64_C(1000000));
    if (r != SALTS_OK && r != SALTS_ETIMEDOUT) { o->error = 3; return; }
  } while (r != SALTS_OK);
  if (praktor_process_worker_state(o->worker,&o->state) != SALTS_OK) o->error = 4;
}
typedef struct reentrant { praktor_process_worker *worker; const void *caller, *driver; int error, calls; } reentrant;
static int remember_thread(void *user) {
  reentrant *r = user; r->driver = cmeta_thread_current_token();
  if (r->driver == r->caller) r->error = 1;
  return SALTS_OK;
}
static int reenter(void *user, const char *text, size_t size) {
  reentrant *r = user; praktor_process_worker_view view; praktor_process_state state;
  (void)text; (void)size; ++r->calls;
  if (cmeta_thread_current_token() != r->driver) r->error = 2;
  if (praktor_process_worker_state(r->worker,&state) != SALTS_OK) r->error = 3;
  if (praktor_process_worker_result(r->worker,&view) != SALTS_EBUSY) r->error = 4;
  if (praktor_process_worker_wait(r->worker,0) != SALTS_ETIMEDOUT) r->error = 5;
  if (praktor_process_worker_wait(r->worker,UINT64_MAX) != SALTS_EBUSY) r->error = 6;
  if (praktor_process_worker_destroy(r->worker) != SALTS_EBUSY || !r->worker->impl) r->error = 7;
  return SALTS_OK;
}
static int admitted(void *user, const char *text, size_t size) {
  gate *g = user; (void)text; (void)size;
  cmeta_mutex_lock(&g->mutex); g->entered = true; cmeta_cond_broadcast(&g->changed); cmeta_mutex_unlock(&g->mutex); return SALTS_OK;
}

spec("C11 joined Process worker without C++ execution state") {
  it("copies program and environment before callbacks can delay admission") {
    praktor_process_worker worker = {0}; gate g; bool entered;
#if defined(_WIN32)
    char program[] = "echo %PRAKTOR_WORKER_VALUE%";
#else
    char program[] = "printf '%s' \"$PRAKTOR_WORKER_VALUE\"";
#endif
    char environment[] = "PRAKTOR_WORKER_VALUE=original"; const char *env[] = {environment,NULL};
    praktor_process_worker_config c = command(program); praktor_process_worker_view view;
    gate_init(&g); c.options.env = env; c.thread_start = gated_setup; c.user = &g;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK);
    entered = gate_wait(&g);
    memset(program,'x',sizeof(program)-1); memset(environment,'y',sizeof(environment)-1); env[0] = NULL;
    gate_release(&g); view = finish(&worker);
    check(entered && view.state == PRAKTOR_PROCESS_EXITED && view.result.exit_code == 0);
    check(strstr(view.stdout_data,"original") != NULL);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK); gate_destroy(&g);
  }
  it("copies argv and binary stdin and drains both output streams") {
    praktor_process_worker worker = {0}; gate g; bool entered;
    char input[] = {'a',0,'b','\n','z'}; const char expected[] = {'a',0,'b','\n','z'};
#if defined(_WIN32)
    char script[] = "$i=[Console]::OpenStandardInput();$o=[Console]::OpenStandardOutput();$i.CopyTo($o);[Console]::Error.Write('stderr')";
    const char *args[] = {"-NoProfile","-Command",script,NULL};
    praktor_process_worker_config c = command("powershell.exe");
#else
    char script[] = "cat; printf stderr >&2";
    const char *args[] = {"-c",script,NULL};
    praktor_process_worker_config c = command("/bin/sh");
#endif
    praktor_process_worker_view view;
    gate_init(&g); c.options.flags = 0; c.options.args = args; c.thread_start = gated_setup; c.user = &g;
    c.input = input; c.input_size = c.max_input_bytes = sizeof(input);
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); entered = gate_wait(&g);
    memset(script,'x',sizeof(script)-1); memset(input,'x',sizeof(input)); args[0] = NULL;
    gate_release(&g); view = finish(&worker);
    check(entered && view.state == PRAKTOR_PROCESS_EXITED && view.result.exit_code == 0);
    check(view.stdout_size == sizeof(expected) && !memcmp(view.stdout_data,expected,sizeof(expected)));
    check(view.stderr_size == 6 && !memcmp(view.stderr_data,"stderr",6));
    check(praktor_process_worker_destroy(&worker) == SALTS_OK); gate_destroy(&g);
  }
  it("rejects metadata, argument, environment and stdin budgets before starting a thread") {
    praktor_process_worker worker = {0}; int calls = 0;
    const char *items[] = {"value",NULL};
    praktor_process_worker_config c = command("echo not-admitted");
    c.thread_start = reject_setup; c.user = &calls; c.max_metadata_bytes = 1;
    check(praktor_process_worker_start(&worker,&c) == SALTS_ENOBUFS && !worker.impl);
    c.max_metadata_bytes = 4096; c.options.args = items; c.max_arguments = 0;
    check(praktor_process_worker_start(&worker,&c) == SALTS_ENOBUFS && !worker.impl);
    c.options.args = NULL; c.options.env = items; c.max_environment_entries = 0;
    check(praktor_process_worker_start(&worker,&c) == SALTS_ENOBUFS && !worker.impl);
    c.options.env = NULL; c.input = "xx"; c.input_size = 2; c.max_input_bytes = 1;
    check(praktor_process_worker_start(&worker,&c) == SALTS_EINVAL && !worker.impl);
    check(calls == 0); check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("rejects setup failure without admitting a Process") {
    praktor_process_worker worker = {0}; int calls = 0;
    praktor_process_worker_config c = command("echo not-admitted"); praktor_process_worker_view view;
    c.thread_start = reject_setup; c.user = &calls;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(calls == 1 && view.state == PRAKTOR_PROCESS_WAIT_FAILED && view.result.pid == -1);
    check(strstr(view.error,"setup failed") != NULL);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("cancels before native admission and joins before callback storage is released") {
    praktor_process_worker worker = {0}; gate g; bool accepted, entered;
    praktor_process_worker_config c = command("echo not-admitted"); praktor_process_worker_view view;
    gate_init(&g); c.thread_start = gated_setup; c.user = &g;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); entered = gate_wait(&g);
    check(praktor_process_worker_cancel(&worker,&accepted) == SALTS_OK && accepted);
    check(praktor_process_worker_cancel(&worker,&accepted) == SALTS_OK && !accepted);
    gate_release(&g); view = finish(&worker);
    check(entered && view.state == PRAKTOR_PROCESS_CANCELLED && view.result.pid == -1 && view.stdout_size == 0);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK && !worker.impl); gate_destroy(&g);
  }
  it("broadcasts completion to concurrent observers without cancelling on a wait deadline") {
    praktor_process_worker worker = {0}; praktor_process_worker_view view;
    praktor_process_worker_config c = command(sleep_command()); bool accepted;
    cmeta_thread_t threads[3] = {0}; observer observers[3]; size_t i;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK);
    check(praktor_process_worker_wait(&worker,UINT64_C(1000000)) == SALTS_ETIMEDOUT);
    for (i = 0; i < 3; ++i) { observers[i].worker = &worker; observers[i].error = 0; check(cmeta_thread_create(&threads[i],observe,&observers[i]) == SALTS_OK); }
    check(praktor_process_worker_cancel(&worker,&accepted) == SALTS_OK && accepted);
    view = finish(&worker); check(view.state == PRAKTOR_PROCESS_CANCELLED && view.result.exit_code == -1);
    for (i = 0; i < 3; ++i) { check(cmeta_thread_join(&threads[i]) == SALTS_OK); check(!observers[i].error && observers[i].state == PRAKTOR_PROCESS_CANCELLED); }
    check(praktor_process_worker_cancel(&worker,&accepted) == SALTS_OK && !accepted);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("contains callback reentrancy and refuses driver-thread blocking or destruction") {
    praktor_process_worker worker = {0};
    reentrant r = {&worker,NULL,NULL,0,0}; praktor_process_worker_config c = command("echo reentrant");
    praktor_process_worker_view view; r.caller = cmeta_thread_current_token();
    c.thread_start = remember_thread; c.emit = reenter; c.user = &r;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(view.state == PRAKTOR_PROCESS_EXITED && !r.error && r.calls > 0 && view.output_streamed_live);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("drains a rejected stream callback and preserves bounded capture failure") {
    praktor_process_worker worker = {0}; praktor_process_worker_config c = command("echo rejected");
    praktor_process_worker_view view; c.emit = reject_emit;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(view.state == PRAKTOR_PROCESS_WAIT_FAILED && view.result.exit_code == -1);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
    c = command("echo too-large-for-budget"); c.max_output_bytes = 8;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(view.state == PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED && view.stdout_size+view.stderr_size <= 8);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("publishes native timeout and failed spawn through the same joined owner") {
    praktor_process_worker worker = {0}; praktor_process_worker_config c = command(sleep_command());
    praktor_process_worker_view view; c.options.timeout_ms = 20;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(view.state == PRAKTOR_PROCESS_TIMED_OUT && view.result.exit_code == -1);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
    c = command("praktor-worker-no-such-executable-7f2c"); c.options.flags = 0;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); view = finish(&worker);
    check(view.state == PRAKTOR_PROCESS_SPAWN_FAILED && view.result.pid == -1 && *view.error);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
  it("cancels and drains an admitted child before returning from destroy") {
    praktor_process_worker worker = {0}; gate *g = calloc(1,sizeof(*g)); bool entered;
#if defined(_WIN32)
    praktor_process_worker_config c = command("echo admitted && ping -n 6 127.0.0.1 > NUL");
#else
    praktor_process_worker_config c = command("printf 'admitted\\n'; sleep 5");
#endif
    check(g != NULL); gate_init(g); c.emit = admitted; c.user = g;
    check(praktor_process_worker_start(&worker,&c) == SALTS_OK); entered = gate_wait(g);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK && !worker.impl);
    gate_destroy(g); free(g); check(entered);
    check(praktor_process_worker_destroy(&worker) == SALTS_OK);
  }
}
