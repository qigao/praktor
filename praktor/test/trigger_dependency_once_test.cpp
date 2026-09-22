#include "workflow_runner.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

TEST_CASE("inline trigger dependency is not repeated by the regular DAG") {
    const bool concurrent = GENERATE(false, true);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
        ("praktor-trigger-once-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    const auto workflow = dir / "workflow.yml";
    const auto output = dir / "events.txt";
    {
        std::ofstream out(workflow);
        REQUIRE(out.is_open());
        // The DAG must schedule prepare after build. build's trigger executes
        // prepare inline first, making the previous double execution deterministic.
        out << "tasks:\n"
            << "  - name: prepare\n"
            << "    depends_on: [build]\n"
            << "    command: \"echo prepare >> " << output.generic_string() << "\"\n"
            << "  - name: notify\n"
            << "    depends_on: [prepare]\n"
            << "    command: \"echo notify >> " << output.generic_string() << "\"\n"
            << "  - name: build\n"
            << "    command: \"echo done\"\n"
            << "    triggers:\n"
            << "      on_success: [notify]\n";
    }
    WorkflowRunner runner(workflow.string());
    REQUIRE(runner.run(concurrent));
    std::ifstream in(output);
    const std::string result((std::istreambuf_iterator<char>(in)), {});
    const auto first_prepare = result.find("prepare");
    const auto first_notify = result.find("notify");
    REQUIRE(first_prepare != std::string::npos);
    REQUIRE(first_notify != std::string::npos);
    CHECK(first_prepare < first_notify);
    CHECK(result.find("prepare", first_prepare + 1) == std::string::npos);
    CHECK(result.find("notify", first_notify + 1) == std::string::npos);
    std::filesystem::remove_all(dir);
}
