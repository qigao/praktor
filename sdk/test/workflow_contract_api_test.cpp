#include "praktor_test.hpp"
#include "praktor.h"
#include "data/workflow_value.hpp"


#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

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
    check((out.is_open()));
    out << content;
}

WorkflowValue parseOwned(const praktor_owned_json& value) {
    check((value.data != nullptr));
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

[[maybe_unused]] static void test_workflowplan_publishes_generated_input_and_output_schemas_1() {
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow, contractWorkflow());

    const std::string stable_path = workflow.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();

    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_compile_workflow(&request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((plan != nullptr));

    praktor_owned_json description = PRAKTOR_OWNED_JSON_INIT;
    check((praktor_describe_workflow_plan(plan, &description, &error) ==
            PRAKTOR_RESULT_SUCCESS));

    const auto value = parseOwned(description);
    const auto input_schema = value.at("input_schema");
    const auto output_schema = value.at("output_schema");

    check((input_schema.at("type").as<std::string>() == "object"));
    check((!(input_schema.at("additionalProperties").as<bool>())));
    check((input_schema.at("properties").at("name").at("type").as<std::string>() ==
          "string"));
    check((input_schema.at("properties").at("count").at("default").as<int>() == 2));
    check((input_schema.at("properties").at("mode").at("enum").is_array()));
    check((input_schema.at("properties").at("tags").at("type").as<std::string>() ==
          "array"));
    check((input_schema.at("properties").at("payload").at("type").as<std::string>() ==
          "object"));
    check((output_schema.at("properties").at("result").at("type").as<std::string>() ==
          "string"));

    praktor_release_json(&description);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("WorkflowPlan publishes generated input and output schemas") {
    it("complete case") { test_workflowplan_publishes_generated_input_and_output_schemas_1(); }
}

[[maybe_unused]] static void test_workflow_input_contracts_fail_before_task_execution_2(int section_index) {
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow, contractWorkflow());

    const std::string valid =
        R"({"name":"Ada","tags":["ci"],"payload":{"branch":"main"}})";

    if (section_index == 0) {
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        check((executeContract(workflow, valid, output, error) ==
                PRAKTOR_RESULT_SUCCESS));
        const auto result = parseOwned(output);
        check((result.at("workflow_status").as<std::string>() == "success"));
        check((result.at("outputs").is_object()));
        check((result.at("outputs").at("result").is_string()));
        praktor_release_json(&output);
    }

    const std::vector<std::string> invalid = {
        R"({"name":7,"tags":[],"payload":{}})",
        R"({"name":"Ada","tags":[]})",
        R"({"name":"Ada","tags":[],"payload":{},"mode":"slow"})",
        R"({"name":"Ada","tags":[],"payload":{},"extra":true})"
    };
    if (section_index > 0) {
        const auto& input = invalid.at(static_cast<std::size_t>(section_index - 1));
        praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
        praktor_error error = PRAKTOR_ERROR_INIT;
        check((executeContract(workflow, input, output, error) ==
              PRAKTOR_RESULT_INPUT_CONTRACT));
        check((error.phase == PRAKTOR_ERROR_PHASE_INPUT_CONTRACT));
        check((error.message[0] != '\0'));
        check((output.data == nullptr));
        check((output.size == 0));
    }

    std::filesystem::remove_all(dir);
}

spec("workflow input contracts fail before task execution") {
    it("valid typed inputs execute and defaults are accepted") { test_workflow_input_contracts_fail_before_task_execution_2(0); }
    for (int input = 1; input <= 4; ++input) {
        it("invalid input %d", input) { test_workflow_input_contracts_fail_before_task_execution_2(input); }
    }
}

[[maybe_unused]] static void test_empty_input_contracts_enforce_their_published_additionalproperties_policy_3(std::size_t row_0, std::size_t row_1, std::size_t row_2, std::size_t row_3) {
    const std::string policy = std::array{"strict", "allow_extra"}.at(row_0);
    const bool explicit_empty = std::array{false, true}.at(row_1);
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow,
        "input_policy: " + policy + "\n" +
        (explicit_empty ? "inputs: {}\n" : "") +
        "tasks:\n  - name: emit\n    command: echo contract-ok\n");

    const std::string path = workflow.string();
    praktor_compile_request compile = PRAKTOR_COMPILE_REQUEST_INIT;
    compile.workflow_path = path.c_str();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_compile_workflow(&compile, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS));

    praktor_owned_json description = PRAKTOR_OWNED_JSON_INIT;
    check((praktor_describe_workflow_plan(plan, &description, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    const auto schema = parseOwned(description).at("input_schema");
    check((schema.at("properties").size() == 0));
    check((schema.at("additionalProperties").as<bool>() ==
          (policy == "allow_extra")));
    praktor_release_json(&description);

    const std::string input = std::array{std::string("{}"), std::string(R"({"extra":true})")}.at(row_2);
    const bool rejects = policy == "strict" && input != "{}";
    const bool reviewed = std::array{false, true}.at(row_3);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_result status;
    if (reviewed) {
        praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
        request.plan = plan;
        request.input_json = input.data();
        request.input_json_size = input.size();
        status = praktor_execute_workflow_plan(&request, nullptr, &output, &error);
    } else {
        status = executeContract(workflow, input, output, error);
    }
    check((status == (rejects ? PRAKTOR_RESULT_INPUT_CONTRACT
                            : PRAKTOR_RESULT_SUCCESS)));
    if (rejects) {
        check((error.phase == PRAKTOR_ERROR_PHASE_INPUT_CONTRACT));
        check((std::string(error.message).find("extra") != std::string::npos));
        check((output.data == nullptr));
        check((output.size == 0));
    } else {
        check((parseOwned(output).at("workflow_status").as<std::string>() == "success"));
    }
    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("empty input contracts enforce their published additionalProperties policy") {
    for (std::size_t row_0 = 0; row_0 < 2; ++row_0) {
        for (std::size_t row_1 = 0; row_1 < 2; ++row_1) {
            for (std::size_t row_2 = 0; row_2 < 2; ++row_2) {
                for (std::size_t row_3 = 0; row_3 < 2; ++row_3) {
                    it("%s / row %zu %zu %zu %zu", "complete case", row_0, row_1, row_2, row_3) { test_empty_input_contracts_enforce_their_published_additionalproperties_policy_3(row_0, row_1, row_2, row_3); }
                }
            }
        }
    }
}

[[maybe_unused]] static void test_output_contracts_interpolate_composite_templates_and_preserve_single_path_types_4() {
    const auto dir = contractTempDir();
    const auto workflow = dir / "workflow.yml";
    writeContractFile(workflow, R"(
input_policy: strict
inputs:
  first:
    type: string
    required: true
  second:
    type: string
    required: true
  count:
    type: integer
    required: true
  tags:
    type: array
    required: true
  payload:
    type: object
    required: true
outputs:
  joined:
    type: string
    required: true
    value: "{{ variables.first }} + {{ variables.second }}"
  spaced:
    type: string
    required: true
    value: "  {{ variables.first }}:{{ variables.second }}  "
  count:
    type: integer
    required: true
    value: "  {{ variables.count }}  "
  tags:
    type: array
    required: true
    value: "{{ variables.tags }}"
  payload:
    type: object
    required: true
    value: "{{ variables.payload }}"
tasks:
  - name: emit
    command: echo contract-ok
)");

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((executeContract(workflow,
        R"({"first":"alpha","second":"beta","count":7,"tags":["ci"],"payload":{"branch":"main"}})",
        output, error) == PRAKTOR_RESULT_SUCCESS));
    const auto result = parseOwned(output);
    for (const auto& outputs : {result.at("outputs"),
                                result.at("agent_output").at("outputs")}) {
        check((outputs.at("joined").as<std::string>() == "alpha + beta"));
        check((outputs.at("spaced").as<std::string>() == "  alpha:beta  "));
        check(((outputs.at("count").is_int64() || outputs.at("count").is_uint64())));
        check((outputs.at("count").as<int>() == 7));
        check((outputs.at("tags").is_array()));
        check((outputs.at("tags").at(0).as<std::string>() == "ci"));
        check((outputs.at("payload").is_object()));
        check((outputs.at("payload").at("branch").as<std::string>() == "main"));
    }
    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

spec("output contracts interpolate composite templates and preserve single-path types") {
    it("complete case") { test_output_contracts_interpolate_composite_templates_and_preserve_single_path_types_4(); }
}
