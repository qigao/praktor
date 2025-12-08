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
    auto dir = std::filesystem::temp_directory_path() / "weave_parser_tests";
    std::filesystem::create_directories(dir);
    std::filesystem::path path = dir / name;
    std::ofstream out(path);
    out << content;
    return path;
}
}

TEST_CASE("parse @weave trigger shorthand and mapping")
{
    const std::string content =
        "tasks:\n"
        "  - name: notify\n"
        "    command: echo ok\n"
        "    triggers:\n"
        "      on_success:\n"
        "        - \"@weave Hello World\"\n"
        "      on_failure:\n"
        "        - weave:\n"
        "            message: Oops\n";

    auto wf = writeTempWorkflow("trigger_weave.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& t = workflow.tasks[0];
    REQUIRE(t.triggers.has_value());
    const Triggers& tr = *t.triggers;
    REQUIRE(tr.on_success.size() == 1);
    REQUIRE(tr.on_failure.size() == 1);

    CHECK(std::holds_alternative<WeaveNotifyTrigger>(tr.on_success[0]));
    CHECK(std::holds_alternative<WeaveNotifyTrigger>(tr.on_failure[0]));
}
