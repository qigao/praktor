#include "praktor/process_driver.h"
#include <praktor/process_retainer.h>
#include <core/clock.h>
#include <threads/threads.h>
#include <tinytest.h>
#include <stdlib.h>
#include <string.h>

static process_options_t command(const char *text) {
  process_options_t o;
  process_options_init(&o);
  o.program = text; o.flags = SALTS_PROCESS_SHELL_COMMAND; o.timeout_ms = 5000u;
  return o;
}
static void finish(praktor_process_driver *driver) {
  bool done = false;
  uint64_t started = hrtime();
  while (!done && hrtime() - started < UINT64_C(6000000000)) {
    check(praktor_process_driver_tick(driver, &done) == SALTS_OK);
    if (!done) sleep_ms(1u);
  }
  check(done);
}
static int count_line(void *user, const char *text, size_t size) {
  (void)text; (void)size; ++*(int *)user; return SALTS_OK;
}
static int reject_line(void *user, const char *text, size_t size) {
  (void)user; (void)text; (void)size; return SALTS_EIO;
}

spec("C11 Process driver without a C++ runtime") {
  it("executes and captures a real child with fixed output bounds") {
    praktor_process_driver driver = {0}; praktor_process_view view;
    process_options_t o = command("echo c11-process-driver");
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
    process_options_t o = command("powershell.exe -NoProfile -Command \"$x=[Console]::In.ReadToEnd(); [Console]::Out.Write($x); [Console]::Error.Write('stderr')\"");
#else
    process_options_t o = command("cat; printf stderr >&2");
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
    process_options_t o = command("echo output-too-large-for-budget");
    check(praktor_process_driver_start(&driver, &o, NULL, 0u, 8u, NULL, NULL, NULL) == SALTS_OK);
    finish(&driver);
    (void)praktor_process_driver_close(&driver, 3000u);
    check(praktor_process_driver_view(&driver, &view) == SALTS_OK);
    check(view.output_limited && view.stdout_size + view.stderr_size <= 8u);
    check(praktor_process_driver_release(&driver) == SALTS_OK);
  }
  it("handles failed admission and a failing stream callback through the same cleanup path") {
    praktor_process_driver driver = {0}; praktor_process_view view;
    process_options_t o = command("");
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
  it("keeps reservations pending and rejects stale and copied recovery owners") {
    praktor_process_retainer retainer = {0}, copied;
    praktor_process_retainer_token first = {0}, second = {0}, third = {0};
    size_t pending = 0u, reaped = 1u;
    check(praktor_process_retainer_init(&retainer, 1u) == SALTS_OK);
    check(praktor_process_retainer_reserve(&retainer, &first) == SALTS_OK);
    check(praktor_process_retainer_release(&retainer, first) == SALTS_OK);
    check(praktor_process_retainer_reserve(&retainer, &second) == SALTS_OK);
    check(praktor_process_retainer_release(&retainer, first) == SALTS_EINVAL);
    copied = retainer;
    check(praktor_process_retainer_release(&copied, second) == SALTS_EINVAL);
    check(praktor_process_retainer_run_ready(&retainer, 8u, &reaped, &pending) == SALTS_EBUSY);
    check(pending == 1u && reaped == 0u);
    check(praktor_process_retainer_destroy(&retainer) == SALTS_EBUSY);
    check(praktor_process_retainer_release(&retainer, second) == SALTS_OK);
    check(praktor_process_retainer_destroy(&retainer) == SALTS_OK);
    check(praktor_process_retainer_init(&retainer, 1u) == SALTS_OK);
    check(praktor_process_retainer_reserve(&retainer, &third) == SALTS_OK);
    check(praktor_process_retainer_release(&retainer, second) == SALTS_EINVAL);
    check(praktor_process_retainer_release(&retainer, third) == SALTS_OK);
    check(praktor_process_retainer_destroy(&retainer) == SALTS_OK);
  }
  it("rejects exhausted reservation identities and reclaims failed spawn capacity") {
    praktor_process_retainer retainer = {0};
    praktor_process_retainer_token token = {0};
    praktor_process_driver driver = {0};
    process_options_t options = command("");
    check(praktor_process_retainer_init(&retainer, 1u) == SALTS_OK);
    check(praktor_process_driver_start(&driver, &options, NULL, 0u, 128u,
          &retainer, NULL, NULL) != SALTS_OK);
    check(praktor_process_driver_close(&driver, 10u) == SALTS_EIO);
    check(praktor_process_driver_release(&driver) == SALTS_OK);
    check(praktor_process_retainer_reserve(&retainer, &token) == SALTS_OK);
    check(praktor_process_retainer_release(&retainer, token) == SALTS_OK);
    retainer.serial = UINT64_MAX;
    check(praktor_process_retainer_reserve(&retainer, &token) == SALTS_ERANGE);
    check(token.owner == NULL && token.serial == 0u);
    check(praktor_process_retainer_destroy(&retainer) == SALTS_OK);
  }
  it("reserves before spawn and retains only C buffers after callback-user destruction") {
    praktor_process_retainer retainer = {0};
    praktor_process_driver driver = {0}, rejected = {0}; praktor_process_view view;
    size_t pending = 1u, reaped = 0u;
    int *cookie = (int *)calloc(1u, sizeof(int));
    bool done = false;
    uint64_t started;
#if defined(_WIN32)
    process_options_t o = command("ping -n 6 127.0.0.1 > NUL");
#else
    process_options_t o = command("sleep 5");
#endif
    check(cookie != NULL);
    check(praktor_process_retainer_init(&retainer, 1u) == SALTS_OK);
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
    check(praktor_process_retainer_destroy(&retainer) == SALTS_EBUSY);
    started = hrtime();
    while (pending && hrtime() - started < UINT64_C(6000000000)) {
      int r = praktor_process_retainer_run_ready(&retainer, 64u, &reaped, &pending);
      check(r == SALTS_OK || r == SALTS_EBUSY);
      if (pending) sleep_ms(1u);
    }
    check(pending == 0u);
    check(praktor_process_retainer_destroy(&retainer) == SALTS_OK);
  }
}
