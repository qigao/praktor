#include <praktor.h>
#include <stddef.h>
#include <string.h>
int main(void) {
    const char yaml[] =
        "tasks:\n  - name: hello\n    set_variable: observed\n    value: ready\n";
    praktor_execute_request request = {0};
    praktor_runtime_result out = {0};
    request.yaml = yaml;
    request.yaml_size = sizeof yaml - 1u;
    request.limits = praktor_default_limits();
    if (PRAKTOR_ABI_MAJOR != 3u || praktor_config_reflection() == NULL)
        return 1;
    if (praktor_execute(&request, &out) != PRAKTOR_RUNTIME_OK ||
        out.json == NULL || !strstr(out.json, "\"workflow_status\":\"success\""))
        return 2;
    praktor_release_result(&out);
    return out.json == NULL && out.json_size == 0u ? 0 : 3;
}
