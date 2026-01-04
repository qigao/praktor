#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path writeTempWorkflow(const std::string& name, const std::string& content)
{
    auto dir = std::filesystem::temp_directory_path() / "prakter_parser_tests";
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
        "  APP_NAME: prakter\n"
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



TEST_CASE("parse workflow embedded modules")
{
    const std::string content =
        "embedded:\n"
        "  helpers:\n"
        "    source: |\n"
        "      export function identity(value) {\n"
        "        return value;\n"
        "      }\n"
        "tasks:\n"
        "  - name: consumer\n"
        "    script:\n"
        "      source: |\n"
        "        context.set(\"used\", \"module\");\n";

    auto wf = writeTempWorkflow("embedded.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.embedded.size() == 1);
    auto it = workflow.embedded.find("helpers");
    REQUIRE(it != workflow.embedded.end());
    CHECK(it->second.language == "javascript");
    CHECK(it->second.source.find("identity") != std::string::npos);
}


TEST_CASE("parse scalar depends_on and dotEnv fields")
{
    const std::string content =
        "dotEnv: \".env.global\"\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n"
        "  - name: deploy\n"
        "    depends_on: build\n"
        "    dotEnv: \".env.deploy\"\n"
        "    command: echo deploy\n";

    auto wf = writeTempWorkflow("scalar_fields.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.dot_env.size() == 1);
    CHECK(workflow.dot_env[0] == ".env.global");

    REQUIRE(workflow.tasks.size() == 2);
    const Task& deploy = workflow.tasks[1];
    REQUIRE(deploy.depends_on.size() == 1);
    CHECK(deploy.depends_on[0] == "build");
    REQUIRE(deploy.dot_env.size() == 1);
    CHECK(deploy.dot_env[0] == ".env.deploy");
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

TEST_CASE("parse continue_on_error attribute")
{
    const std::string content =
        "tasks:\n"
        "  - name: test_unit\n"
        "    command: npm run test:unit\n"
        "    continue_on_error: true\n"
        "  - name: test_integration\n"
        "    command: npm run test:integration\n"
        "    continue_on_error: false\n"
        "  - name: build\n"
        "    command: npm run build\n";

    auto wf = writeTempWorkflow("continue_on_error.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 3);

    CHECK(workflow.tasks[0].continue_on_error == true);
    CHECK(workflow.tasks[1].continue_on_error == false);
    CHECK(workflow.tasks[2].continue_on_error == false);  // default
}

TEST_CASE("parse working_dir attribute")
{
    const std::string content =
        "tasks:\n"
        "  - name: build_frontend\n"
        "    working_dir: ./packages/frontend\n"
        "    command: npm run build\n"
        "  - name: build_backend\n"
        "    working_dir: ./packages/backend\n"
        "    command: cargo build\n"
        "  - name: test\n"
        "    command: npm test\n";

    auto wf = writeTempWorkflow("working_dir.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 3);

    REQUIRE(workflow.tasks[0].working_dir.has_value());
    CHECK(workflow.tasks[0].working_dir.value() == "./packages/frontend");

    REQUIRE(workflow.tasks[1].working_dir.has_value());
    CHECK(workflow.tasks[1].working_dir.value() == "./packages/backend");

    CHECK_FALSE(workflow.tasks[2].working_dir.has_value());  // default - not set
}

TEST_CASE("parse silent attribute")
{
    const std::string content =
        "tasks:\n"
        "  - name: noisy_task\n"
        "    command: echo hello\n"
        "  - name: quiet_task\n"
        "    command: echo secret\n"
        "    silent: true\n";

    auto wf = writeTempWorkflow("silent.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);

    CHECK(workflow.tasks[0].silent == false);  // default
    CHECK(workflow.tasks[1].silent == true);
}
