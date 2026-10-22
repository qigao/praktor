#include "praktor_test.hpp"
#include "praktor.h"


#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path createApiTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-core-api-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    check((out.is_open()));
    out << text;
}

praktor_result executeFile(const std::filesystem::path& path,
                           praktor_owned_json& output,
                           praktor_error& error) {
    static const char input[] = "{}";
    const std::string stable_path = path.string();
    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;
    return praktor_execute_workflow(&request, &output, &error);
}

int32_t PRAKTOR_CALL alwaysCancelled(void*) {
    return 1;
}

} // namespace

[[maybe_unused]] static void test_core_only_abi_advertises_json_workflows_without_script_capability_1() {
    const praktor_api* api = praktor_get_api();

    check((api != nullptr));
    check(((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) != 0));
    check(((api->capabilities & PRAKTOR_CAPABILITY_SCRIPT_ENGINE) == 0));
    check(((api->capabilities & PRAKTOR_CAPABILITY_EXECUTION_CONTROL) != 0));
    check((api->execute_workflow_controlled != nullptr));
}

spec("core-only ABI advertises JSON workflows without script capability") {
    it("complete case") { test_core_only_abi_advertises_json_workflows_without_script_capability_1(); }
}

[[maybe_unused]] static void test_core_only_abi_executes_command_workflow_and_rejects_script_workflow_2() {
    const auto dir = createApiTempDir();
    const auto command_workflow = dir / "command.yml";
    const auto script_workflow = dir / "script.yml";

    writeText(command_workflow, R"(
tasks:
  - name: hello
    command: "echo core-only-api"
)");
    writeText(script_workflow, R"(
tasks:
  - name: script_task
    script: |
      ctx.output("value", 1);
)");

    {
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;

        check((executeFile(command_workflow, output, error) == PRAKTOR_RESULT_SUCCESS));
        check((output.data != nullptr));
        check((std::strstr(output.data, "\"workflow_status\":\"success\"") != nullptr));
        praktor_release_json(&output);
    }

    {
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;

        check((executeFile(script_workflow, output, error) == PRAKTOR_RESULT_EXECUTION_FAILED));
        check((output.data != nullptr));
        check((std::strstr(output.data, "\"workflow_status\":\"failed\"") != nullptr));
        check((std::strstr(output.data, "ENABLE_SCRIPT_ENGINE=OFF") != nullptr));
        praktor_release_json(&output);
    }

    std::filesystem::remove_all(dir);
}

spec("core-only ABI executes command workflow and rejects script workflow") {
    it("complete case") { test_core_only_abi_executes_command_workflow_and_rejects_script_workflow_2(); }
}


[[maybe_unused]] static void test_core_only_abi_honors_pre_start_cancellation_3() {
    const auto dir = createApiTempDir();
    const auto workflow = dir / "cancelled.yml";
    writeText(workflow, R"(
tasks:
  - name: must_not_run
    command: "echo should-not-run"
)");

    static const char input[] = "{}";
    const std::string stable_path = workflow.string();
    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;

    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.is_cancelled = &alwaysCancelled;

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_execute_workflow_controlled(
                &request, &control, &output, &error) ==
            PRAKTOR_RESULT_CANCELLED));
    check((output.data != nullptr));
    check((std::strstr(output.data, "\"workflow_status\":\"cancelled\"") != nullptr));
    check((std::string(error.message) == "Workflow execution cancelled"));
    praktor_release_json(&output);

    std::filesystem::remove_all(dir);
}

spec("core-only ABI honors pre-start cancellation") {
    it("complete case") { test_core_only_abi_honors_pre_start_cancellation_3(); }
}
