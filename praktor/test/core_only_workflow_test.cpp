#include "workflow_runner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path createCoreOnlyTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-core-only-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << text;
}

} // namespace

static_assert(PRAKTOR_SCRIPT_ENGINE_ENABLED == 0,
              "core_only_workflow_test must only build with scripts disabled");

TEST_CASE("workflow value converts integral decimal JSON numbers") {
    const auto value = WorkflowValue::parse(R"({"count":7.0})");
    CHECK(value["count"].as<int>() == 7);
}

TEST_CASE("core-only build executes workflows without script tasks") {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "command.yml";

    writeText(workflow, R"(
tasks:
  - name: hello
    command: "echo core-only"
)");

    WorkflowRunner runner(workflow.string());
    const auto result = runner.execute();

    CHECK(result.success);
    CHECK(result.value["workflow_status"].as<std::string>() == "success");

    std::filesystem::remove_all(dir);
}

TEST_CASE("core-only build rejects script workflows before scheduling") {
    const auto dir = createCoreOnlyTempDir();
    const auto side_effect = dir / "should-not-exist.txt";
    const auto workflow = dir / "script.yml";

#ifdef _WIN32
    const std::string first_command = "cmd /C echo should-not-run > \"" +
                                      side_effect.string() + "\"";
#else
    const std::string first_command = "sh -c 'echo should-not-run > \"" +
                                      side_effect.string() + "\"'";
#endif

    writeText(workflow,
              "tasks:\n"
              "  - name: first\n"
              "    command: |-\n"
              "      " + first_command + "\n"
              "\n"
              "  - name: script_later\n"
              "    depends_on: [first]\n"
              "    script: |\n"
              "      ctx.output(\"value\", 1);\n");

    WorkflowRunner runner(workflow.string());
    const auto result = runner.execute();

    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("ENABLE_SCRIPT_ENGINE=OFF") != std::string::npos);
    CHECK(result.value["workflow_status"].as<std::string>() == "failed");
    CHECK(result.value["tasks"].size() == 0);
    CHECK_FALSE(std::filesystem::exists(side_effect));

    std::filesystem::remove_all(dir);
}
