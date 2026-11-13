#include "praktor.h"
#include <tinytest.h>
#include <json_parser.h>
#include <string.h>
static const char *required_text(const json_value_t *object, const char *key) {
    const char *text = json_get_string(object, key);
    check(text != NULL);
    return text;
}

spec("C11 Praktor SDK ABI v3") {
    static praktor_runtime_result shell_result;
    static json_value_t *document;
    before_each() { shell_result = (praktor_runtime_result){0}; document = NULL; }
    after_each() { json_free(document); praktor_release_result(&shell_result); }
    it("uses the C11-only API and canonical CMeta reflection") {
        const char yaml[]=
            "tasks:\n  - name: step\n    set_variable: answer\n    value: ok\n";
        praktor_execute_request request={0};
        praktor_runtime_result result={0};
        request.yaml=yaml;request.yaml_size=sizeof(yaml)-1u;
        request.limits=praktor_default_limits();
        check(PRAKTOR_ABI_MAJOR==3u);
        check(praktor_config_reflection()!=NULL);
        check(praktor_execute(&request,&result)==PRAKTOR_RUNTIME_OK);
        praktor_release_result(&result);
        check(result.json==NULL);
    }
    it("loads environment through the public dynamic-library entrypoint without executing tasks") {
        const char yaml[]="tasks:\n  - name: unused\n    command: exit 7\n";
        const char invalid[]="dotEnv: ['']\ntasks: []\n";
        check(praktor_load_environment(yaml,sizeof(yaml)-1u)==PRAKTOR_RUNTIME_OK);
        check(praktor_load_environment(NULL,1u)==PRAKTOR_RUNTIME_INVALID);
        check(praktor_load_environment(yaml,0u)==PRAKTOR_RUNTIME_INVALID);
        check(praktor_load_environment(invalid,sizeof(invalid)-1u)==PRAKTOR_RUNTIME_INVALID);
    }
    it("publishes real shell output through the shared-library API after native drain") {
        const char yaml[] = "tasks:\n  - name: shell\n    command: echo sdk-shell\n";
        praktor_execute_request request = {0};
        const json_value_t *task, *outputs;
        request.yaml = yaml; request.yaml_size = sizeof(yaml) - 1u;
        request.limits = praktor_default_limits();
        praktor_runtime_status status = praktor_execute(&request, &shell_result);
        info("status %d, error %s, result %s", status, shell_result.error,
            shell_result.json ? shell_result.json : "<none>");
        check(status == PRAKTOR_RUNTIME_OK);
        document = json_parse(shell_result.json, shell_result.json_size);
        check(document != NULL);
        check(strcmp(required_text(document, "workflow_status"), "success") == 0);
        task = json_object_get(json_object_get(document, "tasks"), "shell");
        check(task != NULL);
        outputs = json_object_get(task, "outputs");
        check(outputs != NULL);
        check(strcmp(required_text(outputs, "exit_code"), "0") == 0);
        check(strstr(required_text(outputs, "stdout"), "sdk-shell") != NULL);
    }
    it("reports a failed shell without publishing successful task outputs") {
        const char yaml[] = "tasks:\n  - name: shell\n    command: exit 7\n";
        praktor_execute_request request = {0};
        request.yaml = yaml; request.yaml_size = sizeof(yaml) - 1u;
        request.limits = praktor_default_limits();
        check(praktor_execute(&request, &shell_result) == PRAKTOR_RUNTIME_FAILED);
        document = json_parse(shell_result.json, shell_result.json_size);
        check(document != NULL);
        check(strcmp(required_text(document, "workflow_status"), "failed") == 0);
        check(strcmp(required_text(json_object_get(json_object_get(document, "tasks"),
            "shell"), "status"), "failed") == 0);
        const json_value_t *outputs = json_object_get(json_object_get(
            json_object_get(document, "tasks"), "shell"), "outputs");
        check(!outputs || json_object_get(outputs, "stdout") == NULL);
    }
}
