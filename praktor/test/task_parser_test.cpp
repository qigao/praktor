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
    CHECK(build.action == TaskAction::Orch);
    REQUIRE(std::holds_alternative<OrchParams>(build.specifics));
    const auto& params = std::get<OrchParams>(build.specifics);
    REQUIRE(!params.root.children.empty());
    CHECK(params.root.children[0].params.at("cmd") == "echo building");
    CHECK(build.vars.at("MESSAGE") == "{{ APP_NAME }}");
    CHECK(build.when.has_value());
}

TEST_CASE("parse command post-processor as part of the command runner")
{
    auto wf = writeTempWorkflow("command_parse_json.yml",
        "tasks:\n"
        "  - name: query\n"
        "    command: echo '{\"status\":\"ok\"}'\n"
        "    parse_json:\n"
        "      path: $.status\n"
        "      output_key: status\n");

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    const Task& task = workflow.tasks[0];
    CHECK(task.declared_runner == "command");
    REQUIRE(std::holds_alternative<OrchParams>(task.specifics));

    const auto& params = std::get<OrchParams>(task.specifics);
    REQUIRE(params.root.children.size() == 2);
    CHECK(params.root.children[0].type == "Shell");
    CHECK(params.root.children[1].type == "ParseJson");
    CHECK(params.root.children[1].params.at("path") == "$.status");
    CHECK(params.root.children[1].params.at("output_key") == "status");
}

TEST_CASE("parse rejects multiple command post-processors")
{
    auto wf = writeTempWorkflow("multiple_command_parsers.yml",
        "tasks:\n"
        "  - name: ambiguous\n"
        "    command: echo data\n"
        "    parse_json:\n"
        "      path: $.status\n"
        "      output_key: status\n"
        "    parse_lines:\n"
        "      output_key: lines\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring(
            "command task cannot combine 'parse_json' and 'parse_lines'") &&
        Catch::Matchers::ContainsSubstring("line 7"));
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
    CHECK(cmdTask.action == TaskAction::Orch);
    REQUIRE(std::holds_alternative<OrchParams>(cmdTask.specifics));

    const Task& usesTask = workflow.tasks[1];
    CHECK(usesTask.action == TaskAction::Uses);
    REQUIRE(std::holds_alternative<UsesParams>(usesTask.specifics));
    CHECK_FALSE(std::get<UsesParams>(usesTask.specifics).path.empty());
    CHECK(usesTask.vars.at("INPUT") == "data");
}

