#include <praktor.h>
#include <stdio.h>
#include <string.h>

typedef struct host_retry_smoke_s {
  int invoke_calls;
} host_retry_smoke_t;

static int32_t PRAKTOR_CALL smoke_validate(
    void *user_data,
    const char *tool_name,
    const char *arguments_template_json,
    size_t arguments_template_json_size,
    praktor_error *error) {
  (void)user_data;
  (void)arguments_template_json;
  (void)arguments_template_json_size;
  (void)error;
  return tool_name && strcmp(tool_name, "smoke.inspect") == 0
             ? PRAKTOR_HOST_TOOL_OK
             : PRAKTOR_HOST_TOOL_NOT_FOUND;
}

static int32_t PRAKTOR_CALL smoke_invoke(
    void *user_data,
    const char *tool_name,
    const char *arguments_json,
    size_t arguments_json_size,
    const praktor_execution_control *control,
    const praktor_execution_observer *observer,
    praktor_host_tool_result_sink_fn result_sink,
    void *result_sink_user_data,
    praktor_error *error) {
  host_retry_smoke_t *state = (host_retry_smoke_t *)user_data;
  static const char result[] = "{\"ok\":true}";
  (void)arguments_json;
  (void)arguments_json_size;
  (void)control;
  (void)observer;
  (void)error;
  if (!state || !tool_name || strcmp(tool_name, "smoke.inspect") != 0 ||
      !result_sink) {
    return PRAKTOR_HOST_TOOL_FAILED;
  }
  ++state->invoke_calls;
  if (state->invoke_calls <= 2) {
    return PRAKTOR_HOST_TOOL_FAILED;
  }
  return result_sink(result, sizeof(result) - 1u, result_sink_user_data) == 0
             ? PRAKTOR_HOST_TOOL_OK
             : PRAKTOR_HOST_TOOL_FAILED;
}

static int run_one(const char *path, const char *needle) {
  praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
  praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
  praktor_error error = PRAKTOR_ERROR_INIT;
  praktor_result result;

  request.workflow_path = path;
  request.input_json = "{}";
  request.input_json_size = 2;
  result = praktor_execute_workflow(&request, &output, &error);
  if (result != PRAKTOR_RESULT_SUCCESS) {
    fprintf(stderr, "workflow failed: %d: %s\n", (int)result, error.message);
    praktor_release_json(&output);
    return 1;
  }
  if (!output.data || !strstr(output.data, needle)) {
    fprintf(stderr, "missing result evidence: %s\n", needle);
    if (output.data) fprintf(stderr, "%s\n", output.data);
    praktor_release_json(&output);
    return 1;
  }
  praktor_release_json(&output);
  return 0;
}

static int check_inline_plan(void) {
  static const char source[] =
      "tasks:\n"
      "  - name: inspect\n"
      "    tool: smoke.inspect\n"
      "    retries:\n"
      "      count: 2\n"
      "    with: {}\n";
  praktor_compile_inline_request request = PRAKTOR_COMPILE_INLINE_REQUEST_INIT;
  praktor_workflow_plan *plan = NULL;
  praktor_owned_json description = PRAKTOR_OWNED_JSON_INIT;
  praktor_error error = PRAKTOR_ERROR_INIT;

  request.source_id = "package:smoke:inline";
  request.workflow_yaml = source;
  request.workflow_yaml_size = sizeof(source) - 1u;
  if (praktor_compile_workflow_inline(&request, &plan, &error) !=
          PRAKTOR_RESULT_SUCCESS ||
      !plan) {
    fprintf(stderr, "inline compile failed: %s\n", error.message);
    return 1;
  }
  if (praktor_describe_workflow_plan(plan, &description, &error) !=
          PRAKTOR_RESULT_SUCCESS ||
      !description.data ||
      !strstr(description.data, "\"source_kind\":\"inline\"") ||
      !strstr(description.data, "package:smoke:inline") ||
      !strstr(description.data, "\"retry_count\":2")) {
    fprintf(stderr, "inline describe failed: %s\n", error.message);
    praktor_release_json(&description);
    praktor_release_workflow_plan(plan);
    return 1;
  }
  praktor_release_json(&description);

  {
    static const char input[] = "{}";
    host_retry_smoke_t retry_state = {0};
    praktor_host_tool_executor host = PRAKTOR_HOST_TOOL_EXECUTOR_INIT;
    praktor_plan_execute_request execute = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    host.user_data = &retry_state;
    host.validate = &smoke_validate;
    host.invoke = &smoke_invoke;
    execute.plan = plan;
    execute.input_json = input;
    execute.input_json_size = sizeof(input) - 1u;
    if (praktor_execute_workflow_plan_host_tools(
            &execute, NULL, NULL, &host, &output, &error) !=
            PRAKTOR_RESULT_SUCCESS ||
        retry_state.invoke_calls != 3 ||
        !output.data ||
        !strstr(output.data, "\"workflow_status\":\"success\"")) {
      fprintf(stderr, "inline retry execution failed: %s\n", error.message);
      praktor_release_json(&output);
      praktor_release_workflow_plan(plan);
      return 1;
    }
    praktor_release_json(&output);
  }

  praktor_release_workflow_plan(plan);
  return 0;
}

int main(int argc, char **argv) {
  const praktor_api *api = praktor_get_api();
  if (argc != 3) return 2;
  if (!api) return 3;
  if ((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) == 0) return 4;
  if ((api->capabilities & PRAKTOR_CAPABILITY_SCRIPT_ENGINE) == 0) return 5;
  if ((api->capabilities & PRAKTOR_CAPABILITY_EXECUTION_CONTROL) == 0) return 6;
  if (api->abi_major != PRAKTOR_ABI_MAJOR || api->abi_minor < 6) return 7;
  if (!api->execute_workflow_controlled) return 8;
  if ((api->capabilities & PRAKTOR_CAPABILITY_HOST_TOOL) == 0) return 9;
  if (!api->execute_workflow_plan_host_tools) return 10;
  if ((api->capabilities & PRAKTOR_CAPABILITY_INLINE_WORKFLOW_PLAN) == 0) return 11;
  if (!api->compile_workflow_inline) return 12;
  if (PRAKTOR_HOST_TOOL_MAX_RETRIES != 1024u) return 13;
  if (check_inline_plan() != 0) return 14;
  if (run_one(argv[1], "success") != 0) return 15;
  if (run_one(argv[2], "42") != 0) return 16;
  puts("PRAKTOR_SCRIPT_ENABLED_REAL_ABI_OK");
  return 0;
}
