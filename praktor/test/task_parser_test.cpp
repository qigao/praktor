#include "praktor_test.hpp"
#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"


#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

struct ForceTerminateCase {
    std::string name;
    std::string scalar;
    bool should_pass{false};
    bool expected_value{false};
};

struct SystemActionStringCase {
    std::string name;
    std::string action;
    std::string field;
    std::string scalar;
    bool should_pass{false};
    std::string expected;
    std::string error_fragment;
};

std::vector<SystemActionStringCase> loadSystemActionStringCorpus()
{
    std::ifstream input(PRAKTOR_SYSTEM_ACTION_STRING_CORPUS);
    if (!input) {
        throw std::runtime_error("Cannot open system action string corpus");
    }

    std::vector<SystemActionStringCase> cases;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t')) {
            fields.push_back(field);
        }
        if (fields.size() != 7) {
            throw std::runtime_error("Invalid system action string corpus row");
        }

        cases.push_back(SystemActionStringCase{
            fields[0], fields[1], fields[2], fields[3], fields[4] == "pass",
            fields[5], fields[6],
        });
    }
    return cases;
}

std::string systemActionCorpusWorkflow(const SystemActionStringCase& test_case)
{
    std::ostringstream yaml;
    yaml << "tasks:\n"
         << "  - name: corpus_task\n";
    if (test_case.action == "service") {
        yaml << "    service:\n"
             << "      operation: status\n"
             << "      name: "
             << (test_case.field == "name" ? test_case.scalar : "FixtureService") << "\n"
             << "      profile: "
             << (test_case.field == "profile" ? test_case.scalar : "windows_scm") << "\n"
             << "      arguments: ["
             << (test_case.field == "arguments" ? test_case.scalar : "baseline") << "]\n";
        return yaml.str();
    }

    yaml << "    managed_process:\n"
         << "      operation: "
         << (test_case.field == "restart_without_executable" ? "restart" : "status") << "\n";
    if (test_case.field != "restart_without_executable") {
        yaml << "      executable: "
             << (test_case.field == "executable" ? test_case.scalar : "worker.exe") << "\n";
    }
    yaml << "      arguments: ["
         << (test_case.field == "arguments" ? test_case.scalar : "baseline") << "]\n"
         << "      identity:\n"
         << "        image_name: "
         << (test_case.field == "image_name" ? test_case.scalar : "worker.exe") << "\n";
    return yaml.str();
}

std::vector<ForceTerminateCase> loadForceTerminateCorpus()
{
    std::ifstream input(PRAKTOR_FORCE_TERMINATE_CORPUS);
    if (!input) {
        throw std::runtime_error("Cannot open force_terminate corpus");
    }

    std::vector<ForceTerminateCase> cases;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, '\t')) {
            fields.push_back(field);
        }
        if (fields.size() != 5) {
            throw std::runtime_error("Invalid force_terminate corpus row");
        }

        const bool should_pass = fields[3] == "pass";
        cases.push_back(ForceTerminateCase{
            fields[0], fields[1], should_pass,
            should_pass && fields[4] == "true",
        });
    }
    return cases;
}

}

[[maybe_unused]] static void test_parse_minimal_run_command_task_1() {
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
    check((workflow.tasks.size() == 1));
    const Task& build = workflow.tasks[0];
    check((build.name == "build"));
    check((build.action == TaskAction::Orch));
    check((std::holds_alternative<OrchParams>(build.specifics)));
    const auto& params = std::get<OrchParams>(build.specifics);
    check((!params.root.children.empty()));
    check((params.root.children[0].params.at("cmd") == "echo building"));
    check((build.vars.at("MESSAGE") == "{{ APP_NAME }}"));
    check((build.when.has_value()));
}

spec("parse minimal run_command task") {
    it("complete case") { test_parse_minimal_run_command_task_1(); }
}

[[maybe_unused]] static void test_parse_command_post_processor_as_part_of_the_command_runner_2() {
    auto wf = writeTempWorkflow("command_parse_json.yml",
        "tasks:\n"
        "  - name: query\n"
        "    command: echo '{\"status\":\"ok\"}'\n"
        "    parse_json:\n"
        "      path: $.status\n"
        "      output_key: status\n");

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 1));
    const Task& task = workflow.tasks[0];
    check((task.declared_runner == "command"));
    check((std::holds_alternative<OrchParams>(task.specifics)));

    const auto& params = std::get<OrchParams>(task.specifics);
    check((params.root.children.size() == 2));
    check((params.root.children[0].type == "Shell"));
    check((params.root.children[1].type == "ParseJson"));
    check((params.root.children[1].params.at("path") == "$.status"));
    check((params.root.children[1].params.at("output_key") == "status"));
}

spec("parse command post-processor as part of the command runner") {
    it("complete case") { test_parse_command_post_processor_as_part_of_the_command_runner_2(); }
}

[[maybe_unused]] static void test_parse_rejects_multiple_command_post_processors_3() {
    auto wf = writeTempWorkflow("multiple_command_parsers.yml",
        "tasks:\n"
        "  - name: ambiguous\n"
        "    command: echo data\n"
        "    parse_json:\n"
        "      path: $.status\n"
        "      output_key: status\n"
        "    parse_lines:\n"
        "      output_key: lines\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
            "command task cannot combine 'parse_json' and 'parse_lines'", "line 7"})));
}

spec("parse rejects multiple command post-processors") {
    it("complete case") { test_parse_rejects_multiple_command_post_processors_3(); }
}

[[maybe_unused]] static void test_parse_uses_tasks_4() {
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
    check((workflow.tasks.size() == 2));

    const Task& cmdTask = workflow.tasks[0];
    check((cmdTask.action == TaskAction::Orch));
    check((std::holds_alternative<OrchParams>(cmdTask.specifics)));

    const Task& usesTask = workflow.tasks[1];
    check((usesTask.action == TaskAction::Uses));
    check((std::holds_alternative<UsesParams>(usesTask.specifics)));
    check((!(std::get<UsesParams>(usesTask.specifics).path.empty())));
    check((usesTask.vars.at("INPUT") == "data"));
}

spec("parse uses tasks") {
    it("complete case") { test_parse_uses_tasks_4(); }
}