TEST_CASE("parse program task")
{
    const std::string content =
        "tasks:\n"
        "  - name: probe\n"
        "    program: tool\n"
        "    args:\n"
        "      - alpha beta\n"
        "      - --flag\n"
        "    stdin: payload\n"
        "    output_format: json\n";

    auto wf = writeTempWorkflow("program.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);

    const Task& task = workflow.tasks[0];
    CHECK(task.action == TaskAction::Program);
    CHECK(task.declared_runner == "program");
    REQUIRE(std::holds_alternative<ProgramParams>(task.specifics));

    const auto& params = std::get<ProgramParams>(task.specifics);
    CHECK(params.program == "tool");
    REQUIRE(params.args.size() == 2);
    CHECK(params.args[0] == "alpha beta");
    CHECK(params.args[1] == "--flag");
    CHECK(params.input == "payload");
    CHECK(params.output_format == CommandOutputFormat::Json);
}

TEST_CASE("parse download task")
{
    const std::string content =
        "tasks:\n"
        "  - name: fetch_package\n"
        "    download:\n"
        "      url: '{{ DOWNLOAD_URL }}'\n"
        "      path: packages/release.zip\n"
        "      sha256: '{{ SHA256 }}'\n"
        "      overwrite: true\n"
        "      timeout_ms: 300000\n";

    auto wf = writeTempWorkflow("download.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);

    const Task& task = workflow.tasks[0];
    CHECK(task.action == TaskAction::Download);
    CHECK(task.declared_runner == "download");
    REQUIRE(std::holds_alternative<DownloadParams>(task.specifics));
    const auto& params = std::get<DownloadParams>(task.specifics);
    CHECK(params.url == "{{ DOWNLOAD_URL }}");
    CHECK(params.path == "packages/release.zip");
    CHECK(params.sha256 == "{{ SHA256 }}");
    CHECK(params.overwrite);
    CHECK(params.timeout_ms == 300000);
}

TEST_CASE("download task rejects missing required fields")
{
    const std::string content =
        "tasks:\n"
        "  - name: fetch_package\n"
        "    download:\n"
        "      path: packages/release.zip\n";

    auto wf = writeTempWorkflow("download_missing_url.yml", content);
    CHECK_THROWS_WITH(TaskParser::parseFile(wf.string()),
                      Catch::Matchers::ContainsSubstring("download requires 'url'"));
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

TEST_CASE("parse rejects invalid silent values")
{
    auto wf = writeTempWorkflow("invalid_silent.yml",
        "tasks:\n"
        "  - name: quiet\n"
        "    command: echo quiet\n"
        "    silent: ture\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("'silent' must be a boolean") &&
        Catch::Matchers::ContainsSubstring("line 4"));
}

TEST_CASE("parse rejects removed continue_on_error field")
{
    auto wf = writeTempWorkflow("removed_continue_on_error.yml",
        "tasks:\n"
        "  - name: tolerant\n"
        "    command: echo tolerant\n"
        "    continue_on_error: true\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Unknown key: 'continue_on_error'") &&
        Catch::Matchers::ContainsSubstring("line 4"));
}

TEST_CASE("parse rejects empty script-only tasks")
{
    auto wf = writeTempWorkflow("empty_script.yml",
        "tasks:\n"
        "  - name: empty\n"
        "    script: \"\"\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("'script' cannot be empty") &&
        Catch::Matchers::ContainsSubstring("line 3"));
}

TEST_CASE("parse rejects empty task lifecycle values")
{
    SECTION("finally") {
        auto wf = writeTempWorkflow("empty_finally.yml",
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n"
            "    finally: \"\"\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("'finally' cannot be empty") &&
            Catch::Matchers::ContainsSubstring("line 4"));
    }

    SECTION("dynamic template") {
        auto wf = writeTempWorkflow("empty_dynamic_template.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    dynamic_tasks:\n"
            "      items_variable: items\n"
            "      template:\n"
            "        name: \"\"\n"
            "        command: \"\"\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring(
                "dynamic_tasks template 'name' cannot be empty") &&
            Catch::Matchers::ContainsSubstring("line 6"));
    }
}

TEST_CASE("parse rejects empty command array entries")
{
    auto wf = writeTempWorkflow("empty_command_entry.yml",
        "tasks:\n"
        "  - name: build\n"
        "    command: [echo build, \"\"]\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("command array entries cannot be empty") &&
        Catch::Matchers::ContainsSubstring("line 3"));
}

TEST_CASE("parse rejects removed native_modules field")
{
    auto wf = writeTempWorkflow("removed_native_modules.yml",
        "native_modules: []\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Unknown key: 'native_modules'") &&
        Catch::Matchers::ContainsSubstring("line 1"));
}

TEST_CASE("parse rejects unknown nested workflow keys")
{
    SECTION("defaults") {
        auto wf = writeTempWorkflow("invalid_defaults_key.yml",
            "defaults:\n"
            "  timeuot: 30s\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'timeuot'") &&
            Catch::Matchers::ContainsSubstring("line 2"));
    }

}

TEST_CASE("parse rejects removed workflow module fields")
{
    SECTION("embedded") {
        auto wf = writeTempWorkflow("removed_embedded.yml",
            "embedded: {}\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'embedded'") &&
            Catch::Matchers::ContainsSubstring("line 1"));
    }

    SECTION("imports") {
        auto wf = writeTempWorkflow("removed_imports.yml",
            "imports: []\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'imports'") &&
            Catch::Matchers::ContainsSubstring("line 1"));
    }
}

TEST_CASE("parse rejects unknown top-level workflow keys")
{
    auto wf = writeTempWorkflow("invalid_workflow_key.yml",
        "name: invalid-root\n"
        "varibles:\n"
        "  BUILD: release\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Unknown key: 'varibles'") &&
        Catch::Matchers::ContainsSubstring("line 2"));
}

TEST_CASE("parse rejects unknown nested task configuration keys")
{
    SECTION("each") {
        auto wf = writeTempWorkflow("invalid_each_key.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    command: echo item\n"
            "    each:\n"
            "      items: [one]\n"
            "      index_varible: index\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'index_varible'") &&
            Catch::Matchers::ContainsSubstring("line 6"));
    }

    SECTION("triggers") {
        auto wf = writeTempWorkflow("invalid_trigger_key.yml",
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n"
            "    triggers:\n"
            "      on_failuer: [notify]\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'on_failuer'") &&
            Catch::Matchers::ContainsSubstring("line 5"));
    }

    SECTION("command parser") {
        auto wf = writeTempWorkflow("invalid_command_parser_key.yml",
            "tasks:\n"
            "  - name: query\n"
            "    command: echo data\n"
            "    parse_json:\n"
            "      path: $.value\n"
            "      output_ky: value\n"
            "      output_key: value\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'output_ky'") &&
            Catch::Matchers::ContainsSubstring("line 6"));
    }

    SECTION("dynamic task template") {
        auto wf = writeTempWorkflow("invalid_dynamic_template_key.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    dynamic_tasks:\n"
            "      items_variable: tasks.query.outputs.data\n"
            "      template:\n"
            "        name: generated_{{ index }}\n"
            "        command: echo generated\n"
            "        timeuot: 5s\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("Unknown key: 'timeuot'") &&
            Catch::Matchers::ContainsSubstring("line 8"));
    }
}

