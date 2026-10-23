#include "praktor_test.hpp"
#include "praktor.h"
#include "data/workflow_value.hpp"


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
    check((out.is_open()));
    out << content;
}

WorkflowValue describeProfile(const std::filesystem::path& workflow) {
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

[[maybe_unused]] static void test_strict_typed_workflow_qualifies_for_harness_safe_profile_1() {
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

    check((profile.at("qualified").as<bool>()));
    check((profile.at("reasons").is_array()));
    check((profile.at("reasons").size() == 0));

    std::filesystem::remove_all(dir);
}

spec("strict typed workflow qualifies for harness_safe profile") {
    it("complete case") { test_strict_typed_workflow_qualifies_for_harness_safe_profile_1(); }
}

[[maybe_unused]] static void test_harness_safe_profile_reports_review_blockers_explicitly_2() {
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

    check((!(profile.at("qualified").as<bool>())));
    const auto reasons = strings(profile.at("reasons"));
    check((reasons.count(
              "harness_safe requires input_policy: strict") == 1));
    check((reasons.count(
              "harness_safe requires at least one declared public output") == 1));
    check((reasons.count(
              "harness_safe rejects workflows with unknown effects") == 1));
    check((reasons.count(
              "harness_safe rejects proven outside-workspace access") == 1));
    check((reasons.count(
              "harness_safe rejects direct model-provider calls") == 1));

    const auto effects = strings(
        description.at("effect_manifest").at("effects"));
    check((effects.count("model_api") == 1));
    check((effects.count("outside_workspace") == 1));
    check((description.at("effect_manifest")
              .at("unknown_effects").as<bool>()));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}

spec("harness_safe profile reports review blockers explicitly") {
    it("complete case") { test_harness_safe_profile_reports_review_blockers_explicitly_2(); }
}


[[maybe_unused]] static void test_host_tool_effects_remain_host_resolved_and_reject_harness_safe_3() {
    const auto dir = profileTempDir();
    const auto workflow = dir / "host_tool.yml";

    writeProfileFile(workflow, R"(
input_policy: strict
inputs:
  path:
    type: string
    required: true

outputs:
  result:
    type: string
    required: true
    value: "{{ tasks.inspect.outputs.result.status }}"

tasks:
  - name: inspect
    tool: repo.inspect
    with:
      path: "{{ variables.path }}"
)");

    const auto description = describeProfile(workflow);
    const auto profile =
        description.at("profiles").at("harness_safe");

    check((!(profile.at("qualified").as<bool>())));
    const auto reasons = strings(profile.at("reasons"));
    check((reasons.count(
              "harness_safe rejects workflows with unknown effects") == 1));

    const auto manifest = description.at("effect_manifest");
    const auto effects = strings(manifest.at("effects"));
    check((effects.count("host_tool") == 1));
    check((manifest.at("unknown_effects").as<bool>()));

    const auto unknown_reasons = strings(manifest.at("unknown_reasons"));
    check((unknown_reasons.count(
              "host tool effects resolved by embedding host: repo.inspect") == 1));

    const auto host_tools = description.at("host_tools");
    check((host_tools.is_array()));
    check((host_tools.size() == 1));
    check((host_tools.at(0).at("tool").as<std::string>() == "repo.inspect"));

    std::filesystem::remove_all(dir);
}

spec("host tool effects remain host-resolved and reject harness_safe") {
    it("complete case") { test_host_tool_effects_remain_host_resolved_and_reject_harness_safe_3(); }
}
