#include "praktor_test.hpp"
#include "praktor.h"

#ifndef PRAKTOR_C_API
#error "praktor.h must expose the Praktor-owned C ABI marker"
#endif

#ifdef PRAKTOR_BUILD_SHARED
#error "Praktor consumers must not inherit the producer export marker"
#endif

#ifdef PRAKTOR_USE_SHARED
#error "Praktor consumers import by default and must not require a consumer marker"
#endif

#include "data/workflow_value.hpp"


#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::filesystem::path createTempDir() {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    const auto dir = std::filesystem::temp_directory_path() /
        ("praktor_sdk_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

praktor_execute_request makeRequest(const std::string& workflow_path_storage,
                                    const std::string& input) {
    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = workflow_path_storage.c_str();
    request.input_json = input.data();
    request.input_json_size = input.size();
    return request;
}

#ifdef _WIN32
constexpr const char* kSleepCommand =
    "waitfor SomethingThatNeverHappens /T 1 >nul 2>nul & exit /b 0";
constexpr long long kSequentialMinMs = 1800;
#else
constexpr const char* kSleepCommand = "sleep 0.5";
constexpr long long kSequentialMinMs = 900;
#endif

struct CancellationProbe {
    std::atomic<int> requested{0};
};

int32_t PRAKTOR_CALL cancellationProbe(void* user_data) {
    auto* probe = static_cast<CancellationProbe*>(user_data);
    return probe && probe->requested.load(std::memory_order_acquire) != 0 ? 1 : 0;
}

} // namespace

[[maybe_unused]] static void test_praktor_c_api_publishes_json_workflow_execution_and_ownership_1() {
    const praktor_api* api = praktor_get_api();

    check((api != nullptr));
    check((api->struct_size >= sizeof(praktor_api)));
    check((api->abi_major == PRAKTOR_ABI_MAJOR));
    check((api->abi_minor >= PRAKTOR_ABI_MINOR));
    check(((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) != 0));
    check(((api->capabilities & PRAKTOR_CAPABILITY_SCRIPT_ENGINE) != 0));
    check(((api->capabilities & PRAKTOR_CAPABILITY_EXECUTION_CONTROL) != 0));
    check((api->execute_workflow != nullptr));
    check((api->release_json != nullptr));
    check((api->execute_workflow_controlled != nullptr));

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((api->execute_workflow(nullptr, &output, &error) == PRAKTOR_RESULT_INVALID_ARGUMENT));
    check((error.phase == PRAKTOR_ERROR_PHASE_REQUEST));
}

spec("Praktor C API publishes JSON workflow execution and ownership") {
    it("complete case") { test_praktor_c_api_publishes_json_workflow_execution_and_ownership_1(); }
}

[[maybe_unused]] static void test_praktor_c_api_preserves_sequential_runner_defaults_2() {
    const auto dir = createTempDir();
    const auto workflow_path = dir / "workflow.yml";
    const std::string workflow =
        "tasks:\n"
        "  - name: first\n"
        "    command: \"" + std::string(kSleepCommand) + "\"\n"
        "\n"
        "  - name: second\n"
        "    command: \"" + std::string(kSleepCommand) + "\"\n"
        "\n"
        "  - name: done\n"
        "    depends_on: [first, second]\n"
        "    command: \"echo done\"\n";
    writeFile(workflow_path, workflow);

    const std::string input = "{}";
    const std::string workflow_path_storage = workflow_path.string();
    const auto request = makeRequest(workflow_path_storage, input);

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    const auto start = std::chrono::steady_clock::now();
    const auto status = praktor_execute_workflow(&request, &output, &error);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    info("%s", praktor_test::message([&](std::ostream& out) { out << error.message; }).c_str());
    check((status == PRAKTOR_RESULT_SUCCESS));
    check((duration >= kSequentialMinMs));
    check((output.data != nullptr));
    check((output.size > 0));
    praktor_release_json(&output);
    check((output.data == nullptr));
    check((output.size == 0));
    std::filesystem::remove_all(dir);
}

