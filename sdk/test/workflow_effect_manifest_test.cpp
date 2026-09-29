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
    REQUIRE(out.is_open());
    out << content;
}

WorkflowValue describePlan(const std::filesystem::path& workflow) {
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
    REQUIRE(description.data != nullptr);

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

TEST_CASE("WorkflowPlan publishes transitive conservative effect manifest",
          "[sdk][plan][effects]") {
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
    REQUIRE(manifest.is_object());

    const auto effects = stringSet(manifest.at("effects"));
    CHECK(effects.count("filesystem_read") == 1);
    CHECK(effects.count("filesystem_write") == 1);
    CHECK(effects.count("network") == 1);
    CHECK(effects.count("process") == 1);
    CHECK(effects.count("system_control") == 1);
    CHECK(effects.count("plugin") == 1);
    CHECK(manifest.at("unknown_effects").as<bool>());

    const auto reasons = stringSet(manifest.at("unknown_reasons"));
    CHECK(reasons.count("unknown TurboScript plugin: mystery_plugin") == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("known data-only script plugins do not create unknown effects",
          "[sdk][plan][effects]") {
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
    CHECK_FALSE(manifest.at("unknown_effects").as<bool>());
    CHECK(manifest.at("effects").is_array());
    CHECK(manifest.at("effects").size() == 0);
    CHECK(manifest.at("unknown_reasons").size() == 0);

    std::filesystem::remove_all(dir);
}

TEST_CASE("effect manifest distinguishes generated writes and proven outside-root access",
          "[sdk][plan][effects]") {
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

    CHECK(effects.count("filesystem_read") == 1);
    CHECK(effects.count("filesystem_write") == 1);
    CHECK(effects.count("network") == 1);
    CHECK(effects.count("process") == 1);
    CHECK(effects.count("outside_workspace") == 1);
    CHECK_FALSE(manifest.at("unknown_effects").as<bool>());

    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(outside);
}
