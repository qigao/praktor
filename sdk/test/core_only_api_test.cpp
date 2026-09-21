#include "praktor.h"

#include <catch2/catch_test_macros.hpp>

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
    REQUIRE(out.is_open());
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

} // namespace

TEST_CASE("core-only ABI advertises JSON workflows without script capability") {
    const praktor_api* api = praktor_get_api();

    REQUIRE(api != nullptr);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_JSON_WORKFLOW) != 0);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_SCRIPT_ENGINE) == 0);
}

TEST_CASE("core-only ABI executes command workflow and rejects script workflow") {
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

        CHECK(executeFile(command_workflow, output, error) == PRAKTOR_RESULT_SUCCESS);
        REQUIRE(output.data != nullptr);
        CHECK(std::strstr(output.data, "\"workflow_status\":\"success\"") != nullptr);
        praktor_release_json(&output);
    }

    {
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;

        CHECK(executeFile(script_workflow, output, error) == PRAKTOR_RESULT_EXECUTION_FAILED);
        REQUIRE(output.data != nullptr);
        CHECK(std::strstr(output.data, "\"workflow_status\":\"failed\"") != nullptr);
        CHECK(std::strstr(output.data, "ENABLE_SCRIPT_ENGINE=OFF") != nullptr);
        praktor_release_json(&output);
    }

    std::filesystem::remove_all(dir);
}
