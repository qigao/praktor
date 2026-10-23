#include "praktor_test.hpp"
#include "praktor.h"

#include "data/workflow_value.hpp"


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
    check((output.is_open()));
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

praktor_workflow_plan* compilePlan(const std::filesystem::path& path,
                                   praktor_error& error) {
    const std::string stable_path = path.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    praktor_workflow_plan* plan = nullptr;
    const auto result = praktor_compile_workflow(&request, &plan, &error);
    info("%s", praktor_test::message([&](std::ostream& out) { out << error.message; }).c_str());
    check((result == PRAKTOR_RESULT_SUCCESS));
    check((plan != nullptr));
    return plan;
}

} // namespace

[[maybe_unused]] static void test_workflowplan_abi_publishes_immutable_plan_capability_1() {
    const praktor_api* api = praktor_get_api();
    check((api != nullptr));
    check((api->abi_major == PRAKTOR_ABI_MAJOR));
    check((api->abi_minor >= 2));
    check(((api->capabilities & PRAKTOR_CAPABILITY_WORKFLOW_PLAN) != 0));
    check((api->compile_workflow != nullptr));
    check((api->describe_workflow_plan != nullptr));
    check((api->execute_workflow_plan != nullptr));
    check((api->release_workflow_plan != nullptr));
}

spec("WorkflowPlan ABI publishes immutable plan capability") {
    it("complete case") { test_workflowplan_abi_publishes_immutable_plan_capability_1(); }
}

[[maybe_unused]] static void test_workflowplan_describes_reviewed_dependency_closure_with_sha_256_2() {
    const auto dir = createPlanTempDir();
    const auto root = dir / "workflow.yml";
    const auto included = dir / "shared.yml";
    const auto nested = dir / "nested.yml";
    const auto script = dir / "model.tbs";
    const auto dotenv = dir / "config.env";
    const auto included_dotenv = dir / "shared.env";

    writePlanFile(included, R"(dotEnv:
  - shared.env
tasks:
  - name: included_probe
    command: "echo included"
)");
    writePlanFile(nested, R"(tasks:
  - name: nested_probe
    command: "echo nested"
)");
    writePlanFile(script, "var imported_value = 1;\n");
    writePlanFile(dotenv, "PLAN_TEST=value\n");
    writePlanFile(included_dotenv, "INCLUDED_PLAN_TEST=value\n");
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
    check((praktor_describe_workflow_plan(plan, &metadata, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((metadata.data != nullptr));

    const auto json =
        WorkflowValue::parse(std::string_view(metadata.data, metadata.size));
    const auto digest = json.at("digest").as<std::string>();
    check((digest.size() == 64));
    check((json.at("dependencies").is_array()));
    check((json.at("dependencies").size() == 6));

    const std::string serialized(metadata.data, metadata.size);
    check((serialized.find("workflow.yml") != std::string::npos));
    check((serialized.find("shared.yml") != std::string::npos));
    check((serialized.find("nested.yml") != std::string::npos));
    check((serialized.find("model.tbs") != std::string::npos));
    check((serialized.find("config.env") != std::string::npos));
    check((serialized.find("shared.env") != std::string::npos));
    check((serialized.find("\"root\"") != std::string::npos));
    check((serialized.find("\"include\"") != std::string::npos));
    check((serialized.find("\"uses\"") != std::string::npos));
    check((serialized.find("\"script\"") != std::string::npos));
    check((serialized.find("\"dotenv\"") != std::string::npos));

    praktor_release_json(&metadata);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("WorkflowPlan describes reviewed dependency closure with SHA-256") {
    it("complete case") { test_workflowplan_describes_reviewed_dependency_closure_with_sha_256_2(); }
}

[[maybe_unused]] static void test_workflowplan_executes_unchanged_reviewed_workflow_3() {
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
    check((praktor_execute_workflow_plan(&request, nullptr, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((output.data != nullptr));
    check((std::string_view(output.data, output.size).find(
              "\"workflow_status\":\"success\"") != std::string_view::npos));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("WorkflowPlan executes unchanged reviewed workflow") {
    it("complete case") { test_workflowplan_executes_unchanged_reviewed_workflow_3(); }
}

[[maybe_unused]] static void test_workflowplan_rejects_dependency_mutation_before_execution_4() {
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
    check((praktor_execute_workflow_plan(&request, nullptr, &output, &error) ==
          PRAKTOR_RESULT_PLAN_MISMATCH));
    check((error.phase == PRAKTOR_ERROR_PHASE_PLAN));
    check((std::string(error.message).find("nested.yml") != std::string::npos));
    check((output.data == nullptr));
    check((output.size == 0));

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("WorkflowPlan rejects dependency mutation before execution") {
    it("complete case") { test_workflowplan_rejects_dependency_mutation_before_execution_4(); }
}

[[maybe_unused]] static void test_workflowplan_rejects_transitive_workflow_escape_outside_root_5() {
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

    check((praktor_compile_workflow(&request, &plan, &error) ==
          PRAKTOR_RESULT_PLAN_INVALID));
    check((plan == nullptr));
    check((error.phase == PRAKTOR_ERROR_PHASE_PLAN));
    check((std::string(error.message).find("escapes workflow root") !=
          std::string::npos));

    std::filesystem::remove_all(parent);
}

spec("WorkflowPlan rejects transitive workflow escape outside root") {
    it("complete case") { test_workflowplan_rejects_transitive_workflow_escape_outside_root_5(); }
}