[[maybe_unused]] static void test_parse_program_task_5() {
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
    check((workflow.tasks.size() == 1));

    const Task& task = workflow.tasks[0];
    check((task.action == TaskAction::Program));
    check((task.declared_runner == "program"));
    check((std::holds_alternative<ProgramParams>(task.specifics)));

    const auto& params = std::get<ProgramParams>(task.specifics);
    check((params.program == "tool"));
    check((params.args.size() == 2));
    check((params.args[0] == "alpha beta"));
    check((params.args[1] == "--flag"));
    check((params.input == "payload"));
    check((params.output_format == CommandOutputFormat::Json));
}

spec("parse program task") {
    it("complete case") { test_parse_program_task_5(); }
}

[[maybe_unused]] static void test_parse_download_task_6() {
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
    check((workflow.tasks.size() == 1));

    const Task& task = workflow.tasks[0];
    check((task.action == TaskAction::Download));
    check((task.declared_runner == "download"));
    check((std::holds_alternative<DownloadParams>(task.specifics)));
    const auto& params = std::get<DownloadParams>(task.specifics);
    check((params.url == "{{ DOWNLOAD_URL }}"));
    check((params.path == "packages/release.zip"));
    check((params.sha256 == "{{ SHA256 }}"));
    check((params.overwrite));
    check((params.timeout_ms == 300000));
}

spec("parse download task") {
    it("complete case") { test_parse_download_task_6(); }
}

[[maybe_unused]] static void test_parse_service_and_managed_process_tasks_7() {
    auto wf = writeTempWorkflow("system_actions.yml", R"(
tasks:
  - name: start_camera
    service:
      operation: start
      name: RetroCamera
      profile: windows_scm
      arguments: [--verbose]
      timeout_ms: 30000
      poll_interval_ms: 200
  - name: start_saver
    managed_process:
      operation: start
      executable: RetroScreenSaver.exe
      arguments: [--fullscreen]
      working_directory: C:/Retro
      identity:
        image_name: RetroScreenSaver.exe
      startup_timeout_ms: 5000
      stop_timeout_ms: 5000
      force_terminate: true
)");

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));

    const Task& service_task = workflow.tasks[0];
    check((service_task.action == TaskAction::Service));
    check((service_task.declared_runner == "service"));
    check((std::holds_alternative<ServiceParams>(service_task.specifics)));
    const auto& service = std::get<ServiceParams>(service_task.specifics);
    check((service.operation == SystemOperation::Start));
    check((service.name == "RetroCamera"));
    check((service.profile == "windows_scm"));
    check((service.arguments == StrList{"--verbose"}));
    check((service.timeout_ms == 30000));
    check((service.poll_interval_ms == 200));

    const Task& process_task = workflow.tasks[1];
    check((process_task.action == TaskAction::ManagedProcess));
    check((process_task.declared_runner == "managed_process"));
    check((std::holds_alternative<ManagedProcessParams>(process_task.specifics)));
    const auto& process = std::get<ManagedProcessParams>(process_task.specifics);
    check((process.operation == SystemOperation::Start));
    check((process.executable == "RetroScreenSaver.exe"));
    check((process.arguments == StrList{"--fullscreen"}));
    check((process.working_directory == "C:/Retro"));
    check((process.identity.image_name == "RetroScreenSaver.exe"));
    check((process.startup_timeout_ms == 5000));
    check((process.stop_timeout_ms == 5000));
    check((process.force_terminate));
}

spec("parse service and managed_process tasks") {
    it("complete case") { test_parse_service_and_managed_process_tasks_7(); }
}

[[maybe_unused]] static void test_parse_system_action_defaults_8() {
    auto wf = writeTempWorkflow("system_action_defaults.yml", R"(
tasks:
  - name: query_camera
    service:
      name: RetroCamera
  - name: query_saver
    managed_process:
      identity:
        image_name: RetroScreenSaver.exe
)");

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));

    const auto& service = std::get<ServiceParams>(workflow.tasks[0].specifics);
    check((service.operation == SystemOperation::Status));
    check((service.profile == "windows_scm"));
    check((service.timeout_ms == 30000));
    check((service.poll_interval_ms == 200));

    const auto& process = std::get<ManagedProcessParams>(workflow.tasks[1].specifics);
    check((process.operation == SystemOperation::Status));
    check((process.executable.empty()));
    check((process.startup_timeout_ms == 5000));
    check((process.stop_timeout_ms == 5000));
    check((!(process.force_terminate)));
}

spec("parse system action defaults") {
    it("complete case") { test_parse_system_action_defaults_8(); }
}

[[maybe_unused]] static void test_force_terminate_corpus_has_identical_taskparser_boolean_semantics_9() {
    for (const auto& test_case : loadForceTerminateCorpus()) {
        info("%s", praktor_test::message([&](std::ostream& out) { out << test_case.name << ", " << test_case.scalar; }).c_str());
        const auto wf = writeTempWorkflow(
            "force_terminate_" + test_case.name + ".yml",
            "tasks:\n"
            "  - name: query_worker\n"
            "    managed_process:\n"
            "      operation: status\n"
            "      identity:\n"
            "        image_name: worker.exe\n"
            "      force_terminate: " + test_case.scalar + "\n");

        if (!test_case.should_pass) {
            check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'force_terminate' must be a boolean"})));
            continue;
        }

        const Workflow workflow = TaskParser::parseFile(wf.string());
        check((workflow.tasks.size() == 1));
        const auto& process = std::get<ManagedProcessParams>(workflow.tasks[0].specifics);
        check((process.force_terminate == test_case.expected_value));
    }
}

spec("force_terminate corpus has identical TaskParser boolean semantics") {
    it("complete case") { test_force_terminate_corpus_has_identical_taskparser_boolean_semantics_9(); }
}

