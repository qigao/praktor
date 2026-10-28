#include "praktor.h"
praktor_runtime_status praktor_execute(
    const praktor_execute_request *request,praktor_runtime_result *out) {
    if(!request||!out)return PRAKTOR_RUNTIME_INVALID;
    return praktor_runtime_execute_yaml(
        request->yaml,request->yaml_size,&request->limits,
        request->is_cancelled,request->cancel_context,out);
}
void praktor_release_result(praktor_runtime_result *out) {
    praktor_runtime_result_destroy(out);
}
praktor_runtime_limits praktor_default_limits(void) {
    return praktor_runtime_limits_default();
}
const cmeta_data_desc *praktor_config_reflection(void) {
    return praktor_runtime_limits_reflection();
}
