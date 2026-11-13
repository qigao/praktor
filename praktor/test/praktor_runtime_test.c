#include <tinytest.h>
#include <praktor/runtime.h>
#include <string.h>
#if defined(_WIN32)
#define BLOCKING_COMMAND "ping -n 3 127.0.0.1 > NUL"
#else
#define BLOCKING_COMMAND "sleep 2"
#endif
spec("C11 Configurator CFlow CMeta runtime") {
    static praktor_runtime_result execution;
    before_each() { execution=(praktor_runtime_result){0}; }
    after_each() { praktor_runtime_result_destroy(&execution); }
    it("applies the environment command timeout when the document omits it") {
        const char yaml[]="tasks:\n  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.json,"\"workflow_status\":\"failed\"");
    }
    it("overrides an environment timeout with document defaults for lowered Command nodes") {
        const char yaml[]="defaults: {timeout: 5000, stream_output: false}\ntasks:\n"
            "  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"workflow_status\":\"success\"");
    }
    it("lets an explicit Shell timeout override the document timeout") {
        const char yaml[]="defaults: {timeout: 50}\ntasks:\n  - name: slow\n"
            "    shell:\n      cmd: " BLOCKING_COMMAND "\n      timeout: 5000\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_OK);
    }
    it("keeps the workflow deadline independent of inherited command timeout") {
        const char yaml[]="defaults: {timeout: 5000}\ntasks:\n"
            "  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        praktor_runtime_limits limits=praktor_runtime_limits_default();limits.timeout_ms=50;
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,&limits,NULL,NULL,&execution)==PRAKTOR_RUNTIME_DEADLINE);
    }
    it("runs a two-task native SetVariable DAG") {
        const char yaml[]=
            "tasks:\n"
            "  - name: first\n    set_variable: answer\n    value: ready\n"
            "  - name: second\n    depends_on: [first]\n"
            "    set_variable: done\n    from: answer\n";
        praktor_runtime_result result={0};
        check(praktor_runtime_limits_reflection()!=NULL);
        check(praktor_runtime_execute_yaml(yaml,sizeof(yaml)-1u,
            NULL,NULL,NULL,&result)==PRAKTOR_RUNTIME_OK);
        check(result.json!=NULL);
        if(result.json)check(strstr(result.json,"\"workflow_status\":\"success\"")!=NULL);
        praktor_runtime_result_destroy(&result);
    }
    it("refuses old C++ script workflows without fallback") {
        const char yaml[]="tasks:\n  - name: old\n    script: print(1)\n";
        praktor_runtime_result result={0};
        check(praktor_runtime_execute_yaml(yaml,sizeof(yaml)-1u,
            NULL,NULL,NULL,&result)!=PRAKTOR_RUNTIME_OK);
        praktor_runtime_result_destroy(&result);
    }
}