[[maybe_unused]] static void test_system_action_string_corpus_has_strict_taskparser_scalar_semantics_10() {
    for (const auto& test_case : loadSystemActionStringCorpus()) {
        info("%s", praktor_test::message([&](std::ostream& out) { out << test_case.name << ", " << test_case.action << ", " << test_case.field << ", " << test_case.scalar; }).c_str());
        const auto wf = writeTempWorkflow(
            "system_action_string_" + test_case.name + ".yml",
            systemActionCorpusWorkflow(test_case));

        if (!test_case.should_pass) {
            check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {test_case.error_fragment})));
            continue;
        }

        const Workflow workflow = TaskParser::parseFile(wf.string());
        check((workflow.tasks.size() == 1));
        if (test_case.action == "service") {
            const auto& parsed = std::get<ServiceParams>(workflow.tasks[0].specifics);
            if (test_case.field == "name") {
                check((parsed.name == test_case.expected));
            } else if (test_case.field == "profile") {
                check((parsed.profile == test_case.expected));
            } else {
                check((parsed.arguments.size() == 1));
                check((parsed.arguments.front() == test_case.expected));
            }
        } else {
            const auto& parsed = std::get<ManagedProcessParams>(workflow.tasks[0].specifics);
            if (test_case.field == "executable") {
                check((parsed.executable == test_case.expected));
            } else if (test_case.field == "image_name") {
                check((parsed.identity.image_name == test_case.expected));
            } else {
                check((parsed.arguments.size() == 1));
                check((parsed.arguments.front() == test_case.expected));
            }
        }
    }
}

spec("system action string corpus has strict TaskParser scalar semantics") {
    it("complete case") { test_system_action_string_corpus_has_strict_taskparser_scalar_semantics_10(); }
}

[[maybe_unused]] static void test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(int section_index) {
    if (section_index == 0) {
        auto unknown_key = writeTempWorkflow("service_unknown_key.yml", R"(
tasks:
  - name: start_camera
    service:
      name: RetroCamera
      profile_name: windows_scm
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(unknown_key.string())); }, {"Unknown key: 'profile_name'", "line 6"})));

        auto invalid_operation = writeTempWorkflow("service_invalid_operation.yml", R"(
tasks:
  - name: start_camera
    service:
      operation: enable
      name: RetroCamera
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(invalid_operation.string())); }, {"Unsupported service.operation value: 'enable'", "line 5"})));
    }

    if (section_index == 1) {
        auto empty_name = writeTempWorkflow("service_empty_name.yml", R"(
tasks:
  - name: start_camera
    service:
      name: ""
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(empty_name.string())); }, {"service.name cannot be empty", "line 5"})));

        auto zero_timeout = writeTempWorkflow("service_zero_timeout.yml", R"(
tasks:
  - name: start_camera
    service:
      name: RetroCamera
      timeout_ms: 0
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(zero_timeout.string())); }, {"service.timeout_ms must be positive", "line 6"})));

        auto scalar_arguments = writeTempWorkflow("service_scalar_arguments.yml", R"(
tasks:
  - name: start_camera
    service:
      name: RetroCamera
      arguments: --verbose
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(scalar_arguments.string())); }, {"service.arguments must be a sequence", "line 6"})));
    }

    if (section_index == 2) {
        auto unknown_identity_key = writeTempWorkflow("managed_process_unknown_identity_key.yml", R"(
tasks:
  - name: start_saver
    managed_process:
      identity:
        image: RetroScreenSaver.exe
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(unknown_identity_key.string())); }, {"Unknown key: 'image'", "line 6"})));

        auto missing_executable = writeTempWorkflow("managed_process_missing_executable.yml", R"(
tasks:
  - name: start_saver
    managed_process:
      operation: start
      identity:
        image_name: RetroScreenSaver.exe
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(missing_executable.string())); }, {"managed_process.start requires 'executable'", "line 4"})));

        auto scalar_arguments = writeTempWorkflow("managed_process_scalar_arguments.yml", R"(
tasks:
  - name: start_saver
    managed_process:
      executable: RetroScreenSaver.exe
      arguments: --fullscreen
      identity:
        image_name: RetroScreenSaver.exe
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(scalar_arguments.string())); }, {"managed_process.arguments must be a sequence", "line 6"})));
    }

    if (section_index == 3) {
        auto empty_identity = writeTempWorkflow("managed_process_empty_identity.yml", R"(
tasks:
  - name: stop_saver
    managed_process:
      identity:
        image_name: ""
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(empty_identity.string())); }, {"managed_process.identity.image_name cannot be empty", "line 6"})));

        auto negative_timeout = writeTempWorkflow("managed_process_negative_timeout.yml", R"(
tasks:
  - name: stop_saver
    managed_process:
      identity:
        image_name: RetroScreenSaver.exe
      stop_timeout_ms: -1
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(negative_timeout.string())); }, {"managed_process.stop_timeout_ms must be positive", "line 7"})));

        auto missing_identity = writeTempWorkflow("managed_process_missing_identity.yml", R"(
tasks:
  - name: stop_saver
    managed_process:
      operation: stop
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(missing_identity.string())); }, {"managed_process requires 'identity'", "line 4"})));
    }

    if (section_index == 4) {
        auto wf = writeTempWorkflow("multiple_system_runners.yml", R"(
tasks:
  - name: ambiguous
    service:
      name: RetroCamera
    managed_process:
      identity:
        image_name: RetroScreenSaver.exe
)");
        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"declares multiple runners", "line 3"})));
    }
}

spec("parse rejects invalid system action schemas with source lines") {
    it("service rejects unknown keys and invalid operation") { test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(0); }
    it("service rejects empty names and non-positive timeouts") { test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(1); }
    it("managed process rejects invalid fields and missing start executable") { test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(2); }
    it("managed process rejects empty identity, non-positive timeout, and missing identity") { test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(3); }
    it("system actions remain mutually exclusive runners") { test_parse_rejects_invalid_system_action_schemas_with_source_lines_11(4); }
}

[[maybe_unused]] static void test_download_task_rejects_missing_required_fields_12() {
    const std::string content =
        "tasks:\n"
        "  - name: fetch_package\n"
        "    download:\n"
        "      path: packages/release.zip\n";

    auto wf = writeTempWorkflow("download_missing_url.yml", content);
    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"download requires 'url'"})));
}

spec("download task rejects missing required fields") {
    it("complete case") { test_download_task_rejects_missing_required_fields_12(); }
}



