#include "praktor.h"

#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path createPlanTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-plan-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writePlanFile(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

praktor_workflow_plan* compilePlan(const std::filesystem::path& path,
                                   praktor_error& error) {
    const std::string stable_path = path.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    praktor_workflow_plan* plan = nullptr;
    const auto result = praktor_compile_workflow(&request, &plan, &error);
    INFO(error.message);
    REQUIRE(result == PRAKTOR_RESULT_SUCCESS);
    REQUIRE(plan != nullptr);
    return plan;
}

} // namespace

TEST_CASE("WorkflowPlan ABI publishes immutable plan capability", "[sdk][plan]") {
    const praktor_api* api = praktor_get_api();
    REQUIRE(api != nullptr);
    CHECK(api->abi_major == PRAKTOR_ABI_MAJOR);
    CHECK(api->abi_minor >= 2);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_WORKFLOW_PLAN) != 0);
    REQUIRE(api->compile_workflow != nullptr);
    REQUIRE(api->describe_workflow_plan != nullptr);
    REQUIRE(api->execute_workflow_plan != nullptr);
    REQUIRE(api->release_workflow_plan != nullptr);
}

TEST_CASE("WorkflowPlan describes reviewed dependency closure with SHA-256",
          "[sdk][plan][closure]") {
    const auto dir = createPlanTempDir();
    const auto root = dir / "workflow.yml";
    const auto included = dir / "shared.yml";
    const auto nested = dir / "nested.yml";
    const auto script = dir / "model.tbs";
    const auto dotenv = dir / "config.env";

    writePlanFile(included, R"(tasks:
  - name: included_probe
    command: "echo included"
)");
    writePlanFile(nested, R"(tasks:
  - name: nested_probe
    command: "echo nested"
)");
    writePlanFile(script, "var imported_value = 1;\n");
    writePlanFile(dotenv, "PLAN_TEST=value\n");
    writePlanFile(root, R"(dotEnv:
  - config.env
includes:
  shared: ./shared.yml
tasks:
  - name: inspect
    script: |
      import("./model.tbs");
      ctx.output("ready", true);
  - name: nested
    uses: ./nested.yml
)");

    praktor_error error = PRAKTOR_ERROR_INIT;
    praktor_workflow_plan* plan = compilePlan(root, error);

    praktor_owned_json metadata = PRAKTOR_OWNED_JSON_INIT;
    REQUIRE(praktor_describe_workflow_plan(plan, &metadata, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(metadata.data != nullptr);

    const auto json =
        WorkflowValue::parse(std::string_view(metadata.data, metadata.size));
    const auto digest = json.at("digest").as<std::string>();
    CHECK(digest.size() == 64);
    REQUIRE(json.at("dependencies").is_array());
    CHECK(json.at("dependencies").size() == 5);

    const std::string serialized(metadata.data, metadata.size);
    CHECK(serialized.find("workflow.yml") != std::string::npos);
    CHECK(serialized.find("shared.yml") != std::string::npos);
    CHECK(serialized.find("nested.yml") != std::string::npos);
    CHECK(serialized.find("model.tbs") != std::string::npos);
    CHECK(serialized.find("config.env") != std::string::npos);
    CHECK(serialized.find("\"root\"") != std::string::npos);
    CHECK(serialized.find("\"include\"") != std::string::npos);
    CHECK(serialized.find("\"uses\"") != std::string::npos);
    CHECK(serialized.find("\"script\"") != std::string::npos);
    CHECK(serialized.find("\"dotenv\"") != std::string::npos);

    praktor_release_json(&metadata);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("WorkflowPlan executes unchanged reviewed workflow", "[sdk][plan][execute]") {
    const auto dir = createPlanTempDir();
    const auto root = dir / "workflow.yml";
    writePlanFile(root, R"(tasks:
  - name: hello
    command: "echo workflow-plan"
)");

    praktor_error error = PRAKTOR_ERROR_INIT;
    praktor_workflow_plan* plan = compilePlan(root, error);

    static constexpr char input[] = "{}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    REQUIRE(praktor_execute_workflow_plan(&request, nullptr, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(output.data != nullptr);
    CHECK(std::string_view(output.data, output.size).find(
              "\"workflow_status\":\"success\"") != std::string_view::npos);

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("WorkflowPlan rejects dependency mutation before execution",
          "[sdk][plan][mismatch]") {
    const auto dir = createPlanTempDir();
    const auto root = dir / "workflow.yml";
    const auto nested = dir / "nested.yml";

    writePlanFile(nested, R"(tasks:
  - name: initial_probe
    command: "echo initial"
)");
    writePlanFile(root, R"(tasks:
  - name: nested
    uses: ./nested.yml
)");

    praktor_error error = PRAKTOR_ERROR_INIT;
    praktor_workflow_plan* plan = compilePlan(root, error);

    writePlanFile(nested, R"(tasks:
  - name: changed
    command: "echo changed"
)");

    static constexpr char input[] = "{}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    CHECK(praktor_execute_workflow_plan(&request, nullptr, &output, &error) ==
          PRAKTOR_RESULT_PLAN_MISMATCH);
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_PLAN);
    CHECK(std::string(error.message).find("nested.yml") != std::string::npos);
    CHECK(output.data == nullptr);
    CHECK(output.size == 0);

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("WorkflowPlan rejects transitive workflow escape outside root",
          "[sdk][plan][root]") {
    const auto parent = createPlanTempDir();
    const auto root_dir = parent / "reviewed";
    const auto root = root_dir / "workflow.yml";
    const auto outside = parent / "outside.yml";

    writePlanFile(outside, R"(tasks:
  - name: outside_probe
    command: "echo outside"
)");
    writePlanFile(root, R"(tasks:
  - name: escaped
    uses: ../outside.yml
)");

    const std::string stable_path = root.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;

    CHECK(praktor_compile_workflow(&request, &plan, &error) ==
          PRAKTOR_RESULT_PLAN_INVALID);
    CHECK(plan == nullptr);
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_PLAN);
    CHECK(std::string(error.message).find("escapes workflow root") !=
          std::string::npos);

    std::filesystem::remove_all(parent);
}
