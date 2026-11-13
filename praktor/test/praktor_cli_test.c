#include <core/process.h>
#include <json_parser.h>
#include <string.h>
#include <tinytest.h>

static const char *required_text(const json_value_t *object, const char *key) {
    const char *text = json_get_string(object, key);
    check(text != NULL);
    return text;
}

spec("Praktor CLI workflow execution") {
    static process_t *child;
    static json_value_t *document;
    before_each() { child = NULL; document = NULL; }
    after_each() { json_free(document); if (child) process_destroy(child); }

    it("executes a workflow file and returns shell output as canonical JSON") {
        const char *arguments[] = {PRAKTOR_CLI_WORKFLOW, NULL};
        process_options_t options;
        process_result_t result = {0};
        char output[8192] = {0};
        size_t size = 0u;
        const json_value_t *task, *outputs;
        process_options_init(&options);
        options.program = PRAKTOR_CLI_PATH;
        options.args = arguments;
        options.timeout_ms = 5000u;
        options.max_output_bytes = sizeof(output) - 1u;
        check(process_spawn(&options, &child) == SALTS_OK);
        check(process_wait_for(child, 10000u, &result) == SALTS_OK);
        check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 0);
        check(process_read_stdout(child, output, sizeof(output) - 1u, &size) == SALTS_OK);
        document = json_parse(output, size);
        check(document != NULL);
        check(strcmp(required_text(document, "workflow_status"), "success") == 0);
        task = json_object_get(json_object_get(document, "tasks"), "shell");
        check(task != NULL);
        outputs = json_object_get(task, "outputs");
        check(outputs != NULL);
        check(strstr(required_text(outputs, "stdout"), "cli-shell") != NULL);
        check(strcmp(required_text(outputs, "exit_code"), "0") == 0);
    }
    it("rejects a missing workflow argument with a usage error") {
        process_options_t options;
        process_result_t result = {0};
        char error[256] = {0};
        size_t size = 0u;
        process_options_init(&options);
        options.program = PRAKTOR_CLI_PATH;
        options.timeout_ms = 5000u;
        check(process_spawn(&options, &child) == SALTS_OK);
        check(process_wait_for(child, 10000u, &result) == SALTS_OK);
        check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 2);
        check(process_read_stderr(child, error, sizeof(error) - 1u, &size) == SALTS_OK);
        check(strstr(error, "usage: praktor workflow.yml") != NULL);
    }
    it("loads dotEnv and evaluates root variables and task vars through the shared library") {
        const char *arguments[] = {PRAKTOR_CLI_ENV_WORKFLOW, NULL};
        process_options_t options;
        process_result_t result = {0};
        char output[8192] = {0};
        size_t size = 0u;
        process_options_init(&options);
        options.program = PRAKTOR_CLI_PATH;
        options.args = arguments;
        options.timeout_ms = 5000u;
        options.max_output_bytes = sizeof(output) - 1u;
        check(process_spawn(&options, &child) == SALTS_OK);
        check(process_wait_for(child, 10000u, &result) == SALTS_OK);
        check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 0);
        check(process_read_stdout(child, output, sizeof(output) - 1u, &size) == SALTS_OK);
        document = json_parse(output, size);
        check(document != NULL);
        const json_value_t *tasks = json_object_get(document, "tasks");
        const json_value_t *root = json_object_get(json_object_get(tasks, "root"), "outputs");
        const json_value_t *overlay = json_object_get(json_object_get(tasks, "overlay"), "outputs");
        check(strstr(required_text(root, "stdout"), "root-scope") != NULL);
        check(strstr(required_text(overlay, "stdout"), "dotenv-loaded") != NULL);
    }
    it("accepts an explicit workflow timeout through the positive int boundary") {
        const char *arguments[] = {PRAKTOR_CLI_WORKFLOW, "--timeout-ms", "2147483647", NULL};
        process_options_t options;
        process_result_t result = {0};
        process_options_init(&options);
        options.program = PRAKTOR_CLI_PATH;
        options.args = arguments;
        options.timeout_ms = 5000u;
        check(process_spawn(&options, &child) == SALTS_OK);
        check(process_wait_for(child, 10000u, &result) == SALTS_OK);
        check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 0);
    }
    it("applies the requested deadline to a running workflow") {
        const char *arguments[] = {PRAKTOR_CLI_TIMEOUT_WORKFLOW, "--timeout-ms", "50", NULL};
        process_options_t options;
        process_result_t result = {0};
        char output[8192] = {0};
        size_t size = 0u;
        process_options_init(&options);
        options.program = PRAKTOR_CLI_PATH;
        options.args = arguments;
        options.timeout_ms = 5000u;
        options.max_output_bytes = sizeof(output) - 1u;
        check(process_spawn(&options, &child) == SALTS_OK);
        check(process_wait_for(child, 10000u, &result) == SALTS_OK);
        check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 1);
        check(process_read_stdout(child, output, sizeof(output) - 1u, &size) == SALTS_OK);
        document = json_parse(output, size);
        check(document != NULL);
        check(strcmp(required_text(document, "workflow_status"), "timed_out") == 0);
    }
    it("rejects missing, malformed, nonpositive and overflowing timeout values") {
        const char *invalid[] = {NULL, "", "0", "-1", "invalid", "10ms", "2147483648"};
        for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            const char *arguments[] = {PRAKTOR_CLI_WORKFLOW, "--timeout-ms", invalid[i], NULL};
            process_options_t options;
            process_result_t result = {0};
            process_options_init(&options);
            options.program = PRAKTOR_CLI_PATH;
            options.args = arguments;
            options.timeout_ms = 5000u;
            check(process_spawn(&options, &child) == SALTS_OK);
            check(process_wait_for(child, 10000u, &result) == SALTS_OK);
            check(result.state == SALTS_PROCESS_EXITED && result.exit_code == 2);
            process_destroy(child); child = NULL;
        }
    }
}