[[maybe_unused]] static void test_parse_scalar_depends_on_and_dotenv_fields_13() {
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
    check((workflow.dot_env.size() == 1));
    check((workflow.dot_env[0] == ".env.global"));

    check((workflow.tasks.size() == 2));
    const Task& deploy = workflow.tasks[1];
    check((deploy.depends_on.size() == 1));
    check((deploy.depends_on[0] == "build"));
    check((deploy.dot_env.size() == 1));
    check((deploy.dot_env[0] == ".env.deploy"));
}

spec("parse scalar depends_on and dotEnv fields") {
    it("complete case") { test_parse_scalar_depends_on_and_dotenv_fields_13(); }
}


[[maybe_unused]] static void test_parse_task_triggers_and_environment_14() {
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
    check((workflow.tasks.size() == 2));
    const Task& build = workflow.tasks[1];
    check((build.triggers.has_value()));
    const Triggers& triggers = *build.triggers;
    check((triggers.on_failure.size() == 1));
    check((triggers.on_failure[0] == "notify_http"));
}

spec("parse task triggers and environment") {
    it("complete case") { test_parse_task_triggers_and_environment_14(); }
}

[[maybe_unused]] static void test_parse_working_dir_attribute_15() {
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
    check((workflow.tasks.size() == 3));

    check((workflow.tasks[0].working_dir.has_value()));
    check((workflow.tasks[0].working_dir.value() == "./packages/frontend"));

    check((workflow.tasks[1].working_dir.has_value()));
    check((workflow.tasks[1].working_dir.value() == "./packages/backend"));

    check((!(workflow.tasks[2].working_dir.has_value())));  // default - not set
}

spec("parse working_dir attribute") {
    it("complete case") { test_parse_working_dir_attribute_15(); }
}

[[maybe_unused]] static void test_parse_silent_attribute_16() {
    const std::string content =
        "tasks:\n"
        "  - name: noisy_task\n"
        "    command: echo hello\n"
        "  - name: quiet_task\n"
        "    command: echo secret\n"
        "    silent: true\n";

    auto wf = writeTempWorkflow("silent.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));

    check((workflow.tasks[0].silent == false));  // default
    check((workflow.tasks[1].silent == true));
}

spec("parse silent attribute") {
    it("complete case") { test_parse_silent_attribute_16(); }
}

[[maybe_unused]] static void test_parse_rejects_invalid_silent_values_17() {
    auto wf = writeTempWorkflow("invalid_silent.yml",
        "tasks:\n"
        "  - name: quiet\n"
        "    command: echo quiet\n"
        "    silent: ture\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'silent' must be a boolean", "line 4"})));
}

spec("parse rejects invalid silent values") {
    it("complete case") { test_parse_rejects_invalid_silent_values_17(); }
}

[[maybe_unused]] static void test_silent_rejects_managed_process_only_numeric_boolean_forms_18() {
    for (const std::string scalar : {"01", "0x1"}) {
        info("%s", praktor_test::message([&](std::ostream& out) { out << scalar; }).c_str());
        auto wf = writeTempWorkflow(
            "invalid_numeric_silent_" + scalar + ".yml",
            "tasks:\n"
            "  - name: quiet\n"
            "    command: echo quiet\n"
            "    silent: " + scalar + "\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'silent' must be a boolean"})));
    }
}

spec("silent rejects managed-process-only numeric boolean forms") {
    it("complete case") { test_silent_rejects_managed_process_only_numeric_boolean_forms_18(); }
}

[[maybe_unused]] static void test_parse_rejects_removed_continue_on_error_field_19() {
    auto wf = writeTempWorkflow("removed_continue_on_error.yml",
        "tasks:\n"
        "  - name: tolerant\n"
        "    command: echo tolerant\n"
        "    continue_on_error: true\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'continue_on_error'", "line 4"})));
}

spec("parse rejects removed continue_on_error field") {
    it("complete case") { test_parse_rejects_removed_continue_on_error_field_19(); }
}

[[maybe_unused]] static void test_parse_rejects_empty_script_only_tasks_20() {
    auto wf = writeTempWorkflow("empty_script.yml",
        "tasks:\n"
        "  - name: empty\n"
        "    script: \"\"\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'script' cannot be empty", "line 3"})));
}

spec("parse rejects empty script-only tasks") {
    it("complete case") { test_parse_rejects_empty_script_only_tasks_20(); }
}

[[maybe_unused]] static void test_parse_rejects_empty_task_lifecycle_values_21(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("empty_finally.yml",
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n"
            "    finally: \"\"\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'finally' cannot be empty", "line 4"})));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("empty_dynamic_template.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    dynamic_tasks:\n"
            "      items_variable: items\n"
            "      template:\n"
            "        name: \"\"\n"
            "        command: \"\"\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "dynamic_tasks template 'name' cannot be empty", "line 6"})));
    }
}

spec("parse rejects empty task lifecycle values") {
    it("finally") { test_parse_rejects_empty_task_lifecycle_values_21(0); }
    it("dynamic template") { test_parse_rejects_empty_task_lifecycle_values_21(1); }
}

[[maybe_unused]] static void test_parse_rejects_empty_command_array_entries_22() {
    auto wf = writeTempWorkflow("empty_command_entry.yml",
        "tasks:\n"
        "  - name: build\n"
        "    command: [echo build, \"\"]\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"command array entries cannot be empty", "line 3"})));
}

spec("parse rejects empty command array entries") {
    it("complete case") { test_parse_rejects_empty_command_array_entries_22(); }
}

[[maybe_unused]] static void test_parse_rejects_removed_native_modules_field_23() {
    auto wf = writeTempWorkflow("removed_native_modules.yml",
        "native_modules: []\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'native_modules'", "line 1"})));
}

spec("parse rejects removed native_modules field") {
    it("complete case") { test_parse_rejects_removed_native_modules_field_23(); }
}

[[maybe_unused]] static void test_parse_rejects_unknown_nested_workflow_keys_24(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("invalid_defaults_key.yml",
            "defaults:\n"
            "  timeuot: 30s\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'timeuot'", "line 2"})));
    }

}

spec("parse rejects unknown nested workflow keys") {
    it("defaults") { test_parse_rejects_unknown_nested_workflow_keys_24(0); }
}

