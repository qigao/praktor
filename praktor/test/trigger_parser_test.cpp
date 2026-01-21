#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"
#include "yml/task_types.hpp"

#include <catch2/catch_all.hpp>

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


TEST_CASE("parse trigger with task name reference")
{
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
    REQUIRE(workflow.tasks.size() == 2);
    const Task& task = workflow.tasks[1];
    REQUIRE(task.triggers.has_value());
    const auto& actions = task.triggers->on_failure;
    REQUIRE(actions.size() == 1);
    CHECK(actions[0] == "rollback");
}

TEST_CASE("parse trigger with script task reference")
{
    const std::string content =
        "tasks:\n"
        "  - name: write_report\n"
        "    script:\n"
        "      source: |\n"
        "        const fs = require('fs');\n"
        "        fs.writeFileSync('./out.txt', 'done', {flag: 'a'});\n"
        "  - name: report\n"
        "    command: echo done\n"
        "    triggers:\n"
        "      on_complete:\n"
        "        - write_report\n";

    auto wf = writeTempWorkflow("trigger_write_file.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);
    const Task& task = workflow.tasks[1];
    REQUIRE(task.triggers.has_value());
    const auto& actions = task.triggers->on_complete;
    REQUIRE(actions.size() == 1);
    CHECK(actions[0] == "write_report");
}