TEST_CASE("parse rejects empty trigger action lists")
{
    auto wf = writeTempWorkflow("empty_trigger_list.yml",
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_success: []\n"
        "      on_failure: [notify]\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring(
            "Trigger action list must contain at least one task name") &&
        Catch::Matchers::ContainsSubstring("line 5"));
}

TEST_CASE("parse rejects mapping command values with source location")
{
    auto wf = writeTempWorkflow("mapping_command.yml",
        "tasks:\n"
        "  - name: invalid\n"
        "    command:\n"
        "      cmd: echo invalid\n");

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("command must be a scalar or sequence") &&
        Catch::Matchers::ContainsSubstring("line 3"));
}

TEST_CASE("top-level defaults apply to included tasks")
{
    auto included = writeTempWorkflow("defaults_included.yml",
        "tasks:\n"
        "  - name: included_step\n"
        "    command: echo included\n");

    std::string content =
        "defaults:\n"
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
    REQUIRE(it->timeout.has_value());
    CHECK(it->timeout.value() == "30s");
}

TEST_CASE("parse rejects invalid include shapes")
{
    SECTION("includes must be a map") {
        auto wf = writeTempWorkflow("invalid_includes.yml",
            "includes:\n"
            "  - child.yml\n"
            "tasks:\n"
            "  - name: root\n"
            "    command: echo root\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("'includes' must be a map") &&
            Catch::Matchers::ContainsSubstring("line 1"));
    }

    SECTION("include paths cannot be empty") {
        auto wf = writeTempWorkflow("empty_include_path.yml",
            "includes:\n"
            "  child: \"\"\n"
            "tasks:\n"
            "  - name: root\n"
            "    command: echo root\n");

        REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
            Catch::Matchers::ContainsSubstring("include path cannot be empty") &&
            Catch::Matchers::ContainsSubstring("line 2"));
    }

}

TEST_CASE("parseFileWithIncludes resolves a relative workflow against basePath")
{
    auto wf = writeTempWorkflow("relative_main.yml",
        "tasks:\n"
        "  - name: root\n"
        "    command: echo root\n");

    Workflow workflow = TaskParser::parseFileWithIncludes(
        wf.filename().string(), wf.parent_path().string());

    REQUIRE(workflow.tasks.size() == 1);
    CHECK(workflow.tasks[0].name == "root");
    CHECK(std::filesystem::path(workflow.source_path) == std::filesystem::absolute(wf));
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

TEST_CASE("parse rejects duplicate YAML mapping keys with source location")
{
    const std::string content =
        "tasks:\n"
        "  - name: first\n"
        "    name: second\n"
        "    command: echo duplicate\n";

    auto wf = writeTempWorkflow("duplicate_yaml_key.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Duplicate mapping key") &&
        Catch::Matchers::ContainsSubstring("line 3"));
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
        Catch::Matchers::ContainsSubstring("Unknown orchestration node type: 'http_request'"));
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
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
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

    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[1].specifics));

    const auto& sleep_params = std::get<OrchParams>(workflow.tasks[0].specifics);
    const auto& file_params = std::get<OrchParams>(workflow.tasks[1].specifics);

    CHECK(sleep_params.root.type == "Sleep");
    CHECK(sleep_params.root.params.at("duration") == "1s");
    CHECK(file_params.root.type == "FileExists");
    CHECK(file_params.root.params.at("path") == "./config.yml");
}

TEST_CASE("parse rejects multiple task runners including BT shorthand")
{
    const std::string command_and_shell =
        "tasks:\n"
        "  - name: confused\n"
        "    command: echo one\n"
        "    shell: echo two\n";

    auto wf = writeTempWorkflow("multiple_runner_shorthand.yml", command_and_shell);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("declares multiple runners"));

    const std::string command_and_program =
        "tasks:\n"
        "  - name: raw_confused\n"
        "    command: echo one\n"
        "    program: tool\n";

    auto program_wf = writeTempWorkflow("multiple_command_program.yml", command_and_program);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(program_wf.string()),
        Catch::Matchers::ContainsSubstring("declares multiple runners"));

    const std::string two_shorthands =
        "tasks:\n"
        "  - name: also_confused\n"
        "    shell: echo one\n"
        "    sleep: 1s\n";

    auto shorthand_wf = writeTempWorkflow("multiple_bt_shorthand.yml", two_shorthands);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(shorthand_wf.string()),
        Catch::Matchers::ContainsSubstring("declares multiple runners"));
}