spec("Praktor C API preserves sequential runner defaults") {
    it("complete case") { test_praktor_c_api_preserves_sequential_runner_defaults_2(); }
}

[[maybe_unused]] static void test_praktor_c_api_preserves_heterogeneous_json_inputs_and_outputs_3() {
    const auto dir = createTempDir();
    const auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, R"(
tasks:
  - name: inspect
    script: |
      if (ctx.get("payload.count") != 7) fail("nested count was not preserved");
      ctx.output("count", ctx.get("payload.count"));
      ctx.output("enabled", ctx.get("payload.enabled"));
      ctx.output("payload", "{\"status\":\"completed\",\"items\":[1,true,null]}");
)" );

    const std::string input =
        R"({"payload":{"count":7,"enabled":true},"values":[1,"two",false,null]})";
    const std::string workflow_path_storage = workflow_path.string();
    const auto request = makeRequest(workflow_path_storage, input);

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    const auto execution_status = praktor_execute_workflow(&request, &output, &error);
    info("%s", praktor_test::message([&](std::ostream& out) { out << error.message; }).c_str());
    check((execution_status == PRAKTOR_RESULT_SUCCESS));
    check((output.data != nullptr));

    const auto result = WorkflowValue::parse(std::string_view(output.data, output.size));
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.pretty_string(); }).c_str());
    check((result.at("workflow_status").as<std::string>() == "success"));
    const auto inspect = result.at("tasks").at("inspect");
    check((inspect.at("status").as<std::string>() == "success"));
    const auto outputs = inspect.at("outputs");
    check((outputs.at("count").as<int>() == 7));
    check((outputs.at("enabled").as<bool>()));
    const auto payload = outputs.at("payload");
    check((payload.at("status").as<std::string>() == "completed"));
    check((payload.at("items").is_array()));
    check((payload.at("items").size() == 3));
    check((payload.at("items").at(1).as<bool>()));
    check((payload.at("items").at(2).is_null()));

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

spec("Praktor C API preserves heterogeneous JSON inputs and outputs") {
    it("complete case") { test_praktor_c_api_preserves_heterogeneous_json_inputs_and_outputs_3(); }
}

[[maybe_unused]] static void test_praktor_c_api_rejects_malformed_json_and_non_object_roots_4() {
    const auto dir = createTempDir();
    const auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, "tasks: []\n");
    const std::string workflow_path_storage = workflow_path.string();

    for (const std::string input : {std::string("{"), std::string("[]")}) {
         {
            const auto request = makeRequest(workflow_path_storage, input);
            praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
            praktor_error error = PRAKTOR_ERROR_INIT;
            check((praktor_execute_workflow(&request, &output, &error) ==
                  PRAKTOR_RESULT_INVALID_JSON));
            check((error.phase == PRAKTOR_ERROR_PHASE_INPUT_JSON));
            check((error.message[0] != '\0'));
            check((output.data == nullptr));
            check((output.size == 0));
        }
    }

    std::filesystem::remove_all(dir);
}

spec("Praktor C API rejects malformed JSON and non-object roots") {
    it("complete case") { test_praktor_c_api_rejects_malformed_json_and_non_object_roots_4(); }
}

[[maybe_unused]] static void test_praktor_c_api_returns_canonical_json_for_workflow_failure_5() {
    const auto dir = createTempDir();
    const auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, R"(
tasks:
  - name: before_failure
    script: |
      ctx.output("ready", true);
  - name: fail
    depends_on: [before_failure]
    script: |
      fail("expected failure");
)" );

    const std::string input = "{}";
    const std::string workflow_path_storage = workflow_path.string();
    const auto request = makeRequest(workflow_path_storage, input);

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_execute_workflow(&request, &output, &error) ==
            PRAKTOR_RESULT_EXECUTION_FAILED));
    check((output.data != nullptr));
    const auto result = WorkflowValue::parse(std::string_view(output.data, output.size));
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.pretty_string(); }).c_str());
    check((result.at("workflow_status").as<std::string>() == "failed"));
    const auto before_failure = result.at("tasks").at("before_failure");
    check((before_failure.at("outputs").at("ready").as<bool>()));
    check((result.at("error").as<std::string>() == "Workflow execution failed"));
    check((error.phase == PRAKTOR_ERROR_PHASE_EXECUTION));
    check((std::string(error.message) == "Workflow execution failed"));

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

