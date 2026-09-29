#include "praktor.h"
#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path contractTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-contract-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeContractFile(const std::filesystem::path& path,
                       const std::string& content) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << content;
}

WorkflowValue parseOwned(const praktor_owned_json& value) {
    REQUIRE(value.data != nullptr);
    return WorkflowValue::parse(std::string_view(value.data, value.size));
}

praktor_result executeContract(const std::filesystem::path& workflow,
                               const std::string& input,
                               praktor_owned_json& output,
                               praktor_error& error) {
    const std::string stable_path = workflow.string();
    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    request.input_json = input.data();
    request.input_json_size = input.size();
    return praktor_execute_workflow(&request, &output, &error);
}

const char* contractWorkflow() {
    return R"(
name: typed_contract
description: Typed harness contract
input_policy: strict

inputs:
  name:
    type: string
    required: true
    description: Display name
  count:
    type: integer
    default: 2
  mode:
    type: string
    enum: [fast, safe]
    default: fast
  tags:
    type: array
    required: true
  payload:
    type: object
    required: true

outputs:
  result:
    type: string
    required: true
    value: "{{ tasks.emit.outputs.stdout }}"

tasks:
  - name: emit
    command: "echo contract-ok"
)";
}

} // namespace

TEST_CASE("WorkflowPlan publishes generated input and output schemas",
          "[sdk][contract][plan]") {
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow, contractWorkflow());

    const std::string stable_path = workflow.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();

    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(praktor_compile_workflow(&request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(plan != nullptr);

    praktor_owned_json description = PRAKTOR_OWNED_JSON_INIT;
    REQUIRE(praktor_describe_workflow_plan(plan, &description, &error) ==
            PRAKTOR_RESULT_SUCCESS);

    const auto value = parseOwned(description);
    const auto input_schema = value.at("input_schema");
    const auto output_schema = value.at("output_schema");

    CHECK(input_schema.at("type").as<std::string>() == "object");
    CHECK_FALSE(input_schema.at("additionalProperties").as<bool>());
    CHECK(input_schema.at("properties").at("name").at("type").as<std::string>() ==
          "string");
    CHECK(input_schema.at("properties").at("count").at("default").as<int>() == 2);
    REQUIRE(input_schema.at("properties").at("mode").at("enum").is_array());
    CHECK(input_schema.at("properties").at("tags").at("type").as<std::string>() ==
          "array");
    CHECK(input_schema.at("properties").at("payload").at("type").as<std::string>() ==
          "object");
    CHECK(output_schema.at("properties").at("result").at("type").as<std::string>() ==
          "string");

    praktor_release_json(&description);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("workflow input contracts fail before task execution",
          "[sdk][contract][input]") {
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow, contractWorkflow());

    const std::string valid =
        R"({"name":"Ada","tags":["ci"],"payload":{"branch":"main"}})";

    SECTION("valid typed inputs execute and defaults are accepted") {
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        REQUIRE(executeContract(workflow, valid, output, error) ==
                PRAKTOR_RESULT_SUCCESS);
        const auto result = parseOwned(output);
        CHECK(result.at("workflow_status").as<std::string>() == "success");
        REQUIRE(result.at("outputs").is_object());
        CHECK(result.at("outputs").at("result").is_string());
        praktor_release_json(&output);
    }

    const std::vector<std::string> invalid = {
        R"({"name":7,"tags":[],"payload":{}})",
        R"({"name":"Ada","tags":[]})",
        R"({"name":"Ada","tags":[],"payload":{},"mode":"slow"})",
        R"({"name":"Ada","tags":[],"payload":{},"extra":true})"
    };
    for (const auto& input : invalid) {
        DYNAMIC_SECTION("rejects " << input) {
            praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
            praktor_error error = PRAKTOR_ERROR_INIT;
            CHECK(executeContract(workflow, input, output, error) ==
                  PRAKTOR_RESULT_INPUT_CONTRACT);
            CHECK(error.phase == PRAKTOR_ERROR_PHASE_INPUT_CONTRACT);
            CHECK(error.message[0] != '\0');
            CHECK(output.data == nullptr);
            CHECK(output.size == 0);
        }
    }

    std::filesystem::remove_all(dir);
}
