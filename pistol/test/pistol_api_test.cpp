#include "praktor_api.h"
 

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_pistol_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

#ifdef _WIN32
constexpr const char* kSleepCommand = "waitfor SomethingThatNeverHappens /T 1 >nul 2>nul & exit /b 0";
constexpr long long kSequentialMinMs = 1800;
#else
constexpr const char* kSleepCommand = "sleep 0.5";
constexpr long long kSequentialMinMs = 900;
#endif

} // namespace

TEST_CASE("pistol API executes workflow with JSON object inputs", "[pistol]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "out.txt";

    writeFile(workflow_path, R"(
tasks:
  - name: write
    working_dir: .
    command: "echo {{ NAME }} > out.txt"
)");

    REQUIRE(praktor_execute_workflow(workflow_path.string().c_str(), R"({"NAME":"pistol"})") == PRAKTOR_API_SUCCESS);
    REQUIRE(std::filesystem::exists(output_path));

    std::ifstream input(output_path);
    std::string content;
    std::getline(input, content);
    input.close();
    CHECK(content.find("pistol") != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API rejects non-object JSON payloads", "[pistol]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, "tasks: []\n");

    CHECK(praktor_execute_workflow(workflow_path.string().c_str(), "[]") == PRAKTOR_API_INVALID_JSON);
    CHECK(praktor_execute_workflow(nullptr, "{}") == PRAKTOR_API_INVALID_ARGUMENT);
    CHECK(praktor_execute_workflow("", "{}") == PRAKTOR_API_INVALID_ARGUMENT);

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol API preserves sequential runner defaults", "[pistol]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    const std::string workflow =
        "tasks:\n"
        "  - name: first\n"
        "    command: \"" + std::string(kSleepCommand) + "\"\n"
        "\n"
        "  - name: second\n"
        "    command: \"" + std::string(kSleepCommand) + "\"\n"
        "\n"
        "  - name: done\n"
        "    depends_on: [first, second]\n"
        "    command: \"echo done\"\n";
    writeFile(workflow_path, workflow);

    auto start = std::chrono::steady_clock::now();
    REQUIRE(praktor_execute_workflow(workflow_path.string().c_str(), "{}") == PRAKTOR_API_SUCCESS);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    CHECK(duration >= kSequentialMinMs);

    std::filesystem::remove_all(dir);
}

TEST_CASE("pistol C++ API wraps the DLL C entrypoint", "[pistol]") {
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "cpp-out.txt";

    writeFile(workflow_path, R"(
tasks:
  - name: write
    working_dir: .
    command: "echo {{ NAME }} > cpp-out.txt"
)");

    REQUIRE(praktor::execute_workflow(workflow_path.string(), R"({"NAME":"cpp"})")
            == PRAKTOR_RESULT_SUCCESS);
    REQUIRE(std::filesystem::exists(output_path));

    std::ifstream input(output_path);
    std::string content;
    std::getline(input, content);
    input.close();
    CHECK(content.find("cpp") != std::string::npos);

    std::filesystem::remove_all(dir);
}
