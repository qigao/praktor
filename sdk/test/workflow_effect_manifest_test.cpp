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

std::filesystem::path effectTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-effects-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeEffectFile(const std::filesystem::path& path,
                     const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    check((out.is_open()));
    out << content;
}

WorkflowValue describePlan(const std::filesystem::path& workflow) {
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
    check((description.data != nullptr));

    WorkflowValue value = WorkflowValue::parse(
        std::string_view(description.data, description.size));
    praktor_release_json(&description);
    praktor_release_workflow_plan(plan);
    return value;
}

std::set<std::string> stringSet(const WorkflowValue& array) {
    std::set<std::string> values;
    for (const auto& value : array.array_range()) {
        values.insert(value.as<std::string>());
    }
    return values;
}

} // namespace

[[maybe_unused]] static void test_workflowplan_publishes_transitive_conservative_effect_manifest_1() {
    const auto dir = effectTempDir();
    const auto root = dir / "root.yml";
    const auto nested = dir / "nested.yml";

    writeEffectFile(dir / ".env", "TOKEN=test\n");
    writeEffectFile(nested, R"(
tasks:
  - name: run
    command: "echo nested"
  - name: fetch
    download:
      url: https://example.com/payload
      path: payload.bin
)");
    writeEffectFile(root, R"(
dotEnv: ./.env

tasks:
  - name: nested
    uses: ./nested.yml

  - name: inspect_service
    service:
      operation: status
      name: ExampleService

  - name: scripted
    script: |
      import("net");
      import("mystery_plugin");
      ctx.output("ok", true);
)");

    const auto description = describePlan(root);
    const auto manifest = description.at("effect_manifest");
    check((manifest.is_object()));

    const auto effects = stringSet(manifest.at("effects"));
    check((effects.count("filesystem_read") == 1));
    check((effects.count("filesystem_write") == 1));
    check((effects.count("network") == 1));
    check((effects.count("process") == 1));
    check((effects.count("system_control") == 1));
    check((effects.count("plugin") == 1));
    check((manifest.at("unknown_effects").as<bool>()));

    const auto reasons = stringSet(manifest.at("unknown_reasons"));
    check((reasons.count("unknown TurboScript plugin: mystery_plugin") == 1));

    std::filesystem::remove_all(dir);
}

spec("WorkflowPlan publishes transitive conservative effect manifest") {
    it("complete case") { test_workflowplan_publishes_transitive_conservative_effect_manifest_1(); }
}

[[maybe_unused]] static void test_known_data_only_script_plugins_do_not_create_unknown_effects_2() {
    const auto dir = effectTempDir();
    const auto root = dir / "pure.yml";

    writeEffectFile(root, R"(
tasks:
  - name: transform
    script: |
      import("mapper");
      import("parser");
      ctx.output("ok", true);
)");

    const auto manifest = describePlan(root).at("effect_manifest");
    check((!(manifest.at("unknown_effects").as<bool>())));
    check((manifest.at("effects").is_array()));
    check((manifest.at("effects").size() == 0));
    check((manifest.at("unknown_reasons").size() == 0));

    std::filesystem::remove_all(dir);
}

spec("known data-only script plugins do not create unknown effects") {
    it("complete case") { test_known_data_only_script_plugins_do_not_create_unknown_effects_2(); }
}

[[maybe_unused]] static void test_effect_manifest_distinguishes_generated_writes_and_proven_outside_root_access_3() {
    const auto dir = effectTempDir();
    const auto outside = effectTempDir();
    const auto root = dir / "paths.yml";
    const std::string outside_file =
        (outside / "artifact.bin").generic_string();

    writeEffectFile(root,
        "tasks:\n"
        "  - name: build\n"
        "    sources: [input.txt]\n"
        "    generates: [output.txt]\n"
        "    command: \"echo build\"\n"
        "  - name: fetch\n"
        "    download:\n"
        "      url: https://example.com/payload\n"
        "      path: \"" + outside_file + "\"\n");

    const auto manifest = describePlan(root).at("effect_manifest");
    const auto effects = stringSet(manifest.at("effects"));

    check((effects.count("filesystem_read") == 1));
    check((effects.count("filesystem_write") == 1));
    check((effects.count("network") == 1));
    check((effects.count("process") == 1));
    check((effects.count("outside_workspace") == 1));
    check((!(manifest.at("unknown_effects").as<bool>())));

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}

spec("effect manifest distinguishes generated writes and proven outside-root access") {
    it("complete case") { test_effect_manifest_distinguishes_generated_writes_and_proven_outside_root_access_3(); }
}
