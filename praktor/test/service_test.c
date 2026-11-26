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
static json_value_t *job_inputs[2];
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
  int rc = praktor_component_host_start(&host, &limits, metadata, NULL, error, sizeof error);
  info("host start: %d %s", rc, error);
  check(rc == SALTS_OK);
  check(praktor_component_host_service(&host, "shared", &service, NULL) == SALTS_OK);
}
static int resolve_literal(void *user, const char *input, size_t max_reference_bytes, tstr *out) {
  (void)user;
  (void)max_reference_bytes;
  *out = tstr_dup(input);
  return *out ? SALTS_OK : SALTS_ENOMEM;
}
static void start_job(size_t index, const char *key, const char *text) {
  if (key) {
    json_free(job_inputs[index]);
    job_inputs[index] = json_create_object();
    check(job_inputs[index] != NULL);
    put(job_inputs[index], key, json_create_string(text));
  }
  const praktor_execution_admission admission = {.inputs = job_inputs[index]};
  const praktor_execution_request request = {.inputs = job_inputs[index], .resolve_text = resolve_literal};
  check(praktor_execution_service_admit(&service, &admission) == SALTS_OK);
  check(praktor_execution_service_start(&service, &request, &jobs[index]) == SALTS_OK);
  json_free(job_inputs[index]);
  job_inputs[index] = NULL;
}
static praktor_execution_result wait_job(praktor_execution_service *provider,
                                       praktor_service_job *job) {
  praktor_execution_result view = {0};
  uint64_t begin = hrtime();
  while (!view.done) {
    check(praktor_execution_service_poll(provider, job, &view) == SALTS_OK);
    check(hrtime() - begin < UINT64_C(5000000000));
    if (!view.done)
      sleep_ms(1u);
  }
  return view;
}
static const char *result_text(const praktor_execution_result *result_view, const char *key) {
  for (size_t i = 0u; i < result_view->output_count; ++i)
    if (!strcmp(result_view->outputs[i].key, key))
      return result_view->outputs[i].text;
  return NULL;
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

typedef struct wasm_fixture {
  unsigned binds, unbinds, starts, steps, releases;
  const char *output;
  const char *input;
  unsigned refuse_at;
  unsigned max_fuel, max_timeout_ms;
} wasm_fixture;

static praktor_runtime_status bind_wasm(void *context, const char *module, const char *entry,
                                       unsigned int fuel, unsigned int timeout_ms,
                                       void **out) {
  wasm_fixture *fixture = context;
  ++fixture->binds;
  if (fixture->binds == fixture->refuse_at || strcmp(module, "llm.measure") || strcmp(entry, "run"))
    return PRAKTOR_RUNTIME_INVALID;
  if (fuel != 100000u || timeout_ms != 1000u ||
      (fixture->max_fuel && fuel > fixture->max_fuel) ||
      (fixture->max_timeout_ms && timeout_ms > fixture->max_timeout_ms))
    return PRAKTOR_RUNTIME_INVALID;
  *out = fixture;
  return PRAKTOR_RUNTIME_OK;
}
static void unbind_wasm(void *context, void *binding) {
  wasm_fixture *fixture = context;
  if (binding == fixture && fixture->starts == fixture->releases) ++fixture->unbinds;
}
static praktor_runtime_status start_wasm(void *context, void *binding, unsigned int fuel,
    const char *input, size_t size, void **job) {
  wasm_fixture *fixture = context;
  static const char expected[] = "{\"text\":\"literal\"}";
  if (binding != fixture || fuel != 100000u || size != sizeof expected - 1 ||
      memcmp(input, expected, size)) return PRAKTOR_RUNTIME_INVALID;
  ++fixture->starts;
  fixture->input = input;
  *job = fixture;
  return PRAKTOR_RUNTIME_OK;
}
static praktor_runtime_status step_wasm(void *context, void *job,
    praktor_runtime_status (*probe)(void *), void *probe_context,
    char *output, size_t capacity, size_t *size, int *done) {
  wasm_fixture *fixture = context;
  if (job != fixture || strcmp(fixture->input, "{\"text\":\"literal\"}"))
    return PRAKTOR_RUNTIME_INVALID;
  ++fixture->steps;
  praktor_runtime_status status = probe(probe_context);
  if (status != PRAKTOR_RUNTIME_OK || !fixture->output) return status;
  *size = strlen(fixture->output);
  if (*size > capacity) return PRAKTOR_RUNTIME_RESOURCE;
  memcpy(output, fixture->output, *size);
  *done = 1;
  return PRAKTOR_RUNTIME_OK;
}
static void release_wasm(void *context, void *job) {
  wasm_fixture *fixture = context;
  if (job == fixture && fixture->input[0] == '{') ++fixture->releases;
  fixture->input = NULL;
}
static int cancel_wasm(void *context) {
  return ((wasm_fixture *)context)->steps >= 2u;
}
static praktor_runtime_status invoke_wasm(wasm_fixture *fixture, const char *task,
    praktor_cancel_probe cancel) {
  const praktor_wasm_host engine = {
      fixture, bind_wasm, unbind_wasm, start_wasm, step_wasm, release_wasm};
  const praktor_execute_request request = {
      .yaml = source, .yaml_size = tstr_len(source), .limits = praktor_default_limits(),
      .is_cancelled = cancel, .cancel_context = fixture};
  const praktor_runtime_status status = praktor_execute_with_wasm(&request, task, &engine, &result);
  info("WASM execution: %d %s", status, result.error);
  if (result.json) document = json_parse(result.json, result.json_size);
  return status;
}

#define WASM_TASK "  - name: invoke\n    tool: shared\n    vars: {arg: literal}\n    with: {text: $arg}\n"

spec("Reusable YAML execution services") {
  before_each() {
    host = (praktor_component_host){0};
    service = (praktor_execution_service){0};
    memset(jobs, 0, sizeof jobs);
    memset(job_inputs, 0, sizeof job_inputs);
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
    for (size_t i = 0u; i < 2u; ++i)
      json_free(job_inputs[i]);
    json_free(metadata);
    json_free(document);
    praktor_release_result(&result);
    tstr_free(source);
    for (size_t i = 0u; i < 2u; ++i)
      (void)fs_unlink(files[i]);
  }
  it("omits a previous task exit code when a Tool fails before launch") {
    source=workflow(PRAKTOR_SERVICE_ECHO);
    source=tstr_cat(source,
        "  - {name: first, tool: shared, with: {text: ready}}\n"
        "  - name: failed\n"
        "    depends_on: [first]\n"
        "    tool: shared\n"
        "    with: {text: $praktor_test_missing_tool_argument}\n");
    check_equal(execute(),PRAKTOR_RUNTIME_FAILED);
    check_contains(result.error,"tool start failed");
    check_equal(json_get_string(outputs("first"),"exit_code"),"0");
    check_null(outputs("failed"));
  }
  it("binds one instance per name and isolates simultaneous job arguments") {
    setup_host(PRAKTOR_SERVICE_ECHO, true);
    praktor_execution_service again = {0}, other = {0};
    check(praktor_component_host_service(&host, "shared", &again, NULL) == SALTS_OK);
    check(again.self == service.self);
    check(praktor_component_host_service(&host, "other", &other, NULL) == SALTS_OK);
    check(other.self != service.self);
    start_job(0u, "text", "alpha & literal spaces");
    start_job(1u, "text", "beta");
    check(host.processes[0].jobs == 2u);
    check(praktor_component_host_stop(&host) == SALTS_EBUSY);
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_EBUSY);
    praktor_execution_result first = wait_job(&service, &jobs[0]),
                           second = wait_job(&service, &jobs[1]);
    check(first.success && second.success);
    check_equal(result_text(&first, "shell_output"), "alpha & literal spaces");
    check_equal(result_text(&second, "shell_output"), "beta");
  }
  it("runs two generic tasks through one service with native variable resolution") {
    source = tstr_cat_typed(
        NULL, "variables:\n  message: root-value\nservices:\n  shared: '{}'\ntasks:\n",
        PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(
        source,
        "  - name: first\n    tool: shared\n    with:\n      text: $message\n  - name: second\n "
        "   depends_on: [first]\n    vars:\n      message: task-value\n    tool: shared\n    "
        "with:\n      text: $message\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
    check(strcmp(json_get_string(outputs("first"), "stdout"), "root-value") == 0);
    check(strcmp(json_get_string(outputs("second"), "stdout"), "task-value") == 0);
  }
  it("combines an admitted service capability with task values in a guard") {
    source = workflow(PRAKTOR_SERVICE_CURL);
    source = tstr_cat(source,
        "  - name: route\n"
        "    vars: {phase: ready}\n"
        "    if:\n"
        "      guard:\n"
        "        fields: [phase]\n"
        "        match: '$phase == \"ready\" && capability(\"download\") && !capability(\"Download\")'\n"
        "    then: {assert: {guard: {fields: [], expr: 'true'}}}\n"
        "    else: {assert: {guard: {fields: [], expr: 'false'}}}\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
  }
  it("passes an inline JSON value with braces as one literal argument") {
    source = workflow(PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(source,
        "  - name: emit\n"
        "    tool: shared\n"
        "    with: {text: '{\"data\":{\"name\":\"$literal\"}}'}\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
    check(strcmp(json_get_string(outputs("emit"), "stdout"),
                 "{\"data\":{\"name\":\"$literal\"}}") == 0);
  }
  it("binds separate Tool leaves and restarts the same leaf within a repeat") {
    source = workflow(PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(source,
        "  - name: invoke\n"
        "    outputs: [$first, $shell_output]\n"
        "    actions:\n"
        "      Sequence:\n"
        "        - {set_variable: text, value: first}\n"
        "        - repeat: 2\n"
        "          child:\n"
        "            Sequence:\n"
        "              - Tool: {service: shared, with: {text: $text}}\n"
        "              - if: {guard: {fields: [text], match: '$text == \"first\"'}}\n"
        "                then:\n"
        "                  - {set_variable: first, from: $shell_output}\n"
        "                  - {set_variable: text, value: repeated}\n"
        "        - assert: {guard: {fields: [shell_output], match: '$shell_output == \"repeated\"'}}\n"
        "        - {set_variable: text, value: second}\n"
        "        - Tool: {service: shared, with: {text: $text}}\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
    check_equal(json_get_string(outputs("invoke"),"first"),"first");
    check_equal(json_get_string(outputs("invoke"),"shell_output"),"second");
  }
  it("drains a timed-out nested Tool before the fallback invokes another service") {
    source = tstr_cat_typed(NULL,
        "services: {slow: '{}', recover: '{}'}\n"
        "tasks:\n"
        "  - name: invoke\n"
        "    outputs: [$shell_output]\n"
        "    actions:\n"
        "      Fallback:\n"
        "        - timeout: 100\n"
        "          child: {Tool: {service: slow}}\n"
        "        - Tool: {service: recover, with: {text: recovered}}\n",
        PRAKTOR_SERVICE_LONG_SLEEP,PRAKTOR_SERVICE_ECHO);
    check(execute() == PRAKTOR_RUNTIME_OK);
    check_equal(json_get_string(outputs("invoke"),"shell_output"),"recovered");
  }
  it("rejects a retired reference in an invocation before starting a tool") {
    source = workflow(PRAKTOR_SERVICE_ECHO);
    source = tstr_cat(source,
        "  - name: emit\n"
        "    tool: shared\n"
        "    with: {text: '{legacy}'}\n");
    check(execute() != PRAKTOR_RUNTIME_OK);
    check_null(result.json);
    check_contains(result.error,"Use $name for a tool value reference");
  }
  it("takes a false guard branch when the workflow has no services") {
    source = tstr_dup(
        "tasks:\n"
        "  - name: route\n"
        "    vars: {phase: ready}\n"
        "    if:\n"
        "      guard:\n"
        "        fields: [phase]\n"
        "        match: 'capability(\"download\")'\n"
        "    then: {assert: {guard: {fields: [], expr: 'false'}}}\n"
        "    else: {assert: {guard: {fields: [], expr: 'true'}}}\n");
    check(execute() == PRAKTOR_RUNTIME_OK);
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
                       "tool: shared\n    with:\n      text: $PRAKTOR_CLI_ENV_TEST_VALUE\n",
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
  it("runs an admitted WASM tool as a task and a BT leaf") {
    source = workflow(PRAKTOR_SERVICE_WASM);
    source = tstr_cat(source, WASM_TASK
      "  - name: nested\n    depends_on: [invoke]\n    vars: {arg: literal}\n"
      "    outputs: [$argument_metrics]\n    actions:\n      Sequence:\n"
      "        - Tool:\n            service: shared\n            with: {text: $arg}\n");
    wasm_fixture fixture = {.output = "{\"bytes\":7}"};
    check(invoke_wasm(&fixture, "nested", NULL) == PRAKTOR_RUNTIME_OK);
    check(fixture.binds == 1 && fixture.unbinds == 1);
    check(fixture.starts == 2 && fixture.steps == 2 && fixture.releases == 2);
    check_equal(json_object_size(outputs("invoke")), 1u);
    const char *direct = json_get_string(outputs("invoke"), "argument_metrics");
    const char *nested = json_get_string(outputs("nested"), "argument_metrics");
    check(direct && strcmp(direct, fixture.output) == 0);
    check(nested && strcmp(nested, fixture.output) == 0);
  }
  it("requires an explicit WASM host for WASM service declarations") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_WASM), WASM_TASK);
    check(execute() == PRAKTOR_RUNTIME_UNSUPPORTED);
    check_contains(result.error, "requires an explicit execution host");
  }
  it("checks WASM service declarations and Tool inputs before binding a host") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_WASM), WASM_TASK);
    const praktor_execute_request request = {
        .yaml = source, .yaml_size = tstr_len(source), .limits = praktor_default_limits()};
    check_equal(praktor_check(&request, &result), PRAKTOR_RUNTIME_OK);
    check_null(result.json);
  }
  it("checks executable Tool inputs without opening a process") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_ECHO), "  - name: missing\n    tool: shared\n");
    const praktor_execute_request request = {
        .yaml = source, .yaml_size = tstr_len(source), .limits = praktor_default_limits()};
    check_equal(praktor_check(&request, &result), PRAKTOR_RUNTIME_INVALID);
    check_contains(result.error, "missing tool input");
    check_null(result.json);
  }
  it("releases an earlier WASM binding when another service fails admission") {
    source = tstr_cat_typed(NULL, "services:\n  shared: '{}'\n  later: '{}'\ntasks:\n" WASM_TASK,
                          PRAKTOR_SERVICE_WASM, PRAKTOR_SERVICE_WASM);
    wasm_fixture fixture = {.refuse_at = 2};
    check(invoke_wasm(&fixture, NULL, NULL) == PRAKTOR_RUNTIME_INVALID);
    check(fixture.binds == 2 && !fixture.starts && fixture.unbinds == 1);
    check_contains(result.error, "host refused module");
  }
  it("lets the WASM host reject either requested limit before starting a job") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_WASM), WASM_TASK);
    wasm_fixture cases[] = {{.max_fuel = 99999u}, {.max_timeout_ms = 999u}};
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
      check(invoke_wasm(&cases[i], NULL, NULL) == PRAKTOR_RUNTIME_INVALID);
      check(cases[i].binds == 1 && !cases[i].starts && !cases[i].unbinds);
      check_contains(result.error, "fuel 100000, timeout_ms 1000");
      praktor_release_result(&result);
      json_free(document);
      document = NULL;
    }
  }
  it("cancels a yielded WASM job before unbinding the module") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_WASM), WASM_TASK);
    wasm_fixture fixture = {0};
    check(invoke_wasm(&fixture, NULL, cancel_wasm) == PRAKTOR_RUNTIME_CANCELLED);
    check(fixture.starts == 1 && fixture.steps == 2 && fixture.releases == 1 && fixture.unbinds == 1);
    check(!json_object_size(outputs("invoke")));
  }
  it("stops a BT on malformed guest JSON before publishing an output") {
    source = tstr_cat(workflow(PRAKTOR_SERVICE_WASM),
      "  - name: invoke\n    outputs: [$argument_metrics]\n    actions:\n"
      "      Fallback:\n        - Tool: {service: shared, with: {text: literal}}\n"
      "        - {set_variable: argument_metrics, value: fallback}\n");
    wasm_fixture fixture = {.output = "{} true"};
    check(invoke_wasm(&fixture, NULL, NULL) == PRAKTOR_RUNTIME_INVALID);
    check(fixture.releases == 1 && fixture.unbinds == 1);
    check(!json_object_size(outputs("invoke")));
    check_contains(result.error, "one JSON document");
  }
  it("rejects duplicate capability declarations before task IO") {
    source = tstr_cat_typed(NULL,
                            "services:\n  shared: '{}'\n  invalid: '{}'\ntasks:\n  - name: first\n"
                            "    tool: shared\n    with:\n      url: '{}'\n      path: '{}'\n",
                            PRAKTOR_SERVICE_CURL, PRAKTOR_SERVICE_DUPLICATE_CAPABILITY,
                            PRAKTOR_SERVICE_PAYLOAD_URL, files[0]);
    check(execute() == PRAKTOR_RUNTIME_INVALID);
    check_contains(result.error, "duplicate service capability");
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
    start_job(0u, NULL, NULL);
    praktor_execution_result initial = {0};
    check(praktor_execution_service_poll(&service, &jobs[0], &initial) == SALTS_OK);
    check(!initial.done);
    check(praktor_execution_service_cancel(&service, &jobs[0]) == SALTS_OK);
    check(praktor_execution_service_cancel(&service, &jobs[0]) == SALTS_OK);
    praktor_execution_result view = wait_job(&service, &jobs[0]);
    check(view.done && view.cancelled && !view.success);
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
    check(host.processes[0].jobs == 0u);
  }
  it("honors the provider timeout after actual process admission") {
    setup_host(PRAKTOR_SERVICE_SLEEP, false);
    start_job(0u, NULL, NULL);
    uint64_t started = hrtime();
    praktor_execution_result view = wait_job(&service, &jobs[0]);
    check(!view.success && !view.cancelled);
    check(hrtime() - started >= UINT64_C(20000000));
    check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
  }
  it("rejects binary and oversized output without truncating it to success") {
    setup_host(PRAKTOR_SERVICE_CAPTURE, false);
    const char *modes[] = {"binary", "bulk"};
    for (size_t i = 0u; i < 2u; ++i) {
      start_job(0u, "mode", modes[i]);
      praktor_execution_result view = wait_job(&service, &jobs[0]);
      check(!view.success);
      check_not_null(result_text(&view, "shell_exit_code"));
      check_null(result_text(&view, "shell_output"));
      check(strstr(view.error, i == 0u ? "NUL-free" : "capture limit") != NULL);
      check(praktor_execution_service_release(&service, &jobs[0]) == SALTS_OK);
    }
  }
}
