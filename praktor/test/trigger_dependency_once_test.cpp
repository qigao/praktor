#include "praktor_test.hpp"
#include "workflow_runner.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

[[maybe_unused]] static void test_inline_trigger_dependency_is_not_repeated_by_the_regular_dag_1(std::size_t row_0) {
    const bool concurrent = std::array{false, true}.at(row_0);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() /
        ("praktor-trigger-once-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    const auto workflow = dir / "workflow.yml";
    const auto output = dir / "events.txt";
    {
        std::ofstream out(workflow);
        check((out.is_open()));
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
    check((runner.run(concurrent)));
    std::ifstream in(output);
    const std::string result((std::istreambuf_iterator<char>(in)), {});
    in.close();
    const auto first_prepare = result.find("prepare");
    const auto first_notify = result.find("notify");
    check((first_prepare != std::string::npos));
    check((first_notify != std::string::npos));
    check((first_prepare < first_notify));
    check((result.find("prepare", first_prepare + 1) == std::string::npos));
    check((result.find("notify", first_notify + 1) == std::string::npos));
    std::filesystem::remove_all(dir);
}

spec("inline trigger dependency is not repeated by the regular DAG") {
    for (std::size_t row_0 = 0; row_0 < 2; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_inline_trigger_dependency_is_not_repeated_by_the_regular_dag_1(row_0); }
    }
}
