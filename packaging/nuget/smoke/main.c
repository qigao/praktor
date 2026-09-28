#include <praktor.h>
#include <stdio.h>
#include <string.h>

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

int main(int argc, char **argv) {
  const praktor_api *api = praktor_get_api();
  if (argc != 3) return 2;
  if (!api) return 3;
  if ((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) == 0) return 4;
  if ((api->capabilities & PRAKTOR_CAPABILITY_SCRIPT_ENGINE) == 0) return 5;
  if (run_one(argv[1], "success") != 0) return 6;
  if (run_one(argv[2], "42") != 0) return 7;
  puts("PRAKTOR_SCRIPT_ENABLED_REAL_ABI_OK");
  return 0;
}