spec("Praktor C API returns canonical JSON for workflow failure") {
    it("complete case") { test_praktor_c_api_returns_canonical_json_for_workflow_failure_5(); }
}


[[maybe_unused]] static void test_praktor_c_api_supports_cooperative_cancellation_and_deadlines_6(int section_index) {
    using namespace std::chrono_literals;

    const auto dir = createTempDir();
    const auto workflow_path = dir / "controlled.yml";
    writeFile(workflow_path, R"(
tasks:
  - name: controlled
    script: |
      i = 0;
      while (i < 1000000000) {
        i = i + 1;
      }
      ctx.output("completed", true);
)" );

    const std::string input = "{}";
    const std::string workflow_path_storage = workflow_path.string();
    const auto request = makeRequest(workflow_path_storage, input);

    if (section_index == 0) {
        CancellationProbe probe;
        probe.requested.store(1, std::memory_order_release);
        praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
        control.is_cancelled = &cancellationProbe;
        control.user_data = &probe;

        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        const auto status =
            praktor_execute_workflow_controlled(&request, &control, &output, &error);

        check((status == PRAKTOR_RESULT_CANCELLED));
        check((output.data != nullptr));
        const auto result =
            WorkflowValue::parse(std::string_view(output.data, output.size));
        check((result.at("workflow_status").as<std::string>() == "cancelled"));
        check((std::string(error.message) == "Workflow execution cancelled"));
        praktor_release_json(&output);
    }

    if (section_index == 1) {
        CancellationProbe probe;
        praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
        control.is_cancelled = &cancellationProbe;
        control.user_data = &probe;

        std::thread canceller([&probe]() {
            std::this_thread::sleep_for(50ms);
            probe.requested.store(1, std::memory_order_release);
        });

        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        const auto started = std::chrono::steady_clock::now();
        const auto status =
            praktor_execute_workflow_controlled(&request, &control, &output, &error);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        canceller.join();

        check((status == PRAKTOR_RESULT_CANCELLED));
        check((elapsed < 5s));
        check((output.data != nullptr));
        const auto result =
            WorkflowValue::parse(std::string_view(output.data, output.size));
        check((result.at("workflow_status").as<std::string>() == "cancelled"));
        praktor_release_json(&output);
    }

    if (section_index == 2) {
        praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
        control.timeout_ms = 30;

        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        const auto status =
            praktor_execute_workflow_controlled(&request, &control, &output, &error);

        check((status == PRAKTOR_RESULT_TIMED_OUT));
        check((output.data != nullptr));
        const auto result =
            WorkflowValue::parse(std::string_view(output.data, output.size));
        check((result.at("workflow_status").as<std::string>() == "timed_out"));
        check((std::string(error.message) == "Workflow execution timed out"));
        praktor_release_json(&output);
    }

    std::filesystem::remove_all(dir);
}

spec("Praktor C API supports cooperative cancellation and deadlines") {
    it("pre-cancel returns canonical cancelled result") { test_praktor_c_api_supports_cooperative_cancellation_and_deadlines_6(0); }
    it("running JIT script observes host cancellation probe") { test_praktor_c_api_supports_cooperative_cancellation_and_deadlines_6(1); }
    it("relative deadline returns timed out distinctly") { test_praktor_c_api_supports_cooperative_cancellation_and_deadlines_6(2); }
}
