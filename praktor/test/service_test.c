#include "actions/tool.h"
#include "component_host.h"
#include "praktor.h"
#include <core/clock.h>
#include <core/fmt.h>
#include <core/tstr.h>
#include <file/fs.h>
#include <stdlib.h>
#include <string.h>
#include <threads/threads.h>
#include <tinytest.h>

static praktor_component_host host;
static praktor_execution_service service;
static praktor_service_job jobs[2];
static json_value_t *metadata, *document;
static praktor_runtime_result result;
static tstr source;
static int cancel_after_ticks(void *user) { return ++*(int *)user >= 8; }
static const char *files[] = {PRAKTOR_SERVICE_OUTPUT_A, PRAKTOR_SERVICE_OUTPUT_B};
static void put(json_value_t *object, const char *key, json_value_t *value) {
  check(value != NULL);
  check(json_object_add_checked(object, key, value));
}
static void setup_host(const char *file, bool second) {
  metadata = json_create_object();
  check(metadata != NULL);
  json_value_t *services = json_create_object();
  check(services != NULL);
  put(metadata, "services", services);
  put(services, "shared", json_create_string(file));
  if (second)
    put(services, "other", json_create_string(file));
  praktor_runtime_limits limits = praktor_default_limits();
  char error[256] = {0};
  int rc = praktor_component_host_start(&host, &limits, metadata, error, sizeof error);
  info("host start: %d %s", rc, error);
  check(rc == SALTS_OK);
  const json_value_t *definition = NULL;
  check(praktor_component_host_service(&host, "shared", &service, &definition) == SALTS_OK);
  check(definition != NULL);
}
static praktor_service_result wait_job(praktor_execution_service *provider,
                                       praktor_service_job *job) {
  praktor_service_result view = {0};
  uint64_t begin = hrtime();
  while (!view.done) {
    check(praktor_execution_service_poll(provider, job, &view) == SALTS_OK);
    check(hrtime() - begin < UINT64_C(5000000000));
    if (!view.done)
      sleep_ms(1u);
  }
  return view;
}
static praktor_runtime_status execute(void) {
  praktor_execute_request request = {
      .yaml = source, .yaml_size = tstr_len(source), .limits = praktor_default_limits()};
  praktor_runtime_status status = praktor_execute(&request, &result);
  info("execute: %d %s", status, result.error);
  if (result.json)
    document = json_parse(result.json, result.json_size);
  return status;
}
static const json_value_t *outputs(const char *task) {
  return json_object_get(json_object_get(json_object_get(document, "tasks"), task), "outputs");
}
static tstr workflow(const char *file) {
  return tstr_cat_typed(NULL, "services:\n  shared: '{}'\ntasks:\n", file);
}
spec("Reusable YAML execution services") {
  before_each() {
    host = (praktor_component_host){0};
    service = (praktor_execution_service){0};
    memset(jobs, 0, sizeof jobs);
    metadata = NULL;
    document = NULL;
    source = NULL;
    result = (praktor_runtime_result){0};
    for (size_t i = 0u; i < 2u; ++i)
      (void)fs_unlink(files[i]);
  }
  after_each() {
    for (size_t i = 0u; i < 2u; ++i)
      if (jobs[i].impl) {
        check(praktor_execution_service_cancel(&service, &jobs[i]) == SALTS_OK);
        (void)wait_job(&service, &jobs[i]);
        check(praktor_execution_service_release(&service, &jobs[i]) == SALTS_OK);
      }
    check(praktor_component_host_stop(&host) == SALTS_OK);
    json_free(metadata);
    json_free(document);
    praktor_release_result(&result);
    tstr_free(source);
    for (size_t i = 0u; i < 2u; ++i)
      (void)fs_unlink(files[i]);
  }
  it("binds one instance per name and isolates simultaneous job arguments") {
    setup_host(PRAKTOR_SERVICE_ECHO, true);
    praktor_execution_service again = {0}, other = {0};
    const json_value_t *definition = NULL;
    check(praktor_component_host_service(&host, "shared", &again, &definition) == SALTS_OK);
    check(again.self == service.self);
    check(praktor_component_host_service(&host, "other", &other, &definition) == SALTS_OK);
    check(other.self != service.self);
    const char *a[] = {"emit", "alpha & literal spaces", NULL}, *b[] = {"emit", "beta", NULL};
    praktor_process_request ra = {a, 2u}, rb = {b, 2u};
    check(praktor_execution_service_start(&service, &ra, &jobs[0]) == SALTS_OK);
    check(praktor_execution_service_start(&service, &rb, &jobs[1]) == SALTS_OK);
    check(host.processes[0].jobs == 2u);
    check(praktor_component_host_stop(&host) == SALTS_EBUSY);
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_EBUSY);
    praktor_service_result first = wait_job(&service, &jobs[0]),
                           second = wait_job(&service, &jobs[1]);
    check(first.success && second.success);
    check(strcmp(first.output, a[1]) == 0);
    check(strcmp(second.output, b[1]) == 0);
  }
  it("runs two generic tasks through one service with native variable resolution") {
    source = tstr_cat_typed(
        NULL, "variables:\n  message: root-value\nservices:\n  shared: '{}'\ntasks:\n",
        PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(
        source,
        "  - name: first\n    tool: shared\n    with:\n      text: '{message}'\n  - name: second\n "
        "   depends_on: [first]\n    vars:\n      message: task-value\n    tool: shared\n    "
        "with:\n      text: '{message}'\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
    check(strcmp(json_get_string(outputs("first"), "stdout"), "root-value") == 0);
    check(strcmp(json_get_string(outputs("second"), "stdout"), "task-value") == 0);
  }
  it("inherits environment loaded through the native dotenv entrypoint") {
    source = tstr_cat_typed(
        NULL,
        "dotEnv:\n  - '{}'\nservices:\n  shared: '{}'\ntasks:\n  - name: env\n    tool: shared\n",
        PRAKTOR_SERVICE_DOTENV, PRAKTOR_SERVICE_ENV);
    check(praktor_load_environment(source, tstr_len(source)) == PRAKTOR_RUNTIME_OK);
    check(execute() == PRAKTOR_RUNTIME_OK);
    check(strcmp(json_get_string(outputs("env"), "stdout"), "dotenv-loaded") == 0);
  }
  it("binds argv variables directly from dotenv when no task/root value overrides them") {
    source =
        tstr_cat_typed(NULL,
                       "dotEnv:\n  - '{}'\nservices:\n  shared: '{}'\ntasks:\n  - name: env\n    "
                       "tool: shared\n    with:\n      text: '{PRAKTOR_CLI_ENV_TEST_VALUE}'\n",
                       PRAKTOR_SERVICE_DOTENV, PRAKTOR_SERVICE_ECHO);
    check(praktor_load_environment(source, tstr_len(source)) == PRAKTOR_RUNTIME_OK);
    check(execute() == PRAKTOR_RUNTIME_OK);
    check(strcmp(json_get_string(outputs("env"), "stdout"), "dotenv-loaded") == 0);
  }
  it("downloads two real files through a curl definition without native download knowledge") {
    source = workflow(PRAKTOR_SERVICE_CURL);
    source = tstr_cat_typed(
        source,
        "  - name: first\n    tool: shared\n    with:\n      url: '{}'\n      path: '{}'\n  - "
        "name: second\n    tool: shared\n    with:\n      url: '{}'\n      path: '{}'\n",
        PRAKTOR_SERVICE_PAYLOAD_URL, files[0], PRAKTOR_SERVICE_PAYLOAD_URL, files[1]);
    check(execute() == PRAKTOR_RUNTIME_OK);
    fs_buf_t expected = {0};
    check(fs_read_file(PRAKTOR_SERVICE_PAYLOAD_FILE, &expected) == 0);
    for (size_t i = 0u; i < 2u; ++i) {
      fs_buf_t bytes = {0};
      check(fs_read_file(files[i], &bytes) == 0);
      bool valid = bytes.len == expected.len && !memcmp(bytes.base, expected.base, bytes.len);
      fs_buf_free(&bytes);
      check(valid);
    }
    fs_buf_free(&expected);
  }
  it("validates every task binding before the first external side effect") {
    source = workflow(PRAKTOR_SERVICE_CURL);
    source =
        tstr_cat_typed(source,
                       "  - name: first\n    tool: shared\n    with:\n      url: '{}'\n      path: "
                       "'{}'\n  - name: invalid\n    tool: shared\n    with:\n      url: '{}'\n",
                       PRAKTOR_SERVICE_PAYLOAD_URL, files[0], PRAKTOR_SERVICE_PAYLOAD_URL);
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check(strstr(result.error, "missing tool input") != NULL);
    check(fs_access(files[0], SALTS_FS_ACCESS_EXISTS) != 0);
  }
  it("rejects an unknown service and unused invocation input") {
    source = workflow(PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(source, "  - name: bad\n    tool: absent\n");
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check(strstr(result.error, "unknown tool service") != NULL);
    praktor_release_result(&result);
    json_free(document);
    document = NULL;
    tstr_free(source);
    source = workflow(PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(
        source,
        "  - name: bad\n    tool: shared\n    with:\n      text: ok\n      extra: unused\n");
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check(strstr(result.error, "unused tool input") != NULL);
  }
  it("reports process failure through task status and exit code") {
    source = workflow(PRAKTOR_SERVICE_FAIL);
    source = tstr_cat(source, "  - name: failed\n    tool: shared\n");
    check(execute() == PRAKTOR_RUNTIME_FAILED);
    check(strcmp(json_get_string(outputs("failed"), "exit_code"), "7") == 0);
  }
  it("fails before task IO when a later referenced service definition is unsupported") {
    source = tstr_cat_typed(NULL,
                            "services:\n  shared: '{}'\n  later: '{}'\ntasks:\n  - name: first\n   "
                            " tool: shared\n    with:\n      url: '{}'\n      path: '{}'\n",
                            PRAKTOR_SERVICE_CURL, PRAKTOR_SERVICE_UNSUPPORTED,
                            PRAKTOR_SERVICE_PAYLOAD_URL, files[0]);
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check(strstr(result.error, "unsupported provider.kind") != NULL);
    check(fs_access(files[0], SALTS_FS_ACCESS_EXISTS) != 0);
  }
  it("rejects missing files and duplicate service identities") {
    source = workflow(files[0]);
    source = tstr_cat(source, "  - name: unused\n    tool: shared\n");
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check(strstr(result.error, "cannot open service definition") != NULL);
    praktor_release_result(&result);
    json_free(document);
    document = NULL;
    tstr_free(source);
    source = tstr_cat_typed(NULL,
                            "services:\n  repeated: '{}'\n  repeated: '{}'\ntasks:\n  - name: "
                            "unused\n    tool: repeated\n",
                            PRAKTOR_SERVICE_ECHO, PRAKTOR_SERVICE_ECHO);
    check(execute() == PRAKTOR_RUNTIME_INVALID);
  }
  it("propagates workflow cancellation through the shared-library API") {
    source = workflow(PRAKTOR_SERVICE_LONG_SLEEP);
    source = tstr_cat(source, "  - name: waiting\n    tool: shared\n");
    int calls = 0;
    praktor_execute_request request = {.yaml = source,
                                       .yaml_size = tstr_len(source),
                                       .limits = praktor_default_limits(),
                                       .is_cancelled = cancel_after_ticks,
                                       .cancel_context = &calls};
    check(praktor_execute(&request, &result) == PRAKTOR_RUNTIME_CANCELLED);
  }
  it("cancels and drains a job before releasing its shared provider") {
    setup_host(PRAKTOR_SERVICE_LONG_SLEEP, false);
    const char *args[] = {"sleep", NULL};
    praktor_process_request request = {args, 1u};
    check(praktor_execution_service_start(&service, &request, &jobs[0]) == SALTS_OK);
    praktor_service_result initial = {0};
    check(praktor_execution_service_poll(&service, &jobs[0], &initial) == SALTS_OK);
    check(!initial.done);
    check(praktor_execution_service_cancel(&service, &jobs[0]) == SALTS_OK);
    check(praktor_execution_service_cancel(&service, &jobs[0]) == SALTS_OK);
    praktor_service_result view = wait_job(&service, &jobs[0]);
    check(view.done && view.cancelled && !view.success);
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
    check(host.processes[0].jobs == 0u);
  }
  it("honors the provider timeout after actual process admission") {
    setup_host(PRAKTOR_SERVICE_SLEEP, false);
    const char *args[] = {"sleep", NULL};
    praktor_process_request request = {args, 1u};
    check(praktor_execution_service_start(&service, &request, &jobs[0]) == SALTS_OK);
    uint64_t started = hrtime();
    praktor_service_result view = wait_job(&service, &jobs[0]);
    check(!view.success && !view.cancelled);
    check(hrtime() - started >= UINT64_C(20000000));
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
  }
  it("rejects binary and oversized output without truncating it to success") {
    setup_host(PRAKTOR_SERVICE_ECHO, false);
    const char *modes[] = {"binary", "bulk"};
    for (size_t i = 0u; i < 2u; ++i) {
      const char *next[] = {modes[i], NULL};
      praktor_process_request request = {next, 1u};
      check(praktor_execution_service_start(&service, &request, &jobs[0]) == SALTS_OK);
      praktor_service_result view = wait_job(&service, &jobs[0]);
      check(!view.success);
      check(strstr(view.error, i == 0u ? "NUL-free" : "capture limit") != NULL);
      check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
    }
  }
}
