#include "praktor.h"
#include <tinytest.h>
spec("C11 Praktor SDK ABI v3") {
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
}