[[maybe_unused]] static void test_parse_rejects_removed_workflow_module_fields_25(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("removed_embedded.yml",
            "embedded: {}\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'embedded'", "line 1"})));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("removed_imports.yml",
            "imports: []\n"
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'imports'", "line 1"})));
    }
}

spec("parse rejects removed workflow module fields") {
    it("embedded") { test_parse_rejects_removed_workflow_module_fields_25(0); }
    it("imports") { test_parse_rejects_removed_workflow_module_fields_25(1); }
}

[[maybe_unused]] static void test_parse_rejects_unknown_top_level_workflow_keys_26() {
    auto wf = writeTempWorkflow("invalid_workflow_key.yml",
        "name: invalid-root\n"
        "varibles:\n"
        "  BUILD: release\n"
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'varibles'", "line 2"})));
}

spec("parse rejects unknown top-level workflow keys") {
    it("complete case") { test_parse_rejects_unknown_top_level_workflow_keys_26(); }
}

[[maybe_unused]] static void test_parse_rejects_unknown_nested_task_configuration_keys_27(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("invalid_each_key.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    command: echo item\n"
            "    each:\n"
            "      items: [one]\n"
            "      index_varible: index\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'index_varible'", "line 6"})));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("invalid_trigger_key.yml",
            "tasks:\n"
            "  - name: build\n"
            "    command: echo build\n"
            "    triggers:\n"
            "      on_failuer: [notify]\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'on_failuer'", "line 5"})));
    }

    if (section_index == 2) {
        auto wf = writeTempWorkflow("invalid_command_parser_key.yml",
            "tasks:\n"
            "  - name: query\n"
            "    command: echo data\n"
            "    parse_json:\n"
            "      path: $.value\n"
            "      output_ky: value\n"
            "      output_key: value\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'output_ky'", "line 6"})));
    }

    if (section_index == 3) {
        auto wf = writeTempWorkflow("invalid_dynamic_template_key.yml",
            "tasks:\n"
            "  - name: fanout\n"
            "    dynamic_tasks:\n"
            "      items_variable: tasks.query.outputs.data\n"
            "      template:\n"
            "        name: generated_{{ index }}\n"
            "        command: echo generated\n"
            "        timeuot: 5s\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown key: 'timeuot'", "line 8"})));
    }
}

spec("parse rejects unknown nested task configuration keys") {
    it("each") { test_parse_rejects_unknown_nested_task_configuration_keys_27(0); }
    it("triggers") { test_parse_rejects_unknown_nested_task_configuration_keys_27(1); }
    it("command parser") { test_parse_rejects_unknown_nested_task_configuration_keys_27(2); }
    it("dynamic task template") { test_parse_rejects_unknown_nested_task_configuration_keys_27(3); }
}

[[maybe_unused]] static void test_parse_rejects_empty_trigger_action_lists_28() {
    auto wf = writeTempWorkflow("empty_trigger_list.yml",
        "tasks:\n"
        "  - name: build\n"
        "    command: echo build\n"
        "    triggers:\n"
        "      on_success: []\n"
        "      on_failure: [notify]\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
            "Trigger action list must contain at least one task name", "line 5"})));
}

spec("parse rejects empty trigger action lists") {
    it("complete case") { test_parse_rejects_empty_trigger_action_lists_28(); }
}

[[maybe_unused]] static void test_parse_rejects_mapping_command_values_with_source_location_29() {
    auto wf = writeTempWorkflow("mapping_command.yml",
        "tasks:\n"
        "  - name: invalid\n"
        "    command:\n"
        "      cmd: echo invalid\n");

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"command must be a scalar or sequence", "line 3"})));
}

spec("parse rejects mapping command values with source location") {
    it("complete case") { test_parse_rejects_mapping_command_values_with_source_location_29(); }
}

[[maybe_unused]] static void test_top_level_defaults_apply_to_included_tasks_30() {
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

    check((it != workflow.tasks.end()));
    check((it->timeout.has_value()));
    check((it->timeout.value() == "30s"));
}

spec("top-level defaults apply to included tasks") {
    it("complete case") { test_top_level_defaults_apply_to_included_tasks_30(); }
}

[[maybe_unused]] static void test_parse_rejects_invalid_include_shapes_31(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("invalid_includes.yml",
            "includes:\n"
            "  - child.yml\n"
            "tasks:\n"
            "  - name: root\n"
            "    command: echo root\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"'includes' must be a map", "line 1"})));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("empty_include_path.yml",
            "includes:\n"
            "  child: \"\"\n"
            "tasks:\n"
            "  - name: root\n"
            "    command: echo root\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"include path cannot be empty", "line 2"})));
    }

}

spec("parse rejects invalid include shapes") {
    it("includes must be a map") { test_parse_rejects_invalid_include_shapes_31(0); }
    it("include paths cannot be empty") { test_parse_rejects_invalid_include_shapes_31(1); }
}

[[maybe_unused]] static void test_parsefilewithincludes_resolves_a_relative_workflow_against_basepath_32() {
    auto wf = writeTempWorkflow("relative_main.yml",
        "tasks:\n"
        "  - name: root\n"
        "    command: echo root\n");

    Workflow workflow = TaskParser::parseFileWithIncludes(
        wf.filename().string(), wf.parent_path().string());

    check((workflow.tasks.size() == 1));
    check((workflow.tasks[0].name == "root"));
    check((std::filesystem::path(workflow.source_path) == std::filesystem::absolute(wf)));
}

spec("parseFileWithIncludes resolves a relative workflow against basePath") {
    it("complete case") { test_parsefilewithincludes_resolves_a_relative_workflow_against_basepath_32(); }
}

[[maybe_unused]] static void test_build_graph_rejects_duplicate_task_names_33() {
    const std::string content =
        "tasks:\n"
        "  - name: duplicate\n"
        "    command: echo first\n"
        "  - name: duplicate\n"
        "    command: echo second\n";

    auto wf = writeTempWorkflow("duplicate_names.yml", content);
    Workflow workflow = TaskParser::parseFile(wf.string());

    check((praktor_test::throws_containing([&] { (void)(TaskParser::buildGraph(workflow)); }, {"Duplicate task name: 'duplicate'"})));
}

