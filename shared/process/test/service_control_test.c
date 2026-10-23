#include "praktor/service_control.h"
#include <tinytest.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fake {
  uint64_t clock, advance;
  int states[16]; size_t state_count, next, calls;
  char trace[64], output[128];
  praktor_process_state terminal;
  int exit_code, callback_status;
  bool rendering_ok, reenter, null_output;
  size_t output_size;
  praktor_service_control *control;
  const praktor_service_request *request;
  const praktor_service_host *host;
  int reentrant_status;
  uint32_t timeouts[64];
} fake;
static uint64_t now(void *user) { return ((fake *)user)->clock; }
static void sleep_ms(void *user, uint32_t ms) { ((fake *)user)->clock += (uint64_t)ms*UINT64_C(1000000); }
static int run(void *user, const praktor_service_command *command, praktor_service_response *out) {
  fake *f = user; const char *op = command->arguments.count ? command->arguments.values[0] : "";
  if (f->calls < 63) { f->trace[f->calls] = !strcmp(op,"stop") ? 't' : *op; f->timeouts[f->calls] = command->timeout_ms; }
  ++f->calls;
  if (command->arguments.count > 1 && strcmp(command->arguments.values[1],"Retro Camera Service")) f->rendering_ok = false;
  if (*op == 's' && strcmp(op,"stop") && (command->arguments.count != 5 ||
      strcmp(command->arguments.values[2],"space value") || strcmp(command->arguments.values[3],"quote\"\\end\\") ||
      strcmp(command->arguments.values[4],"--name=Retro Camera Service/Retro Camera Service"))) f->rendering_ok = false;
  if (f->reenter) {
    praktor_service_result ignored;
    f->reentrant_status = praktor_service_control_execute(f->control,f->request,f->host,&ignored);
    praktor_service_control_destroy(f->control);
    if (!f->control->impl) f->rendering_ok = false;
  }
  f->clock += f->advance;
  out->state = f->terminal; out->exit_code = f->exit_code;
  out->failure_message = "host failure";
  if (*op == 'q') {
    size_t index = f->next < f->state_count ? f->next++ : f->state_count-1u;
    (void)snprintf(f->output,sizeof(f->output),"SERVICE_NAME: fixture\r\n  STATE : %d localized text\r\n",f->states[index]);
  } else f->output[0] = '\0';
  out->output = f->null_output ? NULL : f->output; out->output_size = f->output_size ? f->output_size : strlen(f->output);
  return f->callback_status;
}
static const char *const status[] = {"query","{service_name}"};
static const char *const start_args[] = {"start","{service_name}","{arguments}","--name={service_name}/{service_name}"};
static const char *const stop_args[] = {"stop","{service_name}","{arguments}"};
static const char *const arguments[] = {"space value","quote\"\\end\\"};
static const praktor_service_profile profile = {"fake",{status,2},{start_args,4},{stop_args,3}};
static praktor_service_request request(praktor_process_operation operation) {
  const praktor_service_request r = {operation,"Retro Camera Service","fixture",&profile,{arguments,2},100,1}; return r;
}
static praktor_service_result execute(fake *f, praktor_service_request *r, size_t bytes) {
  praktor_service_control control = {0}; praktor_service_result result;
  const praktor_service_config config = {64,bytes,1024};
  const praktor_service_host host = {f,run,now,sleep_ms};
  f->terminal = f->terminal ? f->terminal : PRAKTOR_PROCESS_EXITED;
  f->rendering_ok = true; f->control = &control; f->request = r; f->host = &host;
  check(praktor_service_control_init(&control,&config) == SALTS_OK);
  check(praktor_service_control_execute(&control,r,&host,&result) == SALTS_OK);
  /* Copy the diagnostic into fake storage before destroying its owner. */
  (void)snprintf(f->output,sizeof(f->output),"%.127s",result.message); result.message = f->output;
  praktor_service_control_destroy(&control); return result;
}
spec("C service numeric STATE parser") {
  it("accepts numeric localized fields and rejects malformed or binary fields") {
    static const char *const invalid[] = {"", "STATE : RUNNING", "STATE : 4oops", "STATE : +4", "STATE : -4", "STATE : 0", "STATE : 5", "STATE : 2147483648", "STATE no colon", "STATE:\t", "STATEFOO : 4", "state : 4"};
    size_t i; praktor_service_state s = PRAKTOR_SERVICE_UNKNOWN;
    for (i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) check(praktor_service_parse_state(invalid[i],strlen(invalid[i]),&s) == SALTS_EINVAL);
    for (i = 1; i <= 4; ++i) {
      char output[128]; (void)snprintf(output,sizeof(output),"TYPE : 32\n\t STATE : 00%u LOCALIZED\r\n",(unsigned)i);
      check(praktor_service_parse_state(output,strlen(output),&s) == SALTS_OK && (size_t)s == i);
    }
    check(praktor_service_parse_state("STATE : 4\0bad",13,&s) == SALTS_EINVAL);
    check(praktor_service_parse_state(NULL,0,&s) == SALTS_EINVAL);
  }
}
spec("C service transition controller without C++") {
  it("is idempotent for all numeric status states and target states") {
    unsigned state;
    for (state = 1; state <= 4; ++state) {
      fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_STATUS); praktor_service_result result;
      f.states[0] = (int)state; f.state_count = 1; result = execute(&f,&r,4096);
      check(result.ok && (unsigned)result.state == state && !result.changed && f.calls == 1);
    }
    {
      fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
      f.states[0] = 4; f.state_count = 1; result = execute(&f,&r,4096);
      check(result.ok && !result.changed && f.calls == 1);
      memset(&f,0,sizeof(f)); r.operation = PRAKTOR_PROCESS_STOP; f.states[0] = 1; f.state_count = 1;
      result = execute(&f,&r,4096); check(result.ok && !result.changed && f.calls == 1);
    }
  }
  it("renders exact argv and admits one mutation while polling") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
    f.states[0] = 1; f.states[1] = 2; f.states[2] = 4; f.state_count = 3;
    result = execute(&f,&r,4096);
    check(result.ok && result.changed && result.state == PRAKTOR_SERVICE_RUNNING && f.rendering_ok);
    check(!strcmp(f.trace,"qsqq") && f.timeouts[0] == 100 && f.timeouts[2] == 99 && f.timeouts[3] == 98);
  }
  it("confirms restart stop before start, including an initially pending start") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_RESTART); praktor_service_result result;
    f.states[0] = 2; f.states[1] = 4; f.states[2] = 1; f.states[3] = 4; f.state_count = 4;
    result = execute(&f,&r,4096);
    check(result.ok && result.changed && f.rendering_ok && !strcmp(f.trace,"qqtqsq"));
  }
  it("rejects conflicting pending transitions without mutation") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
    f.states[0] = 3; f.state_count = 1; result = execute(&f,&r,4096);
    check(!result.ok && result.include_state && result.state == PRAKTOR_SERVICE_STOP_PENDING && !result.changed && f.calls == 1);
    memset(&f,0,sizeof(f)); r.operation = PRAKTOR_PROCESS_STOP; f.states[0] = 2; f.state_count = 1;
    result = execute(&f,&r,4096); check(!result.ok && result.include_state && f.calls == 1);
  }
  it("maps every terminal process state, exit code and callback failure") {
    praktor_process_state states[] = {PRAKTOR_PROCESS_SPAWN_FAILED,PRAKTOR_PROCESS_TIMED_OUT,PRAKTOR_PROCESS_CANCELLED,PRAKTOR_PROCESS_WAIT_FAILED,PRAKTOR_PROCESS_SIGNALED,PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED,PRAKTOR_PROCESS_EXITED};
    praktor_service_error errors[] = {PRAKTOR_SERVICE_SPAWN_FAILED,PRAKTOR_SERVICE_TIMEOUT,PRAKTOR_SERVICE_CANCELLED,PRAKTOR_SERVICE_STATE_FAILED,PRAKTOR_SERVICE_STATE_FAILED,PRAKTOR_SERVICE_STATE_FAILED,PRAKTOR_SERVICE_STATE_FAILED};
    size_t i;
    for (i = 0; i < sizeof(states)/sizeof(*states); ++i) {
      fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
      f.states[0] = 1; f.state_count = 1; f.terminal = states[i]; f.exit_code = 17;
      result = execute(&f,&r,4096); check(!result.ok && result.error == errors[i] && f.calls == 1 && !result.changed);
      if (i >= 3) check(result.include_process && result.exit_code == 17 && result.process_state == states[i]);
    }
    {
      fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_STATUS); praktor_service_result result;
      f.states[0] = 1; f.state_count = 1; f.callback_status = SALTS_EIO; result = execute(&f,&r,4096);
      check(!result.ok && result.error == PRAKTOR_SERVICE_SPAWN_FAILED && !strcmp(result.message,"host failure"));
    }
  }
  it("charges late queries against the deadline and preserves changed after a late mutation") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_STATUS); praktor_service_result result;
    f.states[0] = 4; f.state_count = 1; f.advance = UINT64_C(100000000); result = execute(&f,&r,4096);
    check(!result.ok && result.error == PRAKTOR_SERVICE_TIMEOUT && result.duration_ms == 100 && f.calls == 1);
    memset(&f,0,sizeof(f)); r.operation = PRAKTOR_PROCESS_START; f.states[0] = 1; f.state_count = 1; f.advance = UINT64_C(50000000);
    result = execute(&f,&r,4096); check(!result.ok && result.changed && result.error == PRAKTOR_SERVICE_TIMEOUT && f.calls == 2);
  }
  it("bounds polling, handles monotonic counter wrap and cannot reenter or destroy an active owner") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
    f.states[0] = 2; f.state_count = 1; r.timeout_ms = 3; result = execute(&f,&r,4096);
    check(!result.ok && result.error == PRAKTOR_SERVICE_TIMEOUT && result.duration_ms == 3 && f.calls == 3);
    memset(&f,0,sizeof(f)); r = request(PRAKTOR_PROCESS_STATUS); f.states[0] = 4; f.state_count = 1;
    f.clock = UINT64_MAX-UINT64_C(1000000); f.advance = UINT64_C(2000000); f.reenter = true;
    result = execute(&f,&r,4096); check(result.ok && result.duration_ms == 2 && f.reentrant_status == SALTS_EBUSY && f.rendering_ok);
  }
  it("validates every template and aggregate metadata before any command, including restart") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_RESTART); praktor_service_result result;
    result = execute(&f,&r,32); check(!result.ok && result.error == PRAKTOR_SERVICE_INVALID && f.calls == 0);
    r.operation = (praktor_process_operation)99; result = execute(&f,&r,4096);
    check(!result.ok && result.error == PRAKTOR_SERVICE_INVALID && f.calls == 0);
    r = request(PRAKTOR_PROCESS_STATUS); r.profile = NULL; result = execute(&f,&r,4096);
    check(!result.ok && result.error == PRAKTOR_SERVICE_STATE_FAILED && !strcmp(result.phase,"profile") && f.calls == 0);
    {
      const char *const bad[] = {NULL}; praktor_service_profile broken = profile;
      broken.start.values = bad; broken.start.count = 1; r.profile = &broken;
      result = execute(&f,&r,4096); check(!result.ok && f.calls == 0);
    }
  }
  it("rejects oversized or inconsistent host output before parsing") {
    fake f = {0}; praktor_service_request r = request(PRAKTOR_PROCESS_START); praktor_service_result result;
    f.states[0] = 1; f.state_count = 1; f.output_size = 1025;
    result = execute(&f,&r,4096); check(!result.ok && result.error == PRAKTOR_SERVICE_STATE_FAILED && f.calls == 1 && !result.changed);
    memset(&f,0,sizeof(f)); f.states[0] = 1; f.state_count = 1; f.null_output = true;
    result = execute(&f,&r,4096); check(!result.ok && f.calls == 1 && !result.changed);
  }
  it("validates ownership and rejects malformed API callbacks before dispatch") {
    praktor_service_control control = {0}; praktor_service_config config = {0,4096,1024};
    praktor_service_request r = request(PRAKTOR_PROCESS_STATUS); praktor_service_result result;
    fake f = {0}; praktor_service_host host = {&f,run,now,NULL};
    check(praktor_service_control_init(&control,&config) == SALTS_EINVAL);
    config.max_arguments = 8; check(praktor_service_control_init(&control,&config) == SALTS_OK);
    check(praktor_service_control_init(&control,&config) == SALTS_EINVAL);
    check(praktor_service_control_execute(&control,&r,&host,&result) == SALTS_EINVAL && f.calls == 0);
    praktor_service_control_destroy(&control); praktor_service_control_destroy(&control);
    check(!control.impl && !strcmp(praktor_service_windows_scm_profile()->program,"sc.exe"));
  }
}
