#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

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

TEST_CASE("parse minimal run_command task")
{
    const std::string content =
        "variables:\n"
        "  APP_NAME: praktor\n"
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
    CHECK(build.action == TaskAction::Btdsl);
    REQUIRE(std::holds_alternative<BtdslParams>(build.specifics));
    const auto& params = std::get<BtdslParams>(build.specifics);
    REQUIRE(!params.root.children.empty());
    CHECK(params.root.children[0].params.at("cmd") == "echo building");
    CHECK(build.vars.at("MESSAGE") == "{{ APP_NAME }}");
    CHECK(build.when.has_value());
}

TEST_CASE("parse uses tasks")
{
    auto reused = writeTempWorkflow("reusable.yml",
        "tasks:\n"
        "  - name: step\n"
        "    command: echo from reusable\n");

    std::string main_content =
        "tasks:\n"
        "  - name: step-one\n"
        "    command: echo inline\n";
    main_content += "  - name: run-reusable\n";
    main_content += "    uses: " + reused.generic_string() + "\n";
    main_content += "    vars:\n      INPUT: data\n";

    auto wf = writeTempWorkflow("main.yml", main_content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);

    const Task& cmdTask = workflow.tasks[0];
    CHECK(cmdTask.action == TaskAction::Btdsl);
    REQUIRE(std::holds_alternative<BtdslParams>(cmdTask.specifics));

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
        "    command: echo using module\n";

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
        "  - name: notify_http\n"
        "    command: echo sending notification\n"
        "    env:\n"
        "      URL: \"{{ URL }}\"\n"
        "  - name: build\n"
        "    command: echo done\n"
        "    env:\n"
        "      URL: https://example.com\n"
        "    triggers:\n"
        "      on_failure:\n"
        "        - notify_http\n";

    auto wf = writeTempWorkflow("triggers.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);
    const Task& build = workflow.tasks[1];
    REQUIRE(build.triggers.has_value());
    const Triggers& triggers = *build.triggers;
    REQUIRE(triggers.on_failure.size() == 1);
    CHECK(triggers.on_failure[0] == "notify_http");
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

TEST_CASE("top-level defaults apply to included tasks")
{
    auto included = writeTempWorkflow("defaults_included.yml",
        "tasks:\n"
        "  - name: included_step\n"
        "    command: echo included\n");

    std::string content =
        "defaults:\n"
        "  retries:\n"
        "    count: 2\n"
        "    delay: 5s\n"
        "  timeout: 30s\n"
        "includes:\n"
        "  child: " + included.filename().generic_string() + "\n"
        "tasks:\n"
        "  - name: root_step\n"
        "    command: echo root\n";

    auto wf = writeTempWorkflow("defaults_main.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    auto it = std::find_if(workflow.tasks.begin(), workflow.tasks.end(),
        [](const Task& task) { return task.name == "included_step"; });

    REQUIRE(it != workflow.tasks.end());
    REQUIRE(it->retries.has_value());
    CHECK(it->retries->count == 2);
    CHECK(it->retries->delay == "5s");
    REQUIRE(it->timeout.has_value());
    CHECK(it->timeout.value() == "30s");
}

TEST_CASE("build graph rejects duplicate task names")
{
    const std::string content =
        "tasks:\n"
        "  - name: duplicate\n"
        "    command: echo first\n"
        "  - name: duplicate\n"
        "    command: echo second\n";

    auto wf = writeTempWorkflow("duplicate_names.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());

    REQUIRE_THROWS_WITH(TaskParser::buildGraph(workflow),
        Catch::Matchers::ContainsSubstring("Duplicate task name: 'duplicate'"));
}

TEST_CASE("parse rejects http_request BT node in workflow grammar")
{
    const std::string content =
        "tasks:\n"
        "  - name: api_call\n"
        "    sequence:\n"
        "      - http_request:\n"
        "          method: GET\n"
        "          url: https://example.com/data\n";

    auto wf = writeTempWorkflow("http_request_forbidden.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Unknown BTDSL node type: 'http_request'"));
}

TEST_CASE("parse accepts snake_case BT leaf nodes in explicit YAML trees")
{
    const std::string content =
        "tasks:\n"
        "  - name: inspect_json\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: echo {\\\"value\\\":7}\n"
        "          output_key: stdout\n"
        "      - parse_json:\n"
        "          input_key: stdout\n"
        "          output_key: data\n";

    auto wf = writeTempWorkflow("snake_case_bt_leaf.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<BtdslParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<BtdslParams>(workflow.tasks[0].specifics);
    REQUIRE(params.root.children.size() == 2);
    CHECK(params.root.children[0].type == "Shell");
    CHECK(params.root.children[1].type == "ParseJson");
    CHECK(params.root.children[1].params.at("input_key") == "stdout");
    CHECK(params.root.children[1].params.at("output_key") == "data");
    CHECK(params.root.children[1].params.find("path") == params.root.children[1].params.end());
}

TEST_CASE("parse accepts BT shorthand leaf roots")
{
    const std::string content =
        "tasks:\n"
        "  - name: wait_briefly\n"
        "    sleep: \"1s\"\n"
        "  - name: check_config\n"
        "    file_exists: ./config.yml\n";

    auto wf = writeTempWorkflow("bt_shorthand_leaf_roots.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 2);

    REQUIRE(std::holds_alternative<BtdslParams>(workflow.tasks[0].specifics));
    REQUIRE(std::holds_alternative<BtdslParams>(workflow.tasks[1].specifics));

    const auto& sleep_params = std::get<BtdslParams>(workflow.tasks[0].specifics);
    const auto& file_params = std::get<BtdslParams>(workflow.tasks[1].specifics);

    CHECK(sleep_params.root.type == "Sleep");
    CHECK(sleep_params.root.params.at("duration") == "1s");
    CHECK(file_params.root.type == "FileExists");
    CHECK(file_params.root.params.at("path") == "./config.yml");
}

TEST_CASE("parse rejects text btdsl blocks in workflow grammar")
{
    const std::string content =
        "tasks:\n"
        "  - name: legacy_bt\n"
        "    btdsl: |\n"
        "      tree Main { Shell(cmd=\\\"echo legacy\\\") }\n";

    auto wf = writeTempWorkflow("legacy_text_btdsl.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("btdsl must be a map"));
}