spec("build graph rejects duplicate task names") {
    it("complete case") { test_build_graph_rejects_duplicate_task_names_33(); }
}

[[maybe_unused]] static void test_parse_rejects_duplicate_yaml_mapping_keys_with_source_location_34() {
    const std::string content =
        "tasks:\n"
        "  - name: first\n"
        "    name: second\n"
        "    command: echo duplicate\n";

    auto wf = writeTempWorkflow("duplicate_yaml_key.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Duplicate mapping key", "line 3"})));
}

spec("parse rejects duplicate YAML mapping keys with source location") {
    it("complete case") { test_parse_rejects_duplicate_yaml_mapping_keys_with_source_location_34(); }
}

[[maybe_unused]] static void test_parse_rejects_http_request_bt_node_in_workflow_grammar_35() {
    const std::string content =
        "tasks:\n"
        "  - name: api_call\n"
        "    sequence:\n"
        "      - http_request:\n"
        "          method: GET\n"
        "          url: https://example.com/data\n";

    auto wf = writeTempWorkflow("http_request_forbidden.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown orchestration node type: 'http_request'"})));
}

spec("parse rejects http_request BT node in workflow grammar") {
    it("complete case") { test_parse_rejects_http_request_bt_node_in_workflow_grammar_35(); }
}

[[maybe_unused]] static void test_parse_accepts_snake_case_bt_leaf_nodes_in_explicit_yaml_trees_36() {
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
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.children.size() == 2));
    check((params.root.children[0].type == "Shell"));
    check((params.root.children[1].type == "ParseJson"));
    check((params.root.children[1].params.at("input_key") == "stdout"));
    check((params.root.children[1].params.at("output_key") == "data"));
    check((params.root.children[1].params.find("path") == params.root.children[1].params.end()));
}

spec("parse accepts snake_case BT leaf nodes in explicit YAML trees") {
    it("complete case") { test_parse_accepts_snake_case_bt_leaf_nodes_in_explicit_yaml_trees_36(); }
}

[[maybe_unused]] static void test_parse_accepts_bt_shorthand_leaf_roots_37() {
    const std::string content =
        "tasks:\n"
        "  - name: wait_briefly\n"
        "    sleep: \"1s\"\n"
        "  - name: check_config\n"
        "    file_exists: ./config.yml\n";

    auto wf = writeTempWorkflow("bt_shorthand_leaf_roots.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 2));

    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));
    check((std::holds_alternative<OrchParams>(workflow.tasks[1].specifics)));

    const auto& sleep_params = std::get<OrchParams>(workflow.tasks[0].specifics);
    const auto& file_params = std::get<OrchParams>(workflow.tasks[1].specifics);

    check((sleep_params.root.type == "Sleep"));
    check((sleep_params.root.params.at("duration") == "1s"));
    check((file_params.root.type == "FileExists"));
    check((file_params.root.params.at("path") == "./config.yml"));
}

spec("parse accepts BT shorthand leaf roots") {
    it("complete case") { test_parse_accepts_bt_shorthand_leaf_roots_37(); }
}

[[maybe_unused]] static void test_parse_rejects_multiple_task_runners_including_bt_shorthand_38() {
    const std::string command_and_shell =
        "tasks:\n"
        "  - name: confused\n"
        "    command: echo one\n"
        "    shell: echo two\n";

    auto wf = writeTempWorkflow("multiple_runner_shorthand.yml", command_and_shell);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"declares multiple runners"})));

    const std::string command_and_program =
        "tasks:\n"
        "  - name: raw_confused\n"
        "    command: echo one\n"
        "    program: tool\n";

    auto program_wf = writeTempWorkflow("multiple_command_program.yml", command_and_program);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(program_wf.string())); }, {"declares multiple runners"})));

    const std::string two_shorthands =
        "tasks:\n"
        "  - name: also_confused\n"
        "    shell: echo one\n"
        "    sleep: 1s\n";

    auto shorthand_wf = writeTempWorkflow("multiple_bt_shorthand.yml", two_shorthands);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(shorthand_wf.string())); }, {"declares multiple runners"})));
}

spec("parse rejects multiple task runners including BT shorthand") {
    it("complete case") { test_parse_rejects_multiple_task_runners_including_bt_shorthand_38(); }
}

[[maybe_unused]] static void test_parse_rejects_text_orch_blocks_in_workflow_grammar_39() {
    const std::string content =
        "tasks:\n"
        "  - name: legacy_orch\n"
        "    actions: |\n"
        "      tree Main { Shell(cmd=\\\"echo legacy\\\") }\n";

    auto wf = writeTempWorkflow("legacy_text_orch.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"orch node must be a map"})));
}

spec("parse rejects text orch blocks in workflow grammar") {
    it("complete case") { test_parse_rejects_text_orch_blocks_in_workflow_grammar_39(); }
}

[[maybe_unused]] static void test_parse_accepts_flat_if_syntax_with_then_and_else_branches_40() {
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
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.children.size() == 1));

    const auto& if_node = params.root.children[0];
    check((if_node.type == "IfThenElse"));
    check((if_node.params.at("condition") == "{ctx.tasks.fetch.outputs.ok}"));
    check((if_node.children.size() == 2));
    check((if_node.children[0].type == "Sequence"));
    check((if_node.children[1].type == "Sequence"));
    check((if_node.children[0].children.size() == 1));
    check((if_node.children[1].children.size() == 1));
    check((if_node.children[0].children[0].type == "Shell"));
    check((if_node.children[1].children[0].type == "Shell"));
}

spec("parse accepts flat if syntax with then and else branches") {
    it("complete case") { test_parse_accepts_flat_if_syntax_with_then_and_else_branches_40(); }
}

[[maybe_unused]] static void test_parse_rejects_legacy_if_then_else_and_while_do_grammar_41() {
    const std::string if_content =
        "tasks:\n"
        "  - name: legacy_controls\n"
        "    actions:\n"
        "      if_then_else:\n"
        "        condition: true\n"
        "        then:\n"
        "          - shell: echo healthy\n";

    auto wf = writeTempWorkflow("legacy_if_orch.yml", if_content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Legacy orchestration node type 'if_then_else'"})));

    const std::string while_content =
        "tasks:\n"
        "  - name: legacy_loop\n"
        "    actions:\n"
        "      while_do:\n"
        "        condition: true\n"
        "        do:\n"
        "          - shell: echo loop\n";

    auto while_wf = writeTempWorkflow("legacy_while_orch.yml", while_content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(while_wf.string())); }, {"Legacy orchestration node type 'while_do'"})));
}

