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

TEST_CASE("parse @praktor trigger shorthand and mapping")
{
    const std::string content =
        "tasks:\n"
        "  - name: notify\n"
        "    command: echo ok\n"
        "    triggers:\n"
        "      on_success:\n"
        "        - \"@praktor Hello World\"\n"
        "      on_failure:\n"
        "        - praktor:\n"
        "            message: Oops\n";

    auto wf = writeTempWorkflow("trigger_praktor.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& t = workflow.tasks[0];
    REQUIRE(t.triggers.has_value());
    const Triggers& tr = *t.triggers;
    REQUIRE(tr.on_success.size() == 1);
    REQUIRE(tr.on_failure.size() == 1);

    CHECK(std::holds_alternative<PraktorNotifyTrigger>(tr.on_success[0]));
    CHECK(std::holds_alternative<PraktorNotifyTrigger>(tr.on_failure[0]));
}


TEST_CASE("parse run_task trigger shorthand string")
{
    const std::string content =
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - rollback\n";

    auto wf = writeTempWorkflow("trigger_run_task.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& task = workflow.tasks[0];
    REQUIRE(task.triggers.has_value());
    const auto& actions = task.triggers->on_failure;
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<RunTaskTrigger>(actions[0]));
    CHECK(std::get<RunTaskTrigger>(actions[0]).task_name == "rollback");
}

TEST_CASE("parse write_file trigger object")
{
    const std::string content =
        "tasks:\n"
        "  - name: report\n"
        "    command: echo done\n"
        "    triggers:\n"
        "      on_complete:\n"
        "        - write_file:\n"
        "            path: ./out.txt\n"
        "            content: done\n"
        "            mode: append\n";

    auto wf = writeTempWorkflow("trigger_write_file.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& task = workflow.tasks[0];
    REQUIRE(task.triggers.has_value());
    const auto& actions = task.triggers->on_complete;
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<WriteFileTrigger>(actions[0]));
    const auto& trigger = std::get<WriteFileTrigger>(actions[0]);
    CHECK(trigger.path == "./out.txt");
    CHECK(trigger.content == "done");
    CHECK(trigger.mode == WriteFileMode::Append);
}

