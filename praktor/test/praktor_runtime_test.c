#include <tinytest.h>
#include <praktor/runtime.h>
#include <string.h>
spec("C11 Configurator CFlow CMeta runtime") {
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
