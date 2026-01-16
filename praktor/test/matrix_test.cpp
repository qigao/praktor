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
        / ("praktor_matrix_" + std::to_string(rng()));
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

TEST_CASE("matrix and each functionality")
{
    auto original_cwd = std::filesystem::current_path();
    auto dir = createTempDir();
    
    SECTION("matrix build generates correct tasks")
    {
        std::filesystem::current_path(dir);
        auto workflow_path = dir / "workflow.yml";
        
        writeFile(workflow_path, R"(
name: "Matrix Test"
tasks:
  - name: build_platform
    each:
      matrix:
        os: ["linux", "windows"]
        arch: ["amd64"]
      as: "cfg"
    command: "echo Building for {{ cfg.os }} on {{ cfg.arch }}"
    triggers:
      on_success:
        - write_file:
            path: "./builds/{{ cfg.os }}-{{ cfg.arch }}.txt"
            content: "Build successful for {{ cfg.os }} {{ cfg.arch }}"
)"
        );

        WorkflowRunner runner(workflow_path.string());
        REQUIRE(runner.run());

        CHECK(std::filesystem::exists(dir / "builds/linux-amd64.txt"));
        CHECK(std::filesystem::exists(dir / "builds/windows-amd64.txt"));
        
        auto content = FileUtils::readFile("builds/linux-amd64.txt");
        CHECK(content == "Build successful for linux amd64");
    }

    SECTION("each items generates tasks")
    {
        std::filesystem::current_path(dir);
        auto workflow_path = dir / "workflow_items.yml";
        
        writeFile(workflow_path, R"(
tasks:
  - name: list_items
    each:
      items: ["a", "b", "c"]
      as: "val"
      index_variable: "idx"
    command: "echo {{ idx }}: {{ val }}"
    triggers:
      on_success:
        - write_file:
            path: "out_{{ val }}.txt"
            content: "{{ idx }}: {{ val }}"
)"
        );

        WorkflowRunner runner(workflow_path.string());
        REQUIRE(runner.run());

        CHECK(FileUtils::readFile((dir / "out_a.txt").string()) == "0: a");
        CHECK(FileUtils::readFile((dir / "out_b.txt").string()) == "1: b");
        CHECK(FileUtils::readFile((dir / "out_c.txt").string()) == "2: c");
    }

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(dir);
}
