#include "praktor/process_native.h"
#include <tinytest.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <salts/clock.h>
#include <salts/thread.h>
#endif

spec("C native application host ownership") {
  it("validates initialization and closes idempotently") {
    praktor_process_native n = {0}; praktor_process_control_host h;
    void *owner; int r;
    check(praktor_process_native_init(NULL) == SALTS_EINVAL);
    check(praktor_process_native_bind(&n,&h) == SALTS_EINVAL);
    check(praktor_process_native_init(&n) == SALTS_OK); owner = n.impl;
    r = praktor_process_native_init(&n);
    check(r == SALTS_EINVAL && n.impl == owner);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK);
    check(h.user == owner && h.query && h.start && h.request_stop && h.terminate);
    check(praktor_process_native_close(&n,0) == SALTS_OK && !n.impl);
    check(praktor_process_native_close(&n,0) == SALTS_OK);
    praktor_process_native_destroy_join(&n);
  }
}
#ifndef _WIN32
spec("C native application host explicitly unsupported on non-Windows") {
  it("does not fall back to a shell or legacy backend") {
    praktor_process_native n = {0}; praktor_process_control_host h;
    praktor_process_snapshot s = {0}; praktor_process_control_request request = {0};
    praktor_process_backend_result results[4]; size_t i;
    check(praktor_process_native_init(&n) == SALTS_OK);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK);
    results[0] = h.query(h.user,"worker.exe",&s);
    results[1] = h.start(h.user,&request,&s);
    results[2] = h.request_stop(h.user,&s);
    results[3] = h.terminate(h.user,&s);
    praktor_process_native_destroy_join(&n);
    for (i = 0; i < 4; ++i) check(!results[i].ok && strcmp(results[i].message,"unsupported_platform") == 0);
  }
}
#else
spec("C native application host identity and evidence") {
  it("matches exact Unicode basename and session, preserving snapshot tokens") {
    praktor_process_native n = {0}; bool matches;
    praktor_process_backend_result a,b,c,d,e;
    praktor_process_snapshot first = {true,42,7,123,"worker.exe"}, second = first;
    check(praktor_process_native_init(&n) == SALTS_OK);
    a = praktor_process_native_match_candidate(&n,"  C:/configured/WORKER.EXE  ","C:\\runtime\\worker.exe",7,7,&matches);
    check(a.ok && matches);
    b = praktor_process_native_match_candidate(&n,"C:/configured/\xC3\x84.EXE","\xC3\xA4.exe",7,7,&matches);
    check(b.ok && matches);
    c = praktor_process_native_match_candidate(&n,"worker.exe","worker.com",7,7,&matches);
    check(c.ok && !matches);
    d = praktor_process_native_match_candidate(&n,"worker.exe","worker.exe",8,7,&matches);
    check(d.ok && !matches);
    ++second.instance_token;
    e = praktor_process_native_match_snapshot(&n,&first,&second,7,&matches);
    praktor_process_native_destroy_join(&n);
    check(e.ok && !matches);
  }
  it("reports evidence errors for pending/invalid objects and requires signaled exit") {
    praktor_process_native n = {0}; praktor_process_snapshot s = {0};
    praktor_process_backend_result pending,exited,invalid;
    HANDLE event;
    bool pending_message;
    check(praktor_process_native_init(&n) == SALTS_OK);
    event = CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!event) { praktor_process_native_destroy_join(&n); check(event != NULL); return; }
    pending = praktor_process_native_observe_failure(&n,event,42,ERROR_ACCESS_DENIED,"identity unavailable",&s);
    pending_message = strstr(pending.message,"identity unavailable for pid 42; wait_result=258") != NULL;
    (void)SetEvent(event);
    exited = praktor_process_native_observe_failure(&n,event,42,ERROR_ACCESS_DENIED,"identity unavailable",&s);
    invalid = praktor_process_native_observe_failure(&n,NULL,42,ERROR_ACCESS_DENIED,"identity unavailable",&s);
    (void)CloseHandle(event); praktor_process_native_destroy_join(&n);
    check(!pending.ok && pending.native_error == ERROR_ACCESS_DENIED && pending_message);
    check(exited.ok && !s.running);
    check(!invalid.ok && invalid.native_error == ERROR_INVALID_HANDLE);
  }
}
#ifdef PRAKTOR_NATIVE_FIXTURE
static const char *image_name(void) {
  const char *p = PRAKTOR_NATIVE_FIXTURE, *name = p;
  for (; *p; ++p) if (*p == '/' || *p == '\\') name = p+1;
  return name;
}
static praktor_process_control_request fixture_request(void) {
  static const char *const args[] = {"--assert-argv","space value","say \"hello\" and slash\\end\\","","plain"};
  const praktor_process_control_request r = {PRAKTOR_PROCESS_START,NULL,PRAKTOR_NATIVE_FIXTURE,args,5,NULL,3000,3000,false};
  praktor_process_control_request request = r; request.image_name = image_name(); return request;
}
static bool cleanup(praktor_process_control_host *h) {
  uint64_t began = cmeta_hrtime(); praktor_process_snapshot s = {0}; praktor_process_backend_result r;
  do {
    r = h->query(h->user,image_name(),&s);
    if (r.ok && !s.running) return true;
    if (r.ok) (void)h->terminate(h->user,&s);
    cmeta_sleep_ms(1);
  } while (cmeta_hrtime()-began < UINT64_C(3000000000));
  return false;
}
typedef struct window_probe { DWORD pid; bool ready; } window_probe;
static BOOL CALLBACK window_ready(HWND window, LPARAM parameter) {
  window_probe *p = (window_probe *)parameter; DWORD pid;
  (void)GetWindowThreadProcessId(window,&pid);
  if (pid == p->pid && !GetWindow(window,GW_OWNER)) { p->ready = true; return FALSE; }
  return TRUE;
}
static bool ready(uint32_t pid) {
  uint64_t began = cmeta_hrtime(); window_probe p = {pid,false};
  do {
    (void)EnumWindows(window_ready,(LPARAM)&p);
    if (p.ready) return true;
    cmeta_sleep_ms(1);
  } while (cmeta_hrtime()-began < UINT64_C(3000000000));
  return false;
}
spec("C-only native application execution") {
  it("starts with escaped argv and rejects stale identity before graceful controller stop") {
    praktor_process_native n = {0}; praktor_process_control_host h;
    praktor_process_control_request request = fixture_request();
    praktor_process_control_result started = {0},stopped = {0};
    praktor_process_snapshot current = {0},stale;
    praktor_process_backend_result q,denied;
    int a,b; bool initial_clean,window,final_clean,token;
    check(praktor_process_native_init(&n) == SALTS_OK);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK); h.poll_interval_ms = 1;
    initial_clean = cleanup(&h);
    a = praktor_process_control_execute(&request,&h,&started);
    window = started.ok && ready(started.snapshot.pid);
    q = h.query(h.user,image_name(),&current);
    token = q.ok && current.running && current.pid == started.snapshot.pid &&
        current.instance_token == started.snapshot.instance_token;
    stale = current; ++stale.instance_token;
    denied = h.terminate(h.user,&stale);
    request.operation = PRAKTOR_PROCESS_STOP;
    b = praktor_process_control_execute(&request,&h,&stopped);
    final_clean = cleanup(&h); praktor_process_native_destroy_join(&n);
    check(initial_clean && a == SALTS_OK && started.ok && started.changed && window && token);
    check(!denied.ok && denied.native_error == ERROR_INVALID_DATA);
    check(b == SALTS_OK && stopped.ok && stopped.changed && !stopped.snapshot.running && final_clean);
  }
  it("leaves successful persistent applications alive after host destruction") {
    praktor_process_native n = {0}; praktor_process_control_host h;
    praktor_process_control_request request = fixture_request(); praktor_process_snapshot started = {0},current = {0};
    praktor_process_backend_result r,q; bool initial_clean,window,final_clean,alive; int closed;
    check(praktor_process_native_init(&n) == SALTS_OK);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK); initial_clean = cleanup(&h);
    r = h.start(h.user,&request,&started); window = r.ok && ready(started.pid);
    closed = praktor_process_native_close(&n,0);
    check(praktor_process_native_init(&n) == SALTS_OK);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK);
    q = h.query(h.user,image_name(),&current);
    alive = q.ok && current.running && current.pid == started.pid && current.instance_token == started.instance_token;
    final_clean = cleanup(&h); praktor_process_native_destroy_join(&n);
    check(initial_clean && r.ok && window && closed == SALTS_OK && alive && final_clean);
  }
  it("bounds metadata before admission and recovers rejected startup without handle growth") {
    praktor_process_native n = {0}; praktor_process_control_host h;
    praktor_process_control_request request = fixture_request(); praktor_process_snapshot s = {0};
    praktor_process_backend_result bad_utf8,oversized,bad_identity;
    char *large; const char *args[1]; unsigned i; bool recovered = true,final_clean,initial_clean,absent;
    DWORD before = 0,after = 0;
    check(praktor_process_native_init(&n) == SALTS_OK);
    check(praktor_process_native_bind(&n,&h) == SALTS_OK); initial_clean = cleanup(&h);
    args[0] = "\xC0\xAF"; request.arguments = args; request.argument_count = 1;
    bad_utf8 = h.start(h.user,&request,&s);
    large = malloc(32769); if (!large) { praktor_process_native_destroy_join(&n); check(large != NULL); return; }
    memset(large,'x',32768); large[32768] = '\0'; args[0] = large;
    oversized = h.start(h.user,&request,&s); free(large);
    {
      praktor_process_backend_result q = h.query(h.user,image_name(),&s);
      absent = q.ok && !s.running;
    }
    request = fixture_request(); request.image_name = "praktor-wrong-identity.exe";
    (void)GetProcessHandleCount(GetCurrentProcess(),&before);
    for (i = 0; i < 16; ++i) {
      bad_identity = h.start(h.user,&request,&s);
      if (bad_identity.ok) recovered = false;
      if (praktor_process_native_close(&n,1000) != SALTS_OK) recovered = false;
      praktor_process_native_destroy_join(&n);
      if (praktor_process_native_init(&n) != SALTS_OK || praktor_process_native_bind(&n,&h) != SALTS_OK) { recovered = false; break; }
    }
    (void)GetProcessHandleCount(GetCurrentProcess(),&after);
    final_clean = n.impl && cleanup(&h); praktor_process_native_destroy_join(&n);
    check(initial_clean && !bad_utf8.ok && bad_utf8.native_error == ERROR_NO_UNICODE_TRANSLATION);
    check(!oversized.ok && absent && recovered && final_clean && after <= before+4u);
  }
}
#endif
#endif