spec("parse rejects legacy if_then_else and while_do grammar") {
    it("complete case") { test_parse_rejects_legacy_if_then_else_and_while_do_grammar_41(); }
}

[[maybe_unused]] static void test_parse_accepts_flat_timeout_orch_roots_without_treating_them_as_task_timeout_42() {
    const std::string content =
        "tasks:\n"
        "  - name: guard\n"
        "    timeout: 250\n"
        "    child:\n"
        "      shell: echo guarded\n";

    auto wf = writeTempWorkflow("timeout_bt_root.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 1));
    check((!(workflow.tasks[0].timeout.has_value())));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.type == "Timeout"));
    check((params.root.params.at("timeout_ms") == "250"));
    check((params.root.children.size() == 1));
    check((params.root.children[0].type == "Shell"));
}

spec("parse accepts flat timeout orch roots without treating them as task timeout") {
    it("complete case") { test_parse_accepts_flat_timeout_orch_roots_without_treating_them_as_task_timeout_42(); }
}

[[maybe_unused]] static void test_parse_rejects_unknown_bt_node_parameters_43() {
    const std::string content =
        "tasks:\n"
        "  - name: broken\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: echo hello\n"
        "          not_a_real_param: nope\n";

    auto wf = writeTempWorkflow("unknown_bt_param.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Unknown orchestration parameter 'not_a_real_param'"})));
}

spec("parse rejects unknown BT node parameters") {
    it("complete case") { test_parse_rejects_unknown_bt_node_parameters_43(); }
}

[[maybe_unused]] static void test_parse_rejects_malformed_bt_integer_parameters_44() {
    const std::string content =
        "tasks:\n"
        "  - name: broken_timeout\n"
        "    timeout: nope\n"
        "    child:\n"
        "      shell:\n"
        "        cmd: echo hello\n";

    auto wf = writeTempWorkflow("invalid_bt_integer_param.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Orchestration parameter 'timeout_ms'"})));
}

spec("parse rejects malformed BT integer parameters") {
    it("complete case") { test_parse_rejects_malformed_bt_integer_parameters_44(); }
}

[[maybe_unused]] static void test_parse_rejects_malformed_bt_boolean_parameters_45() {
    const std::string content =
        "tasks:\n"
        "  - name: noisy\n"
        "    shell:\n"
        "      cmd: echo hi\n"
        "      stream_output: maybe\n";

    auto wf = writeTempWorkflow("invalid_bt_bool_param.yml", content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {"Orchestration parameter 'stream_output'"})));
}

spec("parse rejects malformed BT boolean parameters") {
    it("complete case") { test_parse_rejects_malformed_bt_boolean_parameters_45(); }
}

[[maybe_unused]] static void test_parse_accepts_subtree_shorthand_with_tree_parameter_46() {
    const std::string content =
        "tasks:\n"
        "  - name: run_subtree\n"
        "    subtree: build_inner\n";

    auto wf = writeTempWorkflow("subtree_bt.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.type == "SubTree"));
    check((params.root.params.at("tree") == "build_inner"));
}

spec("parse accepts subtree shorthand with tree parameter") {
    it("complete case") { test_parse_accepts_subtree_shorthand_with_tree_parameter_46(); }
}

[[maybe_unused]] static void test_parse_accepts_flat_set_variable_syntax_and_rejects_legacy_map_syntax_47() {
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
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.children.size() == 2));
    check((params.root.children[0].type == "SetVariable"));
    check((params.root.children[0].params.at("key") == "deployment_status"));
    check((params.root.children[0].params.at("value") == "ready"));
    check((params.root.children[1].type == "SetVariable"));
    check((params.root.children[1].params.at("key") == "deployed_version"));
    check((params.root.children[1].params.at("from") == "build_version"));

    const std::string legacy_content =
        "tasks:\n"
        "  - name: legacy_setter\n"
        "    actions:\n"
        "      set_variable:\n"
        "        key: deployment_status\n"
        "        value: ready\n";

    auto legacy_wf = writeTempWorkflow("legacy_set_variable_orch.yml", legacy_content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(legacy_wf.string())); }, {"Legacy orchestration node type 'set_variable'"})));
}

spec("parse accepts flat set_variable syntax and rejects legacy map syntax") {
    it("complete case") { test_parse_accepts_flat_set_variable_syntax_and_rejects_legacy_map_syntax_47(); }
}

[[maybe_unused]] static void test_parse_accepts_flat_while_syntax_and_flat_switch_syntax_48() {
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
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.children.size() == 2));

    const auto& while_node = params.root.children[0];
    check((while_node.type == "WhileDo"));
    check((while_node.params.at("condition") == "{ctx.variables.keep_running}"));
    check((while_node.children.size() == 1));
    check((while_node.children[0].type == "Sequence"));

    const auto& switch_node = params.root.children[1];
    check((switch_node.type == "Switch"));
    check((switch_node.params.at("variable") == "selected_case"));
    check((switch_node.children.size() == 2));
    check((switch_node.children[0].type == "Shell"));
    check((switch_node.children[1].type == "Sequence"));
}

spec("parse accepts flat while syntax and flat switch syntax") {
    it("complete case") { test_parse_accepts_flat_while_syntax_and_flat_switch_syntax_48(); }
}

[[maybe_unused]] static void test_parse_rejects_legacy_switch_and_timeout_grammar_49() {
    const std::string switch_content =
        "tasks:\n"
        "  - name: legacy_switch\n"
        "    actions:\n"
        "      switch:\n"
        "        variable: selected_case\n"
        "        cases:\n"
        "          - shell: echo zero\n";

    auto switch_wf = writeTempWorkflow("legacy_switch_orch.yml", switch_content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(switch_wf.string())); }, {"Legacy orchestration node type 'switch'"})));

    const std::string timeout_content =
        "tasks:\n"
        "  - name: legacy_timeout\n"
        "    actions:\n"
        "      timeout:\n"
        "        timeout_ms: 100\n"
        "        child:\n"
        "          shell: echo guarded\n";

    auto timeout_wf = writeTempWorkflow("legacy_timeout_orch.yml", timeout_content);

    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(timeout_wf.string())); }, {"Legacy orchestration node type 'timeout'"})));
}

