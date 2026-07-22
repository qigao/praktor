#include "praktor.h"
 

#include <catch2/catch_all.hpp>
#include "data/workflow_value.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_pistol_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

#ifdef _WIN32
constexpr const char* kSleepCommand = "waitfor SomethingThatNeverHappens /T 1 >nul 2>nul & exit /b 0";
constexpr long long kSequentialMinMs = 1800;
#else
constexpr const char* kSleepCommand = "sleep 0.5";
constexpr long long kSequentialMinMs = 900;
#endif

} // namespace

TEST_CASE("pistol API publishes DataBind execution and ownership", "[pistol][abi]") {
    const praktor_api* api = praktor_get_api();

    REQUIRE(api != nullptr);
    REQUIRE(api->struct_size >= sizeof(praktor_api));
    CHECK(api->abi_major == PRAKTOR_ABI_MAJOR);
    CHECK(api->abi_minor >= PRAKTOR_ABI_MINOR);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_DATA_BIND) != 0);
    REQUIRE(api->execute_workflow != nullptr);
    REQUIRE(api->release_data != nullptr);

    praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    CHECK(api->execute_workflow(nullptr, &output, &error) == PRAKTOR_RESULT_INVALID_ARGUMENT);
}

TEST_CASE("pistol API preserves sequential runner defaults", "[pistol]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
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

    const std::string schema =
        "message WorkflowInput { }\n"
        "message WorkflowResult { string workflow_status; optional string error; }\n";
    const std::string input = "{}";
    const std::string workflow_path_storage = workflow_path.string();

    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = workflow_path_storage.c_str();
    request.input_data = input.data();
    request.input_size = input.size();
    request.schema_text = schema.data();
    request.schema_text_size = schema.size();
    request.input_type = "WorkflowInput";
    request.output_type = "WorkflowResult";

    praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    const auto start = std::chrono::steady_clock::now();
    const auto status = praktor_execute_workflow(&request, &output, &error);
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    INFO(error.message);
    REQUIRE(status == PRAKTOR_RESULT_SUCCESS);
    CHECK(duration >= kSequentialMinMs);
    praktor_release_data(&output);
    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API preserves structured DataBind input and task output", "[pistol][databind]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, R"(
tasks:
  - name: inspect
    script: |
      ctx.output("count", ctx.get("payload.count"));
)" );

    const std::string schema =
        "composite Payload { int32 count; bool enabled; }\n"
        "message WorkflowInput { Payload payload; }\n"
        "message TaskSummary { string status; map<string,int32> outputs; }\n"
        "message WorkflowResult { string workflow_status; "
        "map<string,TaskSummary> tasks; optional string error; }\n";
    const std::string input = R"({"payload":{"count":7,"enabled":true}})";
    const std::string workflow_path_storage = workflow_path.string();

    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = workflow_path_storage.c_str();
    request.input_data = input.data();
    request.input_size = input.size();
    request.input_format = PRAKTOR_DATA_FORMAT_JSON;
    request.schema_text = schema.data();
    request.schema_text_size = schema.size();
    request.input_type = "WorkflowInput";
    request.output_type = "WorkflowResult";
    request.output_format = PRAKTOR_DATA_FORMAT_JSON;

    praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    const auto execution_status = praktor_execute_workflow(&request, &output, &error);
    INFO(error.message);
    REQUIRE(execution_status == PRAKTOR_RESULT_SUCCESS);
    REQUIRE(output.data != nullptr);

    const auto result = WorkflowValue::parse(
        std::string_view(static_cast<const char*>(output.data), output.size));
    CHECK(result["workflow_status"].as<std::string>() == "success");
    CHECK(result["tasks"]["inspect"]["status"].as<std::string>() == "success");
    CHECK(result["tasks"]["inspect"]["outputs"]["count"].as<int>() == 7);

    praktor_release_data(&output);
    CHECK(output.data == nullptr);
    CHECK(output.size == 0);
    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API binds and serializes every DataBind format", "[pistol][databind]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "out.txt";
    writeFile(workflow_path, R"(
tasks:
  - name: write
    working_dir: .
    command: "echo {{ NAME }} > out.txt"
)" );

    const std::string schema =
        "message WorkflowInput { string NAME; }\n"
        "message TextWorkflowResult { string workflow_status; optional string error; }\n"
        "message BinaryWorkflowResult { string workflow_status; }\n";
    const auto schema_path = dir / "workflow.schema";
    writeFile(schema_path, schema);
    const std::string schema_path_storage = schema_path.string();
    const std::string workflow_path_storage = workflow_path.string();

    struct FormatCase {
        praktor_data_format format;
        std::vector<uint8_t> input;
        std::string expected;
    };
    const auto bytes = [](std::string_view value) {
        return std::vector<uint8_t>(value.begin(), value.end());
    };
    std::vector<FormatCase> cases = {
        {PRAKTOR_DATA_FORMAT_JSON, bytes(R"({"NAME":"json"})"), "json"},
        {PRAKTOR_DATA_FORMAT_YAML, bytes("NAME: yaml\n"), "yaml"},
        {PRAKTOR_DATA_FORMAT_XML,
         bytes("<WorkflowInput><NAME>xml</NAME></WorkflowInput>"), "xml"},
        {PRAKTOR_DATA_FORMAT_CSV, bytes("NAME\r\ncsv\r\n"), "csv"},
        {PRAKTOR_DATA_FORMAT_BINARY,
         {6, 0, 0, 0, 'b', 'i', 'n', 'a', 'r', 'y'}, "binary"},
    };

    for (const auto& item : cases) {
        DYNAMIC_SECTION("format " << static_cast<int>(item.format)) {
            praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
            request.workflow_path = workflow_path_storage.c_str();
            request.input_data = item.input.data();
            request.input_size = item.input.size();
            request.input_format = item.format;
            if (item.format == PRAKTOR_DATA_FORMAT_JSON) {
                request.schema_path = schema_path_storage.c_str();
            } else {
                request.schema_text = schema.data();
                request.schema_text_size = schema.size();
            }
            request.input_type = "WorkflowInput";
            request.output_type = item.format == PRAKTOR_DATA_FORMAT_BINARY
                ? "BinaryWorkflowResult"
                : "TextWorkflowResult";
            request.output_format = item.format;

            praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
            praktor_error error = PRAKTOR_ERROR_INIT;
            REQUIRE(praktor_execute_workflow(&request, &output, &error)
                    == PRAKTOR_RESULT_SUCCESS);
            REQUIRE(output.data != nullptr);
            CHECK(output.size > 0);

            std::ifstream file(output_path);
            std::string content;
            std::getline(file, content);
            CHECK(content.find(item.expected) != std::string::npos);
            praktor_release_data(&output);
        }
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API reports schema and data failures", "[pistol][databind]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, "tasks: []\n");
    const std::string workflow_path_storage = workflow_path.string();
    const std::string schema =
        "message WorkflowInput { string NAME; }\n"
        "message WorkflowResult { string workflow_status; optional string error; }\n";
    const std::string invalid_input = "{";

    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = workflow_path_storage.c_str();
    request.input_data = invalid_input.data();
    request.input_size = invalid_input.size();
    request.input_format = PRAKTOR_DATA_FORMAT_JSON;
    request.schema_text = schema.data();
    request.schema_text_size = schema.size();
    request.input_type = "WorkflowInput";
    request.output_type = "WorkflowResult";
    request.output_format = PRAKTOR_DATA_FORMAT_JSON;

    praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    CHECK(praktor_execute_workflow(&request, &output, &error)
          == PRAKTOR_RESULT_INVALID_DATA);
    CHECK(error.data_bind_status != 0);
    CHECK(output.data == nullptr);

    request.input_type = "MissingType";
    CHECK(praktor_execute_workflow(&request, &output, &error)
          == PRAKTOR_RESULT_TYPE_NOT_FOUND);

    request.input_type = "WorkflowInput";
    request.schema_path = "also-present.schema";
    request.schema_text = schema.data();
    request.schema_text_size = schema.size();
    CHECK(praktor_execute_workflow(&request, &output, &error)
          == PRAKTOR_RESULT_INVALID_ARGUMENT);

    request.schema_path = nullptr;
    request.schema_text = nullptr;
    request.schema_text_size = 0;
    CHECK(praktor_execute_workflow(&request, &output, &error)
          == PRAKTOR_RESULT_INVALID_ARGUMENT);

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API returns an owned result for workflow failure", "[pistol][databind]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    writeFile(workflow_path, R"(
tasks:
  - name: fail
    script: |
      fail("expected failure");
)" );

    const std::string workflow_path_storage = workflow_path.string();
    const std::string schema =
        "message WorkflowInput { }\n"
        "message WorkflowResult { string workflow_status; optional string error; }\n";
    const std::string input = "{}";

    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = workflow_path_storage.c_str();
    request.input_data = input.data();
    request.input_size = input.size();
    request.input_format = PRAKTOR_DATA_FORMAT_JSON;
    request.schema_text = schema.data();
    request.schema_text_size = schema.size();
    request.input_type = "WorkflowInput";
    request.output_type = "WorkflowResult";
    request.output_format = PRAKTOR_DATA_FORMAT_JSON;

    praktor_owned_data output = PRAKTOR_OWNED_DATA_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(praktor_execute_workflow(&request, &output, &error)
            == PRAKTOR_RESULT_EXECUTION_FAILED);
    REQUIRE(output.data != nullptr);
    const auto result = WorkflowValue::parse(
        std::string_view(static_cast<const char*>(output.data), output.size));
    CHECK(result["workflow_status"].as<std::string>() == "failed");
    CHECK(result["error"].as<std::string>() == "Workflow execution failed");
    CHECK(std::string(error.message) == "Workflow execution failed");

    praktor_release_data(&output);
    std::filesystem::remove_all(dir);
}
