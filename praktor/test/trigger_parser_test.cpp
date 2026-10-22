#include "praktor_test.hpp"
#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"
#include "yml/task_types.hpp"


#include <filesystem>
#include <fstream>
#include <string>

namespace {
std::filesystem::path writeTempWorkflow(const std::string& name, const std::string& content)
{
    auto dir = std::filesystem::temp_directory_path() / "praktor_parser_tests";
    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / name;
    std::ofstream out(path);
    out << content;
    return path;
}
}


[[maybe_unused]] static void test_parse_trigger_with_task_name_reference_1() {
    const std::string content =
        "tasks:\n"
        "  - name: rollback\n"
        "    command: echo rollback\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - rollback\n";

    auto wf = writeTempWorkflow("trigger_run_task.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));
    const Task& task = workflow.tasks[1];
    check((task.triggers.has_value()));
    const auto& actions = task.triggers->on_failure;
    check((actions.size() == 1));
    check((actions[0] == "rollback"));
}

spec("parse trigger with task name reference") {
    it("complete case") { test_parse_trigger_with_task_name_reference_1(); }
}

[[maybe_unused]] static void test_parse_trigger_with_command_task_reference_2() {
    const std::string content =
        "tasks:\n"
        "  - name: write_report\n"
        "    command: echo done > out.txt\n"
        "  - name: report\n"
        "    command: echo done\n"
        "    triggers:\n"
        "      on_complete:\n"
        "        - write_report\n";

    auto wf = writeTempWorkflow("trigger_write_file.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));
    const Task& task = workflow.tasks[1];
    check((task.triggers.has_value()));
    const auto& actions = task.triggers->on_complete;
    check((actions.size() == 1));
    check((actions[0] == "write_report"));
}

spec("parse trigger with command task reference") {
    it("complete case") { test_parse_trigger_with_command_task_reference_2(); }
}

[[maybe_unused]] static void test_build_graph_rejects_unknown_static_trigger_target_3() {
    const std::string content =
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - missing_task\n";

    auto wf = writeTempWorkflow("trigger_missing_task.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());

    check((praktor_test::throws_containing([&] { (void)(TaskParser::buildGraph(workflow)); }, {"references unknown task 'missing_task'"})));
}

spec("build graph rejects unknown static trigger target") {
    it("complete case") { test_build_graph_rejects_unknown_static_trigger_target_3(); }
}

[[maybe_unused]] static void test_build_graph_allows_dynamic_trigger_target_4() {
    const std::string content =
        "variables:\n"
        "  HANDLER: rollback\n"
        "tasks:\n"
        "  - name: rollback\n"
        "    command: echo rollback\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - \"{{ HANDLER }}\"\n";

    auto wf = writeTempWorkflow("trigger_dynamic_task.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());

    check_nothrow(TaskParser::buildGraph(workflow));
}

spec("build graph allows dynamic trigger target") {
    it("complete case") { test_build_graph_allows_dynamic_trigger_target_4(); }
}
