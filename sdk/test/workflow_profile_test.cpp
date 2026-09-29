#include "praktor.h"
#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

namespace {

std::filesystem::path profileTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-profile-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeProfileFile(const std::filesystem::path& path,
                      const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << content;
}

WorkflowValue describeProfile(const std::filesystem::path& workflow) {
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
    WorkflowValue value = WorkflowValue::parse(
        std::string_view(description.data, description.size));

    praktor_release_json(&description);
    praktor_release_workflow_plan(plan);
    return value;
}

std::set<std::string> strings(const WorkflowValue& array) {
    std::set<std::string> result;
    for (const auto& item : array.array_range()) {
        result.insert(item.as<std::string>());
    }
    return result;
}

} // namespace

TEST_CASE("strict typed workflow qualifies for harness_safe profile",
          "[sdk][plan][profile]") {
    const auto dir = profileTempDir();
    const auto workflow = dir / "safe.yml";

    writeProfileFile(workflow, R"(
input_policy: strict
inputs:
  target:
    type: string
    required: true

outputs:
  result:
    type: string
    required: true
    value: "{{ tasks.run.outputs.stdout }}"

tasks:
  - name: run
    command: "echo safe"
)");

    const auto description = describeProfile(workflow);
    const auto profile =
        description.at("profiles").at("harness_safe");

    CHECK(profile.at("qualified").as<bool>());
    CHECK(profile.at("reasons").is_array());
    CHECK(profile.at("reasons").size() == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("harness_safe profile reports review blockers explicitly",
          "[sdk][plan][profile]") {
    const auto dir = profileTempDir();
    const auto outside = profileTempDir();
    const auto workflow = dir / "unsafe.yml";
    const std::string outside_path =
        (outside / "artifact.bin").generic_string();

    writeProfileFile(workflow,
        "tasks:\n"
        "  - name: fetch_model\n"
        "    download:\n"
        "      url: https://example.com/model-input\n"
        "      path: \"" + outside_path + "\"\n"
        "  - name: model_call\n"
        "    script: |\n"
        "      import(\"net\");\n"
        "      import(\"mystery_plugin\");\n"
        "      var url = \"https://api.openai.com/v1/responses\";\n"
        "      var response = http.get(url, map{});\n"
        "      ctx.output(\"ok\", true);\n");

    const auto description = describeProfile(workflow);
    const auto profile =
        description.at("profiles").at("harness_safe");

    CHECK_FALSE(profile.at("qualified").as<bool>());
    const auto reasons = strings(profile.at("reasons"));
    CHECK(reasons.count(
              "harness_safe requires input_policy: strict") == 1);
    CHECK(reasons.count(
              "harness_safe requires at least one declared public output") == 1);
    CHECK(reasons.count(
              "harness_safe rejects workflows with unknown effects") == 1);
    CHECK(reasons.count(
              "harness_safe rejects proven outside-workspace access") == 1);
    CHECK(reasons.count(
              "harness_safe rejects direct model-provider calls") == 1);

    const auto effects = strings(
        description.at("effect_manifest").at("effects"));
    CHECK(effects.count("model_api") == 1);
    CHECK(effects.count("outside_workspace") == 1);
    CHECK(description.at("effect_manifest")
              .at("unknown_effects").as<bool>());

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}
