#include "praktor_test.hpp"
#include "workflow_runner.hpp"
#include "util/file_utils.hpp"
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

[[maybe_unused]] static void test_matrix_and_each_functionality_1(int section_index) {
    auto original_cwd = std::filesystem::current_path();
    auto dir = createTempDir();

    if (section_index == 0)
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
)"
        );

        WorkflowRunner runner(workflow_path.string());
        check((runner.run()));
    }

    if (section_index == 1)
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
)"
        );

        WorkflowRunner runner(workflow_path.string());
        check((runner.run()));
    }

    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(dir);
}

spec("matrix and each functionality") {
    it("matrix build generates correct tasks") { test_matrix_and_each_functionality_1(0); }
    it("each items generates tasks") { test_matrix_and_each_functionality_1(1); }
}
