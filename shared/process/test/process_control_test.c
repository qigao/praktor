#include "praktor/process_control.h"
#include <tinytest.h>
#include <limits.h>
#include <string.h>

#define MS UINT64_C(1000000)
typedef struct fake {
  uint64_t now, start_delay, query_delay, sleep_cap;
  unsigned queries, starts, stops, terminates, sleeps, fail_query;
  bool running, graceful, terminate_stops, replacement, never_appears;
  praktor_process_backend_result start_error, stop_error, terminate_error, query_error;
  praktor_process_snapshot terminated;
  char query_image[64], start_image[64];
} fake;
static praktor_process_backend_result success(void) {
  const praktor_process_backend_result r = {true,0,""}; return r;
}
static praktor_process_snapshot snapshot(fake *f, bool started) {
  praktor_process_snapshot s = {f->running,0,0,0,started ? f->start_image : f->query_image};
  if (f->running) {
    s.pid = f->replacement && !started ? 200u : 100u;
    s.session_id = 7; s.instance_token = s.pid+1000u;
  }
  return s;
}
static praktor_process_backend_result query(void *user, const char *image, praktor_process_snapshot *out) {
  fake *f = user;
  check(strcmp(image,"worker.exe") == 0);
  ++f->queries; f->now += f->query_delay;
  if (f->queries == f->fail_query) return f->query_error;
  strcpy(f->query_image,f->replacement ? "replacement.exe" : "worker.exe");
  *out = snapshot(f,false); return success();
}
static praktor_process_backend_result start(void *user, const praktor_process_control_request *request,
                                           praktor_process_snapshot *out) {
  fake *f = user;
  check(strcmp(request->executable,"/bin/worker") == 0);
  check(request->argument_count == 2u && strcmp(request->arguments[1],"safe value") == 0);
  check(strcmp(request->working_directory,"/work") == 0);
  ++f->starts; f->now += f->start_delay;
  if (!f->start_error.ok) return f->start_error;
  f->running = !f->never_appears; strcpy(f->start_image,"worker.exe");
  *out = snapshot(f,true);
  /* Native admission identifies its child even before identity query sees it. */
  out->running = true; out->pid = 100; out->session_id = 7; out->instance_token = 1100;
  return success();
}
static praktor_process_backend_result stop(void *user, const praktor_process_snapshot *s) {
  fake *f = user; check(s->running && s->pid != 0); ++f->stops;
  if (f->stop_error.ok && f->graceful) f->running = false;
  return f->stop_error;
}
static praktor_process_backend_result terminate(void *user, const praktor_process_snapshot *s) {
  fake *f = user; ++f->terminates; f->terminated = *s;
  if (f->terminate_error.ok && f->terminate_stops) f->running = false;
  return f->terminate_error;
}
static uint64_t now(void *user) { return ((fake *)user)->now; }
static void sleep_ms(void *user, uint32_t ms) {
  fake *f = user; uint64_t ns = ms ? (uint64_t)ms*MS : MS/10;
  if (f->sleep_cap && ns > f->sleep_cap) ns = f->sleep_cap;
  f->now += ns; ++f->sleeps;
}
static fake fixture(void) {
  fake f = {0}; f.start_error = f.stop_error = f.terminate_error = success();
  f.query_error.ok = false; f.query_error.native_error = 5; f.query_error.message = "query denied";
  f.terminate_stops = true; return f;
}
static praktor_process_control_request request(praktor_process_operation op) {
  static const char *const args[] = {"--mode","safe value"};
  const praktor_process_control_request r = {op,"worker.exe","/bin/worker",args,2,"/work",5,5,false};
  return r;
}
static praktor_process_control_host host(fake *f) {
  const praktor_process_control_host h = {f,query,start,stop,terminate,now,sleep_ms,1}; return h;
}
static praktor_process_control_result run(fake *f, praktor_process_control_request r) {
  praktor_process_control_result out; praktor_process_control_host h = host(f);
  check(praktor_process_control_execute(&r,&h,&out) == SALTS_OK); return out;
}
spec("C application control state and idempotence") {
  it("queries status and makes target states idempotent") {
    fake f = fixture(); praktor_process_control_result out;
    f.running = true; out = run(&f,request(PRAKTOR_PROCESS_STATUS));
    check(out.ok && !out.changed && out.snapshot.pid == 100 && f.queries == 1);
    out = run(&f,request(PRAKTOR_PROCESS_START)); check(out.ok && !out.changed && f.starts == 0);
    f.running = false; out = run(&f,request(PRAKTOR_PROCESS_STOP));
    check(out.ok && !out.changed && f.stops == 0 && f.terminates == 0);
  }
  it("starts with typed arguments and keeps the successful application alive") {
    fake f = fixture(); praktor_process_control_result out = run(&f,request(PRAKTOR_PROCESS_START));
    check(out.ok && out.changed && out.snapshot.running && f.starts == 1 && f.queries == 2);
    check(f.terminates == 0 && f.running);
  }
  it("confirms graceful stop before restart admission") {
    fake f = fixture(); praktor_process_control_result out;
    f.running = f.graceful = true; out = run(&f,request(PRAKTOR_PROCESS_RESTART));
    check(out.ok && out.changed && f.stops == 1 && f.starts == 1 && f.queries == 4);
    check(f.terminates == 0 && out.error == PRAKTOR_PROCESS_CONTROL_NONE);
  }
}
spec("C application control validates before dispatch") {
  it("rejects invalid parameters and arithmetic overflow without a backend call") {
    fake f = fixture(); praktor_process_control_request r = request(PRAKTOR_PROCESS_START);
    praktor_process_control_result out; unsigned i;
    for (i = 0; i < 6; ++i) {
      r = request(PRAKTOR_PROCESS_START);
      switch (i) {
        case 0: r.operation = (praktor_process_operation)100; break;
        case 1: r.image_name = ""; break;
        case 2: r.executable = NULL; break;
        case 3: r.startup_timeout_ms = 0; break;
        case 4: r.stop_timeout_ms = -1; break;
        default: r.startup_timeout_ms = INT64_MAX; break;
      }
      out = run(&f,r); check(!out.ok && out.error == PRAKTOR_PROCESS_CONTROL_INVALID_PARAMETERS);
      check(strcmp(out.phase,"validate") == 0 && !out.changed && f.queries == 0);
    }
  }
  it("rejects incomplete host APIs without dispatch") {
    fake f = fixture(); praktor_process_control_request r = request(PRAKTOR_PROCESS_STATUS);
    praktor_process_control_host h = host(&f); praktor_process_control_result out;
    h.sleep_ms = NULL; check(praktor_process_control_execute(&r,&h,&out) == SALTS_EINVAL);
    h = host(&f); h.terminate = NULL; check(praktor_process_control_execute(&r,&h,&out) == SALTS_EINVAL);
    h = host(&f); h.poll_interval_ms = 0; check(praktor_process_control_execute(&r,&h,&out) == SALTS_EINVAL);
    check(praktor_process_control_execute(NULL,&h,&out) == SALTS_EINVAL && f.queries == 0);
  }
}
spec("C application control deadlines and exact instance compensation") {
  it("times out absent startup without terminating another process") {
    fake f = fixture(); praktor_process_control_result out;
    f.never_appears = true; f.sleep_cap = MS/2;
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && out.changed && out.error == PRAKTOR_PROCESS_CONTROL_TIMEOUT);
    check(strcmp(out.phase,"start_poll") == 0 && out.duration_ms == 5 && f.terminates == 0);
    check(f.sleeps > 5 && !out.snapshot.running);
  }
  it("charges backend start time and terminates the original identity across replacement queries") {
    fake f = fixture(); praktor_process_control_result out;
    f.start_delay = 6*MS; f.replacement = true;
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && out.error == PRAKTOR_PROCESS_CONTROL_TIMEOUT && f.terminates == 1);
    check(f.terminated.pid == 100 && f.terminated.session_id == 7 && f.terminated.instance_token == 1100);
    check(strcmp(f.terminated.canonical_image_name,"worker.exe") == 0);
    check(!out.snapshot.running && out.duration_ms == 6);
  }
  it("reports cleanup rejection with native error and the last observed identity") {
    fake f = fixture(); praktor_process_control_result out;
    f.start_delay = 6*MS; f.terminate_error.ok = false;
    f.terminate_error.native_error = 13; f.terminate_error.message = "cleanup denied";
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && out.changed && out.error == PRAKTOR_PROCESS_CONTROL_BACKEND_FAILURE);
    check(out.native_error == 13 && strcmp(out.phase,"start_timeout_terminate") == 0);
    check(strcmp(out.message,"cleanup denied") == 0 && out.snapshot.running);
  }
  it("fails closed when cleanup is accepted but never confirmed") {
    fake f = fixture(); praktor_process_control_result out;
    f.start_delay = 6*MS; f.terminate_stops = false;
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && out.snapshot.running && out.error == PRAKTOR_PROCESS_CONTROL_TIMEOUT);
    check(strcmp(out.phase,"start_timeout_cleanup_poll") == 0 && out.duration_ms == 11);
  }
  it("restores the admitted identity when the final timeout query fails") {
    fake f = fixture(); praktor_process_control_result out;
    f.start_delay = 6*MS; f.replacement = true; f.fail_query = 3;
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && out.snapshot.pid == 100 && out.snapshot.instance_token == 1100);
    check(strcmp(out.snapshot.canonical_image_name,"worker.exe") == 0 && f.terminates == 0);
    check(strcmp(out.phase,"start_timeout_query") == 0 && out.native_error == 5);
  }
  it("counts query time in stop deadlines and handles monotonic counter wrap") {
    fake f = fixture(); praktor_process_control_result out;
    f.running = true; f.query_delay = 6*MS; f.now = UINT64_MAX-2*MS;
    out = run(&f,request(PRAKTOR_PROCESS_STOP));
    check(!out.ok && out.error == PRAKTOR_PROCESS_CONTROL_TIMEOUT && out.duration_ms == 12);
    check(strcmp(out.phase,"stop_poll") == 0 && f.stops == 1 && f.terminates == 0);
  }
}
spec("C application control stop escalation and failures") {
  it("does not escalate without force and re-queries before forced termination") {
    fake f = fixture(); praktor_process_control_result out; praktor_process_control_request r;
    f.running = true; out = run(&f,request(PRAKTOR_PROCESS_STOP));
    check(!out.ok && f.terminates == 0 && out.changed && strcmp(out.phase,"stop_poll") == 0);
    r = request(PRAKTOR_PROCESS_STOP); r.force_terminate = true; f.replacement = true;
    out = run(&f,r);
    check(out.ok && out.changed && f.terminates == 1 && f.terminated.pid == 200);
    check(out.error == PRAKTOR_PROCESS_CONTROL_NONE && *out.phase == '\0' && *out.message == '\0');
  }
  it("propagates backend errors and unsupported-platform classification") {
    fake f = fixture(); praktor_process_control_result out;
    f.fail_query = 1; f.query_error.message = "unsupported_platform";
    out = run(&f,request(PRAKTOR_PROCESS_STATUS));
    check(!out.ok && out.error == PRAKTOR_PROCESS_CONTROL_UNSUPPORTED_PLATFORM && out.native_error == 5);
    f = fixture(); f.start_error.ok = false; f.start_error.message = "admission denied";
    out = run(&f,request(PRAKTOR_PROCESS_START));
    check(!out.ok && !out.changed && strcmp(out.phase,"start") == 0 && f.terminates == 0);
    f = fixture(); f.running = true; f.stop_error.ok = false; f.stop_error.message = "stop denied";
    out = run(&f,request(PRAKTOR_PROCESS_RESTART));
    check(!out.ok && !out.changed && strcmp(out.phase,"request_stop") == 0 && f.starts == 0);
  }
  it("does not turn query failure into successful stop or force escalation") {
    fake f = fixture(); praktor_process_control_request r = request(PRAKTOR_PROCESS_STOP);
    praktor_process_control_result out; f.running = true; f.fail_query = 2; r.force_terminate = true;
    out = run(&f,r);
    check(!out.ok && out.changed && out.error == PRAKTOR_PROCESS_CONTROL_BACKEND_FAILURE);
    check(strcmp(out.phase,"stop_poll") == 0 && f.terminates == 0 && out.snapshot.pid == 100);
    check(strcmp(out.snapshot.canonical_image_name,"worker.exe") == 0);
  }
}
