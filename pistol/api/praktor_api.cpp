#include "praktor.h"
#include "workflow_runner.hpp"
#include <jsoncons/json.hpp>
#include <unordered_map>
#include <string>
#include <iostream>

praktor_result PRAKTOR_CALL praktor_execute_workflow(const char* workflow_path,
                                                     const char* json_vars) {
    if (!workflow_path || workflow_path[0] == '\0') {
        return PRAKTOR_API_INVALID_ARGUMENT;
    }

    try {
        std::unordered_map<std::string, std::string> initial_vars;
        if (json_vars && json_vars[0] != '\0') {
            auto j = jsoncons::json::parse(json_vars);
            if (!j.is_object()) {
                return PRAKTOR_API_INVALID_JSON;
            }

            for (const auto& kv : j.object_range()) {
                if (kv.value().is_string()) {
                    initial_vars[kv.key()] = kv.value().as_string();
                } else {
                    initial_vars[kv.key()] = kv.value().to_string();
                }
            }
        }

        WorkflowRunner runner(workflow_path, initial_vars);
        return runner.run() ? PRAKTOR_API_SUCCESS : PRAKTOR_API_EXECUTION_FAILED;

    } catch (const std::exception& e) {
        std::cerr << "[Praktor API Error] Exception during workflow execution: " << e.what() << std::endl;
        return PRAKTOR_API_INVALID_JSON;
    }
}

namespace {

int32_t PRAKTOR_CALL execute_workflow_v1(const char* workflow_path, const char* json_vars) {
    return static_cast<int32_t>(praktor_execute_workflow(workflow_path, json_vars));
}

} // namespace

const praktor_api_v1* PRAKTOR_CALL praktor_get_api_v1(void) {
    static const praktor_api_v1 api = {
        sizeof(praktor_api_v1),
        PRAKTOR_ABI_MAJOR,
        PRAKTOR_ABI_MINOR,
        PRAKTOR_CAPABILITY_EXECUTE_WORKFLOW,
        &execute_workflow_v1,
    };
    return &api;
}

 
