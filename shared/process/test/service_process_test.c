#include "praktor/service_process_host.h"
#include <tinytest.h>
#include <stdio.h>
#include <string.h>

static praktor_service_request request(praktor_service_profile *profile) {
  static const char *const query[] = {"query","{service_name}",PRAKTOR_SERVICE_STATE_PATH};
  static const char *const start[] = {"start","{service_name}",PRAKTOR_SERVICE_STATE_PATH,"{arguments}"};
  static const char *const stop[] = {"stop","{service_name}",PRAKTOR_SERVICE_STATE_PATH};
  static const char *const args[] = {"space value","quote\"\\tail\\"};
  const praktor_service_profile p = {PRAKTOR_SERVICE_FIXTURE,{query,3},{start,4},{stop,3}};
  const praktor_service_request r = {PRAKTOR_PROCESS_START,"Retro Camera Service","fixture",profile,{args,2},5000,1};
  *profile = p; return r;
}
spec("C service controller with real Salts::Process commands") {
  it("executes status, escaped argv, start, restart and stop without a C++ shell") {
    const praktor_service_config config = {64,65536,1024};
    praktor_service_control control = {0}; praktor_service_process_host process = {0}; praktor_service_host host;
    praktor_service_profile profile; praktor_service_request r = request(&profile);
    praktor_service_result started = {0},restarted = {0},stopped = {0},status = {0};
    int a,b,c,d,closed; FILE *file = fopen(PRAKTOR_SERVICE_STATE_PATH,"w");
    check(file != NULL); fputs("1\n",file); check(fclose(file) == 0);
    check(praktor_service_control_init(&control,&config) == SALTS_OK);
    check(praktor_service_process_host_init(&process,&config) == SALTS_OK);
    check(praktor_service_process_host_bind(&process,&host) == SALTS_OK);
    a = praktor_service_control_execute(&control,&r,&host,&started);
    r.operation = PRAKTOR_PROCESS_RESTART; b = praktor_service_control_execute(&control,&r,&host,&restarted);
    r.operation = PRAKTOR_PROCESS_STOP; c = praktor_service_control_execute(&control,&r,&host,&stopped);
    r.operation = PRAKTOR_PROCESS_STATUS; d = praktor_service_control_execute(&control,&r,&host,&status);
    closed = praktor_service_process_host_close(&process); praktor_service_control_destroy(&control);
    (void)remove(PRAKTOR_SERVICE_STATE_PATH);
    check(a == SALTS_OK && started.ok && started.changed && started.state == PRAKTOR_SERVICE_RUNNING);
    check(b == SALTS_OK && restarted.ok && restarted.changed && restarted.state == PRAKTOR_SERVICE_RUNNING);
    check(c == SALTS_OK && stopped.ok && stopped.changed && stopped.state == PRAKTOR_SERVICE_STOPPED);
    check(d == SALTS_OK && status.ok && !status.changed && status.state == PRAKTOR_SERVICE_STOPPED);
    check(closed == SALTS_OK && !process.impl && praktor_service_process_host_close(&process) == SALTS_OK);
  }
  it("fails closed on real command timeout and capture overflow, joining every child") {
    const praktor_service_config config = {64,65536,128};
    praktor_service_control control = {0}; praktor_service_process_host process = {0}; praktor_service_host host;
    praktor_service_profile profile; praktor_service_request r = request(&profile);
    praktor_service_result timed = {0},limited = {0}; int a,b,closed;
    check(praktor_service_control_init(&control,&config) == SALTS_OK);
    check(praktor_service_process_host_init(&process,&config) == SALTS_OK);
    check(praktor_service_process_host_bind(&process,&host) == SALTS_OK);
    r.name = "slow"; r.timeout_ms = 50;
    a = praktor_service_control_execute(&control,&r,&host,&timed);
    r.name = "flood"; r.timeout_ms = 5000;
    b = praktor_service_control_execute(&control,&r,&host,&limited);
    closed = praktor_service_process_host_close(&process); praktor_service_control_destroy(&control);
    check(a == SALTS_OK && !timed.ok && !timed.changed && timed.error == PRAKTOR_SERVICE_TIMEOUT);
    check(b == SALTS_OK && !limited.ok && !limited.changed && limited.error == PRAKTOR_SERVICE_STATE_FAILED);
    check(limited.include_process && limited.process_state == PRAKTOR_PROCESS_OUTPUT_LIMIT_EXCEEDED);
    check(closed == SALTS_OK && !process.impl);
  }
}
