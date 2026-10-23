#include "praktor/process_driver.h"
#if defined(PRAKTOR_PROCESS_RETAINER)
#include <salts/process_retainer.h>
#endif
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>
#include <stdlib.h>
#include <string.h>

static cmeta_process_options_t command(const char *text) {
  cmeta_process_options_t o;
  cmeta_process_options_init(&o);
  o.program = text; o.flags = SALTS_PROCESS_SHELL_COMMAND; o.timeout_ms = 5000u;
  return o;
}
static void finish(praktor_process_driver *driver) {
  bool done = false;
  uint64_t started = cmeta_hrtime();
  while (!done && cmeta_hrtime() - started < UINT64_C(6000000000)) {
    check(praktor_process_driver_tick(driver, &done) == SALTS_OK);
    if (!done) cmeta_sleep_ms(1u);
  }
  check(done);
}
#if defined(PRAKTOR_PROCESS_RETAINER)
static int count_line(void *user, const char *text, size_t size) {
  (void)text; (void)size; ++*(int *)user; return SALTS_OK;
}
#endif
static int reject_line(void *user, const char *text, size_t size) {
  (void)user; (void)text; (void)size; return SALTS_EIO;
}

spec("C11 Process driver without a C++ runtime") {
  it("executes and captures a real child with fixed output bounds") {
    praktor_process_driver driver = {0}; praktor_process_view view;
    cmeta_process_options_t o = command("echo c11-process-driver");
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 128u, NULL, NULL, NULL) == SALTS_OK);
    finish(&driver);
    check(praktor_process_driver_close(&driver, 3000u) == SALTS_OK);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK);
    check(!view.active && !view.failed && view.result.exit_code == 0);
    check(strstr(view.stdout_data, "c11-process-driver") != NULL);
    check(praktor_process_driver_release(&driver) == SALTS_OK && driver.impl == NULL);
  }
  it("drains stdout and stderr while writing more than one stdin chunk") {
    char input[16384];
    praktor_process_driver driver = {0}; praktor_process_view view;
#if defined(_WIN32)
    cmeta_process_options_t o = command("powershell.exe -NoProfile -Command \"$x=[Console]::In.ReadToEnd(); [Console]::Out.Write($x); [Console]::Error.Write('stderr')\"");
#else
    cmeta_process_options_t o = command("cat; printf stderr >&2");
#endif
    memset(input, 'x', sizeof(input));
    check(praktor_process_driver_start(&driver, &o, input, sizeof(input), 32768u, NULL, NULL, NULL) == SALTS_OK);
    finish(&driver);
    check(praktor_process_driver_close(&driver, 3000u) == SALTS_OK);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK);
    check(view.result.exit_code == 0 && view.stdout_size == sizeof(input));
    check(memcmp(view.stdout_data, input, sizeof(input)) == 0);
    check(view.stderr_size == 6u && memcmp(view.stderr_data, "stderr", 6u) == 0);
    check(praktor_process_driver_release(&driver) == SALTS_OK);
  }
  it("rejects capture overflow without exceeding the combined bound") {
    praktor_process_driver driver = {0}; praktor_process_view view;
    cmeta_process_options_t o = command("echo output-too-large-for-budget");
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 8u, NULL, NULL, NULL) == SALTS_OK);
    finish(&driver);
    (void)praktor_process_driver_close(&driver, 3000u);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK);
    check(view.output_limited && view.stdout_size + view.stderr_size <= 8u);
    check(praktor_process_driver_release(&driver) == SALTS_OK);
  }
  it("handles failed admission and a failing stream callback through the same cleanup path") {
    praktor_process_driver driver = {0}; praktor_process_view view;
    cmeta_process_options_t o = command("");
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 128u, NULL, NULL, NULL) != SALTS_OK);
    if (driver.impl != NULL) {
      (void)praktor_process_driver_close(&driver, 3000u);
      check(praktor_process_driver_release(&driver) == SALTS_OK);
    }
    o = command("echo callback-error");
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 128u, NULL, reject_line, NULL) == SALTS_OK);
    finish(&driver);
    check(praktor_process_driver_close(&driver, 3000u) == SALTS_EIO);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK && view.failed);
    check(praktor_process_driver_release(&driver) == SALTS_OK);
  }
#if defined(PRAKTOR_PROCESS_RETAINER)
  it("reserves before spawn and retains only C buffers after callback-user destruction") {
    cflow_process_retainer retainer = {0};
    praktor_process_driver driver = {0}, rejected = {0}; praktor_process_view view;
    size_t pending = 1u, reaped = 0u;
    int *cookie = (int *)calloc(1u, sizeof(int));
    bool done = false;
    uint64_t started;
#if defined(_WIN32)
    cmeta_process_options_t o = command("ping -n 6 127.0.0.1 > NUL");
#else
    cmeta_process_options_t o = command("sleep 5");
#endif
    check(cookie != NULL);
    check(cflow_process_retainer_init(&retainer, 1u) == SALTS_OK);
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 128u, &retainer, count_line, cookie) == SALTS_OK);
    check(praktor_process_driver_tick(&driver, &done) == SALTS_OK && !done);
    check(praktor_process_driver_start(&rejected, &o, NULL, 0u, 128u, &retainer, NULL, NULL) == SALTS_ENOBUFS);
    check(rejected.impl == NULL);
    check(praktor_process_driver_release(&driver) == SALTS_EBUSY); /* live admission cannot be handed off */
    check(praktor_process_driver_close(&driver, 0u) == SALTS_EBUSY);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK && view.active && view.closing);
    check(praktor_process_driver_tick(&driver, &done) == SALTS_EBUSY);
    free(cookie); /* ASan catches any late call into this destroyed Host. */
    check(praktor_process_driver_release(&driver) == SALTS_OK && driver.impl == NULL);
    check(cflow_process_retainer_destroy(&retainer) == SALTS_EBUSY);
    started = cmeta_hrtime();
    while (pending && cmeta_hrtime() - started < UINT64_C(6000000000)) {
      int r = cflow_process_retainer_run_ready(&retainer, 64u, &reaped, &pending);
      check(r == SALTS_OK || r == SALTS_EBUSY);
      if (pending) cmeta_sleep_ms(1u);
    }
    check(pending == 0u);
    check(cflow_process_retainer_destroy(&retainer) == SALTS_OK);
  }
#endif
}