spec("parse rejects legacy switch and timeout grammar") {
    it("complete case") { test_parse_rejects_legacy_switch_and_timeout_grammar_49(); }
}

[[maybe_unused]] static void test_parse_accepts_flat_run_once_root_syntax_50() {
    const std::string content =
        "tasks:\n"
        "  - name: only_once\n"
        "    run_once: true\n"
        "    child:\n"
        "      shell: echo hi\n";

    auto wf = writeTempWorkflow("run_once_bt_root.yml", content);

    Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 1));
    check((std::holds_alternative<OrchParams>(workflow.tasks[0].specifics)));

    const auto& params = std::get<OrchParams>(workflow.tasks[0].specifics);
    check((params.root.type == "RunOnce"));
    check((params.root.children.size() == 1));
    check((params.root.children[0].type == "Shell"));
}

spec("parse accepts flat run_once root syntax") {
    it("complete case") { test_parse_accepts_flat_run_once_root_syntax_50(); }
}


[[maybe_unused]] static void test_parse_host_tool_task_preserves_typed_with_arguments_51() {
    auto wf = writeTempWorkflow("host_tool.yml", R"(
tasks:
  - name: inspect
    tool: repo.inspect
    with:
      path: "{{ variables.path }}"
      limit: 2
      enabled: true
      tags: [alpha, beta]
      nested:
        mode: safe
)");

    const Workflow workflow = TaskParser::parseFile(wf.string());
    check((workflow.tasks.size() == 1));
    const Task& task = workflow.tasks[0];
    check((task.action == TaskAction::HostTool));
    check((task.declared_runner == "tool"));
    check((std::holds_alternative<HostToolParams>(task.specifics)));

    const auto& params = std::get<HostToolParams>(task.specifics);
    check((params.tool == "repo.inspect"));
    check((params.arguments.is_object()));
    check((params.arguments.at("path").as<std::string>() ==
          "{{ variables.path }}"));
    check((params.arguments.at("limit").as<int>() == 2));
    check((params.arguments.at("enabled").as<bool>()));
    check((params.arguments.at("tags").is_array()));
    check((params.arguments.at("tags").at(0).as<std::string>() == "alpha"));
    check((params.arguments.at("nested").at("mode").as<std::string>() == "safe"));
}

spec("parse host tool task preserves typed with arguments") {
    it("complete case") { test_parse_host_tool_task_preserves_typed_with_arguments_51(); }
}

[[maybe_unused]] static void test_host_tool_grammar_rejects_untyped_identity_and_malformed_with_52(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("host_tool_numeric.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: 7\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'tool' must be a string stable host-tool identity"})));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("host_tool_with_array.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: repo.inspect\n"
            "    with: [one, two]\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'with' must be a map of typed host-tool arguments"})));
    }

    if (section_index == 2) {
        auto wf = writeTempWorkflow("host_tool_orphan_with.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    command: echo inspect\n"
            "    with:\n"
            "      path: src\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'with' is valid only with the 'tool' runner"})));
    }
}

spec("host tool grammar rejects untyped identity and malformed with") {
    it("tool identity must be a string scalar") { test_host_tool_grammar_rejects_untyped_identity_and_malformed_with_52(0); }
    it("with must be a map") { test_host_tool_grammar_rejects_untyped_identity_and_malformed_with_52(1); }
    it("with cannot appear without tool") { test_host_tool_grammar_rejects_untyped_identity_and_malformed_with_52(2); }
}


[[maybe_unused]] static void test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(int section_index) {
    if (section_index == 0) {
        auto wf = writeTempWorkflow("host_tool_retry.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: repo.inspect\n"
            "    retries:\n"
            "      count: 2\n"
            "    with: {}\n");

        const Workflow workflow = TaskParser::parseFile(wf.string());
        check((workflow.tasks.size() == 1));
        check((workflow.tasks[0].action == TaskAction::HostTool));
        check((workflow.tasks[0].retry_count == 2u));
    }

    if (section_index == 1) {
        auto wf = writeTempWorkflow("host_tool_retry_negative.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: repo.inspect\n"
            "    retries:\n"
            "      count: -1\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "retry count must be between 0 and 1024"})));
    }

    if (section_index == 2) {
        auto wf = writeTempWorkflow("host_tool_retry_too_large.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: repo.inspect\n"
            "    retries:\n"
            "      count: 1025\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "retry count must be between 0 and 1024"})));
    }

    if (section_index == 3) {
        auto wf = writeTempWorkflow("command_retry_rejected.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    command: echo inspect\n"
            "    retries:\n"
            "      count: 1\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'retries' is currently supported only with the 'tool' runner"})));
    }

    if (section_index == 4) {
        auto wf = writeTempWorkflow("command_zero_retry_rejected.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    command: echo inspect\n"
            "    retries:\n"
            "      count: 0\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'retries' is currently supported only with the 'tool' runner"})));
    }

    if (section_index == 5) {
        auto wf = writeTempWorkflow("host_tool_retry_missing_count.yml",
            "tasks:\n"
            "  - name: inspect\n"
            "    tool: repo.inspect\n"
            "    retries: {}\n");

        check((praktor_test::throws_containing([&] { (void)(TaskParser::parseFile(wf.string())); }, {
                "'retries' requires a non-negative integer 'count'"})));
    }
}

spec("host tool grammar parses finite retries and rejects unsupported retry shapes") {
    it("tool retry count is preserved") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(0); }
    it("retry count must be non-negative") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(1); }
    it("retry count above the hard bound is rejected") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(2); }
    it("retry count is currently tool-only") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(3); }
    it("zero retry count is still tool-only grammar") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(4); }
    it("retry policy requires count") { test_host_tool_grammar_parses_finite_retries_and_rejects_unsupported_retry_shapes_53(5); }
}
