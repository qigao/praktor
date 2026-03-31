#include "praktor_api.h"
#include "workflow_runner.hpp"
#include <jsoncons/json.hpp>
#include <unordered_map>
#include <string>
#include <iostream>

extern "C" {

int praktor_execute_workflow(const char* workflow_path, const char* json_vars) {
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
    } catch (...) {
        std::cerr << "[Praktor API Error] Unknown exception during workflow execution" << std::endl;
        return PRAKTOR_API_INVALID_ARGUMENT;
    }
}

}
