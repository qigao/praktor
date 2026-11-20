#include "praktor.h"
#include <tinytest.h>
#include <json_parser.h>
#include <string.h>
static const char *required_text(const json_value_t *object, const char *key) {
    const char *text = json_get_string(object, key);
    check(text != NULL);
    return text;
}

typedef struct event_fixture {
    unsigned document_polls,task_polls;
    int failure;
    const char *attempt;
    praktor_event_field fields[2];
} event_fixture;

static int poll_events(void *context,const char *task,praktor_host_event *event) {
    event_fixture *fixture=context;
    if(!task) {
        ++fixture->document_polls;
        if(fixture->document_polls==1u)return 0;
        *event=(praktor_host_event){.name="host.ready"};
        return 1;
    }
    if(strcmp(task,"device"))return -9;
    ++fixture->task_polls;
    if(fixture->failure)return fixture->failure;
    fixture->fields[0]=(praktor_event_field){"attempt",fixture->attempt};
    fixture->fields[1]=(praktor_event_field){"token","$literal"};
    *event=(praktor_host_event){.name="device.ack",.fields=fixture->fields,.field_count=2u};
    return 1;
}

static const char external_yaml[]=
    "statechart:\n"
    "  initial: waiting\n"
    "  events: {host.ready: {source: external}}\n"
    "  states:\n"
    "    waiting: {transitions: [{'on': host.ready, target: ready}]}\n"
    "    ready: {type: final}\n"
    "tasks:\n"
    "  - name: device\n"
    "    statechart:\n"
    "      initial: polling\n"
    "      events: {device.ack: {source: external, fields: {attempt: int64, token: text}}}\n"
    "      states:\n"
    "        polling:\n"
    "          transitions:\n"
    "            - 'on': device.ack\n"
    "              target: accepted\n"
    "              guard: {fields: [event.data.attempt, event.data.token], match: '$event.data.attempt == \"9007199254740993\" && $event.data.token == \"$literal\"'}\n"
    "            - 'on': device.ack\n"
    "              target: refused\n"
    "        accepted: {type: final}\n"
    "        refused: {type: final}\n"
    "    wait_until:\n"
    "      guard: {fields: [chart.workflow.ready, chart.task.accepted], expr: '$chart.workflow.ready == 1 && $chart.task.accepted == 1'}\n"
    "      timeout_ms: 200\n";

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
        const char yaml[] = "defaults: {timeout: 5000, stream_output: false}\n"
            "tasks:\n  - name: shell\n    command: echo sdk-shell\n";
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
    it("routes host events to the document and named task charts") {
        const praktor_execute_request request={
            .yaml=external_yaml,.yaml_size=sizeof external_yaml-1u,
            .limits=praktor_default_limits()
        };
        event_fixture fixture={.attempt="9007199254740993"};
        check(praktor_execute_with_events(&request,"device",poll_events,&fixture,&shell_result)
              ==PRAKTOR_RUNTIME_OK,"%s",shell_result.error);
        check_equal(fixture.document_polls,2u);
        check_equal(fixture.task_polls,1u);
    }
    it("fails on an invalid typed host event rather than taking a transition") {
        const praktor_execute_request request={
            .yaml=external_yaml,.yaml_size=sizeof external_yaml-1u,
            .limits=praktor_default_limits()
        };
        event_fixture fixture={.attempt="9223372036854775808"};
        check_equal(praktor_execute_with_events(&request,NULL,poll_events,&fixture,&shell_result),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(shell_result.error,"invalid typed host event payload");
    }
    it("propagates the event source error to the execution result") {
        const praktor_execute_request request={
            .yaml=external_yaml,.yaml_size=sizeof external_yaml-1u,
            .limits=praktor_default_limits()
        };
        event_fixture fixture={.failure=-7};
        check_equal(praktor_execute_with_events(&request,NULL,poll_events,&fixture,&shell_result),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(shell_result.error,"event source for 'device' returned -7");
    }
    it("rejects a BT Raise for a host-owned event at admission") {
        const char yaml[]=
            "tasks:\n"
            "  - name: device\n"
            "    statechart: {initial: waiting, states: {waiting: {}}, events: {device.ack: {source: external}}}\n"
            "    actions: {raise: {event: device.ack}}\n";
        const praktor_execute_request request={
            .yaml=yaml,.yaml_size=sizeof yaml-1u,.limits=praktor_default_limits()
        };
        event_fixture fixture={0};
        check_equal(praktor_execute_with_events(&request,NULL,poll_events,&fixture,&shell_result),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(shell_result.json);
        check_equal(fixture.task_polls,0u);
        check_contains(shell_result.error,"declared internal event");
    }
}
