#include "workflow_runner.hpp"
#include "util/file_utils.hpp"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>

namespace {

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("weave_runner_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

}

TEST_CASE("run simple workflow")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "out.txt";

    writeFile(workflow_path, R"(
tasks:
  - name: write
    command: "echo hello > out.txt"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    auto content = FileUtils::readFile("out.txt" );
    CHECK(content.find("hello") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("when condition skips task")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, R"(
variables:
  FLAG: off

tasks:
  - name: step
    when: "{{ FLAG }} == 'on'"
    command: "echo should-not-run"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
}

TEST_CASE("uses executes nested workflow and exposes outputs")
{
    auto dir = createTempDir();
    auto reusable_path = dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";

    writeFile(reusable_path, R"(
tasks:
  - name: produce
    script:
      source: |
        context.set("result", "nested");
)"
    );

    writeFile(workflow_path, std::string("tasks:\n  - name: call\n    uses: ") + reusable_path.string() + R"(
  - name: check
    depends_on: [call]
    script:
      source: |
        const value = context.get("tasks.call.outputs.result");
        if (value !== "nested") {
          throw new Error("missing output");
        }
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
}

TEST_CASE("failing command marks workflow failed")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: crash
    command: "exit 42"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
}
