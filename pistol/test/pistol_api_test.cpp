#include "praktor.h"

#ifndef PRAKTOR_C_API
#error "praktor.h must expose the Praktor-owned C ABI marker"
#endif

#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace {

std::filesystem::path createTempDir() {
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    const auto dir = std::filesystem::temp_directory_path() /
        ("praktor_pistol_" + std::to_string(rng()));
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

} // namespace

TEST_CASE("pistol API publishes JSON workflow execution and ownership", "[pistol][abi]") {
    const praktor_api* api = praktor_get_api();

    REQUIRE(api != nullptr);
    REQUIRE(api->struct_size >= sizeof(praktor_api));
    CHECK(api->abi_major == PRAKTOR_ABI_MAJOR);
    CHECK(api->abi_minor >= PRAKTOR_ABI_MINOR);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) != 0);
    REQUIRE(api->execute_workflow != nullptr);
    REQUIRE(api->release_json != nullptr);

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    CHECK(api->execute_workflow(nullptr, &output, &error) == PRAKTOR_RESULT_INVALID_ARGUMENT);
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_REQUEST);
}

TEST_CASE("pistol API preserves sequential runner defaults", "[pistol]") {
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

    INFO(error.message);
    REQUIRE(status == PRAKTOR_RESULT_SUCCESS);
    CHECK(duration >= kSequentialMinMs);
    REQUIRE(output.data != nullptr);
    CHECK(output.size > 0);
    praktor_release_json(&output);
    CHECK(output.data == nullptr);
    CHECK(output.size == 0);
    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API preserves heterogeneous JSON inputs and outputs", "[pistol][json]") {
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
    INFO(error.message);
    REQUIRE(execution_status == PRAKTOR_RESULT_SUCCESS);
    REQUIRE(output.data != nullptr);

    const auto result = WorkflowValue::parse(std::string_view(output.data, output.size));
    INFO(result.pretty_string());
    CHECK(result.at("workflow_status").as<std::string>() == "success");
    const auto inspect = result.at("tasks").at("inspect");
    CHECK(inspect.at("status").as<std::string>() == "success");
    const auto outputs = inspect.at("outputs");
    CHECK(outputs.at("count").as<int>() == 7);
    CHECK(outputs.at("enabled").as<bool>());
    const auto payload = outputs.at("payload");
    CHECK(payload.at("status").as<std::string>() == "completed");
    REQUIRE(payload.at("items").is_array());
    CHECK(payload.at("items").size() == 3);
    CHECK(payload.at("items").at(1).as<bool>());
    CHECK(payload.at("items").at(2).is_null());

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API rejects malformed JSON and non-object roots", "[pistol][json]") {
    const auto dir = createTempDir();
    const auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, "tasks: []\n");
    const std::string workflow_path_storage = workflow_path.string();

    for (const std::string input : {std::string("{"), std::string("[]")}) {
        DYNAMIC_SECTION("input " << input) {
            const auto request = makeRequest(workflow_path_storage, input);
            praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
            praktor_error error = PRAKTOR_ERROR_INIT;
            CHECK(praktor_execute_workflow(&request, &output, &error) ==
                  PRAKTOR_RESULT_INVALID_JSON);
            CHECK(error.phase == PRAKTOR_ERROR_PHASE_INPUT_JSON);
            CHECK(error.message[0] != '\0');
            CHECK(output.data == nullptr);
            CHECK(output.size == 0);
        }
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API returns canonical JSON for workflow failure", "[pistol][json]") {
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
    REQUIRE(praktor_execute_workflow(&request, &output, &error) ==
            PRAKTOR_RESULT_EXECUTION_FAILED);
    REQUIRE(output.data != nullptr);
    const auto result = WorkflowValue::parse(std::string_view(output.data, output.size));
    INFO(result.pretty_string());
    CHECK(result.at("workflow_status").as<std::string>() == "failed");
    const auto before_failure = result.at("tasks").at("before_failure");
    CHECK(before_failure.at("outputs").at("ready").as<bool>());
    CHECK(result.at("error").as<std::string>() == "Workflow execution failed");
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_EXECUTION);
    CHECK(std::string(error.message) == "Workflow execution failed");

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}