TEST_CASE("parse rejects text orch blocks in workflow grammar")
{
    const std::string content =
        "tasks:\n"
        "  - name: legacy_orch\n"
        "    actions: |\n"
        "      tree Main { Shell(cmd=\\\"echo legacy\\\") }\n";

    auto wf = writeTempWorkflow("legacy_text_orch.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("orch node must be a map"));
}

TEST_CASE("parse accepts flat if syntax with then and else branches")
{
    const std::string content =
        "tasks:\n"
        "  - name: verify_status\n"
        "    sequence:\n"
        "      - if: \"{ctx.tasks.fetch.outputs.ok}\"\n"
        "        then:\n"
        "          - shell: echo healthy\n"
        "        else:\n"
        "          - shell: echo unhealthy\n";

    auto wf = writeTempWorkflow("if_bt.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    REQUIRE(params.root.children.size() == 1);

    const auto& if_node = params.root.children[0];
    CHECK(if_node.type == "IfThenElse");
    CHECK(if_node.params.at("condition") == "{ctx.tasks.fetch.outputs.ok}");
    REQUIRE(if_node.children.size() == 2);
    CHECK(if_node.children[0].type == "Sequence");
    CHECK(if_node.children[1].type == "Sequence");
    REQUIRE(if_node.children[0].children.size() == 1);
    REQUIRE(if_node.children[1].children.size() == 1);
    CHECK(if_node.children[0].children[0].type == "Shell");
    CHECK(if_node.children[1].children[0].type == "Shell");
}

TEST_CASE("parse rejects legacy if_then_else and while_do grammar")
{
    const std::string if_content =
        "tasks:\n"
        "  - name: legacy_controls\n"
        "    actions:\n"
        "      if_then_else:\n"
        "        condition: true\n"
        "        then:\n"
        "          - shell: echo healthy\n";

    auto wf = writeTempWorkflow("legacy_if_orch.yml", if_content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Legacy orchestration node type 'if_then_else'"));

    const std::string while_content =
        "tasks:\n"
        "  - name: legacy_loop\n"
        "    actions:\n"
        "      while_do:\n"
        "        condition: true\n"
        "        do:\n"
        "          - shell: echo loop\n";

    auto while_wf = writeTempWorkflow("legacy_while_orch.yml", while_content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(while_wf.string()),
        Catch::Matchers::ContainsSubstring("Legacy orchestration node type 'while_do'"));
}

TEST_CASE("parse accepts flat timeout orch roots without treating them as task timeout")
{
    const std::string content =
        "tasks:\n"
        "  - name: guard\n"
        "    timeout: 250\n"
        "    child:\n"
        "      shell: echo guarded\n";

    auto wf = writeTempWorkflow("timeout_bt_root.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    CHECK_FALSE(workflow.tasks[0].timeout.has_value());
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    CHECK(params.root.type == "Timeout");
    CHECK(params.root.params.at("timeout_ms") == "250");
    REQUIRE(params.root.children.size() == 1);
    CHECK(params.root.children[0].type == "Shell");
}

TEST_CASE("parse rejects unknown BT node parameters")
{
    const std::string content =
        "tasks:\n"
        "  - name: broken\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: echo hello\n"
        "          not_a_real_param: nope\n";

    auto wf = writeTempWorkflow("unknown_bt_param.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Unknown orchestration parameter 'not_a_real_param'"));
}

TEST_CASE("parse rejects malformed BT integer parameters")
{
    const std::string content =
        "tasks:\n"
        "  - name: broken_timeout\n"
        "    timeout: nope\n"
        "    child:\n"
        "      shell:\n"
        "        cmd: echo hello\n";

    auto wf = writeTempWorkflow("invalid_bt_integer_param.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Orchestration parameter 'timeout_ms'"));
}

TEST_CASE("parse rejects malformed BT boolean parameters")
{
    const std::string content =
        "tasks:\n"
        "  - name: noisy\n"
        "    shell:\n"
        "      cmd: echo hi\n"
        "      stream_output: maybe\n";

    auto wf = writeTempWorkflow("invalid_bt_bool_param.yml", content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(wf.string()),
        Catch::Matchers::ContainsSubstring("Orchestration parameter 'stream_output'"));
}

TEST_CASE("parse accepts subtree shorthand with tree parameter")
{
    const std::string content =
        "tasks:\n"
        "  - name: run_subtree\n"
        "    subtree: build_inner\n";

    auto wf = writeTempWorkflow("subtree_bt.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    CHECK(params.root.type == "SubTree");
    CHECK(params.root.params.at("tree") == "build_inner");
}

TEST_CASE("parse accepts flat set_variable syntax and rejects legacy map syntax")
{
    const std::string content =
        "tasks:\n"
        "  - name: state_update\n"
        "    sequence:\n"
        "      - set_variable: deployment_status\n"
        "        value: ready\n"
        "      - set_variable: deployed_version\n"
        "        from: build_version\n";

    auto wf = writeTempWorkflow("set_variable_bt.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    REQUIRE(params.root.children.size() == 2);
    CHECK(params.root.children[0].type == "SetVariable");
    CHECK(params.root.children[0].params.at("key") == "deployment_status");
    CHECK(params.root.children[0].params.at("value") == "ready");
    CHECK(params.root.children[1].type == "SetVariable");
    CHECK(params.root.children[1].params.at("key") == "deployed_version");
    CHECK(params.root.children[1].params.at("from") == "build_version");

    const std::string legacy_content =
        "tasks:\n"
        "  - name: legacy_setter\n"
        "    actions:\n"
        "      set_variable:\n"
        "        key: deployment_status\n"
        "        value: ready\n";

    auto legacy_wf = writeTempWorkflow("legacy_set_variable_orch.yml", legacy_content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(legacy_wf.string()),
        Catch::Matchers::ContainsSubstring("Legacy orchestration node type 'set_variable'"));
}

TEST_CASE("parse accepts flat while syntax and flat switch syntax")
{
    const std::string content =
        "tasks:\n"
        "  - name: advanced_nodes\n"
        "    sequence:\n"
        "      - while: \"{ctx.variables.keep_running}\"\n"
        "        do:\n"
        "          - shell: echo loop\n"
        "      - switch: selected_case\n"
        "        cases:\n"
        "          - shell: echo zero\n"
        "          - sequence:\n"
        "              - shell: echo one\n";

    auto wf = writeTempWorkflow("advanced_bt_nodes.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    REQUIRE(params.root.children.size() == 2);

    const auto& while_node = params.root.children[0];
    CHECK(while_node.type == "WhileDo");
    CHECK(while_node.params.at("condition") == "{ctx.variables.keep_running}");
    REQUIRE(while_node.children.size() == 1);
    CHECK(while_node.children[0].type == "Sequence");

    const auto& switch_node = params.root.children[1];
    CHECK(switch_node.type == "Switch");
    CHECK(switch_node.params.at("variable") == "selected_case");
    REQUIRE(switch_node.children.size() == 2);
    CHECK(switch_node.children[0].type == "Shell");
    CHECK(switch_node.children[1].type == "Sequence");
}

TEST_CASE("parse rejects legacy switch and timeout grammar")
{
    const std::string switch_content =
        "tasks:\n"
        "  - name: legacy_switch\n"
        "    actions:\n"
        "      switch:\n"
        "        variable: selected_case\n"
        "        cases:\n"
        "          - shell: echo zero\n";

    auto switch_wf = writeTempWorkflow("legacy_switch_orch.yml", switch_content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(switch_wf.string()),
        Catch::Matchers::ContainsSubstring("Legacy orchestration node type 'switch'"));

    const std::string timeout_content =
        "tasks:\n"
        "  - name: legacy_timeout\n"
        "    actions:\n"
        "      timeout:\n"
        "        timeout_ms: 100\n"
        "        child:\n"
        "          shell: echo guarded\n";

    auto timeout_wf = writeTempWorkflow("legacy_timeout_orch.yml", timeout_content);

    REQUIRE_THROWS_WITH(TaskParser::parseFile(timeout_wf.string()),
        Catch::Matchers::ContainsSubstring("Legacy orchestration node type 'timeout'"));
}

TEST_CASE("parse accepts flat run_once root syntax")
{
    const std::string content =
        "tasks:\n"
        "  - name: only_once\n"
        "    run_once: true\n"
        "    child:\n"
        "      shell: echo hi\n";

    auto wf = writeTempWorkflow("run_once_bt_root.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    REQUIRE(workflow.tasks.size() == 1);
    REQUIRE(std::holds_alternative<OrchParams>(workflow.tasks[0].specifics));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    CHECK(params.root.type == "RunOnce");
    REQUIRE(params.root.children.size() == 1);
    CHECK(params.root.children[0].type == "Shell");
}
