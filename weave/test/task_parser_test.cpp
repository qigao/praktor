#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

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

TEST_CASE("parse minimal run_command task")
{
    const std::string content =
        "variables:\n"
        "  APP_NAME: weave\n"
        "env:\n"
        "  GLOBAL_FLAG: enabled\n\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo building\n"
        "    when: \"{{ GLOBAL_FLAG }} == 'enabled'\"\n"
        "    vars:\n"
        "      MESSAGE: \"{{ APP_NAME }}\"\n";

    auto wf = writeTempWorkflow("minimal.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& build = workflow.tasks[0];
    CHECK(build.name == "build");
    CHECK(build.action == TaskAction::RunCommand);
    REQUIRE(std::holds_alternative<RunCommandParams>(build.specifics));
    const auto& params = std::get<RunCommandParams>(build.specifics);
    REQUIRE(std::holds_alternative<std::string>(params.command));
    CHECK(std::get<std::string>(params.command) == "echo building");
    CHECK(build.vars.at("MESSAGE") == "{{ APP_NAME }}");
    CHECK(build.when.has_value());
}

TEST_CASE("parse script and uses tasks")
{
    auto reused = writeTempWorkflow("reusable.yml",
        "tasks:\n"
        "  - name: step\n"
        "    script:\n"
        "      source: |\n"
        "        context.set(\"result\", \"from reusable\");\n");

    std::string main_content =
        "tasks:\n"
        "  - name: step-one\n"
        "    script:\n"
        "      source: |\n"
        "        context.set(\"value\", \"inline\");\n";
    main_content += "  - name: run-reusable\n";
    main_content += "    uses: " + reused.generic_string() + "\n";
    main_content += "    vars:\n      INPUT: data\n";

    auto wf = writeTempWorkflow("main.yml", main_content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);

    const Task& scriptTask = workflow.tasks[0];
    CHECK(scriptTask.action == TaskAction::Script);
    REQUIRE(std::holds_alternative<ScriptParams>(scriptTask.specifics));
    CHECK(std::get<ScriptParams>(scriptTask.specifics).source.find("inline") != std::string::npos);

    const Task& usesTask = workflow.tasks[1];
    CHECK(usesTask.action == TaskAction::Uses);
    REQUIRE(std::holds_alternative<UsesParams>(usesTask.specifics));
    CHECK_FALSE(std::get<UsesParams>(usesTask.specifics).path.empty());
    CHECK(usesTask.vars.at("INPUT") == "data");
}

TEST_CASE("parse task triggers and environment")
{
    const std::string content =
        "tasks:\n"
        "  - name: notify\n"
        "    command: echo done\n"
        "    env:\n"
        "      URL: https://example.com\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - http_post:\n"
        "            url: \"{{ URL }}\"\n";

    auto wf = writeTempWorkflow("triggers.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& notify = workflow.tasks[0];
    REQUIRE(notify.triggers.has_value());
    const Triggers& triggers = *notify.triggers;
    REQUIRE(triggers.on_failure.size() == 1);
}
