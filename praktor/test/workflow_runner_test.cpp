#include "workflow_runner.hpp"
#include "dag/workflow_executor_internal.hpp"
#include "system/managed_process.hpp"
#include "util/file_utils.hpp"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_runner_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

#ifdef _WIN32
std::string shellEnvRef(const std::string& name)
{
    return "%" + name + "%";
}

void setProcessEnv(const std::string& key, const std::string& value)
{
    _putenv_s(key.c_str(), value.c_str());
}

std::string programProbeScript()
{
    return R"(
param([string]$ArgValue)
$inputText = [Console]::In.ReadToEnd()
if ($env:PRAKTOR_PROGRAM_ENV -ne 'raw') { exit 11 }
if ($inputText -ne 'payload') { exit 12 }
if ($ArgValue -ne 'alpha beta') { exit 13 }
[Console]::Out.Write('{"source":"program","value":7}')
)";
}

std::string programRunnerYaml(const std::filesystem::path& script)
{
    return
        "    program: powershell.exe\n"
        "    args:\n"
        "      - -NoProfile\n"
        "      - -ExecutionPolicy\n"
        "      - Bypass\n"
        "      - -File\n"
        "      - " + script.generic_string() + "\n"
        "      - alpha beta\n";
}
#else
std::string shellEnvRef(const std::string& name)
{
    return "$" + name;
}

void setProcessEnv(const std::string& key, const std::string& value)
{
    setenv(key.c_str(), value.c_str(), 1);
}

std::string programProbeScript()
{
    return R"(
#!/bin/sh
input=$(cat)
if [ "$PRAKTOR_PROGRAM_ENV" != "raw" ]; then exit 11; fi
if [ "$input" != "payload" ]; then exit 12; fi
if [ "$1" != "alpha beta" ]; then exit 13; fi
printf '{"source":"program","value":7}'
)";
}

std::string programRunnerYaml(const std::filesystem::path& script)
{
    return
        "    program: /bin/sh\n"
        "    args:\n"
        "      - " + script.generic_string() + "\n"
        "      - alpha beta\n";
}
#endif

std::string jsonEchoCommand(const std::string& json_text)
{
#ifdef _WIN32
    return "echo " + json_text;
#else
    return "printf '%s' '" + json_text + "'";
#endif
}

std::string writeLiteralToFileCommand(const std::filesystem::path& path, const std::string& text)
{
#ifdef _WIN32
    return "echo " + text + " > " + path.generic_string();
#else
    return "printf '%s' \"" + text + "\" > " + path.generic_string();
#endif
}

std::string failOnMatchCommand(const std::string& value_expr, const std::string& expected)
{
#ifdef _WIN32
    return "if /I \\\"" + value_expr + "\\\"==\\\"" + expected + "\\\" (exit 1) else (echo " + value_expr + ")";
#else
    return "if [ " + value_expr + " = " + expected + " ]; then exit 1; else printf '%s' " + value_expr + "; fi";
#endif
}

#ifdef _WIN32
using Praktor::System::IManagedProcessBackend;
using Praktor::System::ManagedProcessCommandResult;
using Praktor::System::ManagedProcessState;

ManagedProcessCommandResult cleanupManagedProcessFixture(
    IManagedProcessBackend& backend,
    const ManagedProcessIdentity& identity)
{
    constexpr auto cleanup_timeout = std::chrono::seconds(3);
    constexpr auto poll_interval = std::chrono::milliseconds(20);
    const auto deadline = std::chrono::steady_clock::now() + cleanup_timeout;
    bool termination_requested = false;
    std::uint32_t terminated_pid = 0;
    std::uint64_t terminated_token = 0;

    do {
        auto queried = backend.query(identity);
        if (!queried.ok) {
            return {false, queried.native_error, std::move(queried.message)};
        }
        if (queried.value.state == ManagedProcessState::NotRunning) {
            return {true, 0, {}};
        }

        const bool new_instance = !termination_requested ||
            queried.value.pid != terminated_pid ||
            queried.value.instance_token != terminated_token;
        if (new_instance) {
            auto terminated = backend.terminate(queried.value);
            if (!terminated.ok) {
                return terminated;
            }
            termination_requested = true;
            terminated_pid = queried.value.pid;
            terminated_token = queried.value.instance_token;
        }
        std::this_thread::sleep_for(poll_interval);
    } while (std::chrono::steady_clock::now() < deadline);

    auto final_query = backend.query(identity);
    if (!final_query.ok) {
        return {false, final_query.native_error, std::move(final_query.message)};
    }
    if (final_query.value.state == ManagedProcessState::NotRunning) {
        return {true, 0, {}};
    }
    return {false, ERROR_TIMEOUT,
            "timed out waiting for managed process fixture cleanup"};
}

class ManagedProcessFixtureCleanup {
public:
    ManagedProcessFixtureCleanup(IManagedProcessBackend& backend,
                                 ManagedProcessIdentity identity)
        : backend_(backend), identity_(std::move(identity)) {}

    ~ManagedProcessFixtureCleanup() noexcept
    {
        try {
            static_cast<void>(cleanupManagedProcessFixture(backend_, identity_));
        } catch (...) {
        }
    }

    ManagedProcessFixtureCleanup(const ManagedProcessFixtureCleanup&) = delete;
    ManagedProcessFixtureCleanup& operator=(const ManagedProcessFixtureCleanup&) = delete;

private:
    IManagedProcessBackend& backend_;
    ManagedProcessIdentity identity_;
};

struct ManagedProcessCacheSpec {
    std::string operation{"status"};
    std::string executable{"C:/praktor-cache-probe/worker.exe"};
    std::vector<std::string> arguments{"--mode", "baseline"};
    std::string working_directory{"C:/praktor-cache-probe"};
    std::string image_name;
    int startup_timeout_ms{1000};
    int stop_timeout_ms{1000};
    bool force_terminate{false};
};

std::string managedProcessCacheWorkflow(const ManagedProcessCacheSpec& spec)
{
    std::ostringstream yaml;
    yaml << "tasks:\n"
         << "  - name: cached_process_status\n"
         << "    sources: [source.txt]\n"
         << "    generates: [marker.txt]\n"
         << "    managed_process:\n"
         << "      operation: " << spec.operation << "\n"
         << "      executable: \"" << spec.executable << "\"\n"
         << "      arguments:\n";
    for (const auto& argument : spec.arguments) {
        yaml << "        - \"" << argument << "\"\n";
    }
    yaml << "      working_directory: \"" << spec.working_directory << "\"\n"
         << "      identity:\n"
         << "        image_name: \"" << spec.image_name << "\"\n"
         << "      startup_timeout_ms: " << spec.startup_timeout_ms << "\n"
         << "      stop_timeout_ms: " << spec.stop_timeout_ms << "\n"
         << "      force_terminate: "
         << (spec.force_terminate ? "true" : "false") << "\n";
    return yaml.str();
}
#endif

}

TEST_CASE("run simple workflow")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "out.txt";

    writeFile(workflow_path, R"(
tasks:
  - name: write
    command: "echo hello > out.txt"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    auto content = FileUtils::readFile("out.txt" );
    CHECK(content.find("hello") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("when condition skips task")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, R"(
variables:
  FLAG: off

tasks:
  - name: step
    when: "$FLAG == 'on'"
    command: "echo should-not-run"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
}

TEST_CASE("uses executes nested workflow and exposes outputs")
{
    auto dir = createTempDir();
    auto reusable_path = dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";

    writeFile(reusable_path, R"(
tasks:
  - name: produce
    command: "echo nested"
    output_format: text
    script: |
      ctx.output("result", "nested");
)"
    );

    writeFile(workflow_path, std::string("tasks:\n  - name: call\n    uses: ") + reusable_path.string() + R"(
  - name: check
    depends_on: [call]
    command: "echo checking"
    script: |
      var result = ctx.get("tasks.call.outputs.result");
      log.info("Got result: " + result);
      ctx.set("verified", result);
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
}

TEST_CASE("uses exports nested task summary")
{
    auto dir = createTempDir();
    auto reusable_path = dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";

    writeFile(reusable_path, R"(
tasks:
  - name: first
    command: "echo one"
    script: |
      ctx.output("result", "one");

  - name: second
    depends_on: [first]
    command: "echo two"
    script: |
      ctx.output("result", "two");
)"
    );

    writeFile(workflow_path, std::string("tasks:\n  - name: call\n    uses: ") + reusable_path.string() + R"(
  - name: inspect
    depends_on: [call]
    command: "echo checking"
    script: |
      var first = ctx.get("tasks.call.outputs.nested_tasks.first.outputs.result");
      var second = ctx.get("tasks.call.outputs.nested_tasks.second.outputs.result");
      ctx.set("nested_first", first);
      ctx.set("nested_second", second);
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
}

TEST_CASE("uses resolves nested workflow relative paths from declaring files")
{
    auto dir = createTempDir();
    auto nested_dir = dir / "nested";
    auto nested_workdir = nested_dir / "work";
    auto misplaced_workdir = dir / "work";
    std::filesystem::create_directories(nested_workdir);

    auto reusable_path = nested_dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";
    auto nested_output = nested_workdir / "out.txt";
    auto misplaced_output = misplaced_workdir / "out.txt";

    writeFile(reusable_path, R"(
tasks:
  - name: write
    working_dir: ./work
    command: "echo nested > out.txt"
)"
    );

    writeFile(workflow_path, R"(
tasks:
  - name: call
    uses: ./nested/reusable.yml
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(nested_output));
    CHECK_FALSE(std::filesystem::exists(misplaced_output));

    auto content = FileUtils::readFile(nested_output.string());
    CHECK(content.find("nested") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("uses rejects alias collision with nested task names")
{
    auto dir = createTempDir();
    auto reusable_path = dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";

    writeFile(reusable_path, R"(
tasks:
  - name: call
    command: "echo inner"
)"
    );

    writeFile(workflow_path, std::string("tasks:\n  - name: call\n    uses: ") + reusable_path.string() + "\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
}

TEST_CASE("failing command marks workflow failed")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: crash
    command: "exit 42"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
}

TEST_CASE("command tasks honor working_dir")
{
    auto dir = createTempDir();
    auto workdir = dir / "build";
    std::filesystem::create_directories(workdir);
    auto workflow_path = dir / "workflow.yml";
    auto output_path = workdir / "out.txt";

    writeFile(workflow_path, R"(
tasks:
  - name: write
    working_dir: ./build
    command: "echo hello > out.txt"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("hello") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("command and explicit actions expose equivalent structured outputs")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path,
              "tasks:\n"
              "  - name: via_command\n"
              "    command: \"" + jsonEchoCommand("{\\\"source\\\":\\\"command\\\",\\\"value\\\":7}") + "\"\n"
              "    output_format: json\n"
              "\n"
              "  - name: via_bt\n"
              "    sequence:\n"
              "      - shell:\n"
              "          cmd: \"" + jsonEchoCommand("{\\\"source\\\":\\\"bt\\\",\\\"value\\\":7}") + "\"\n"
              "          output_key: stdout\n"
              "          stderr_key: stderr\n"
              "          exit_code_key: exit_code\n"
              "      - parse_json:\n"
              "          input_key: stdout\n"
              "          output_key: data\n"
              "\n"
              "  - name: inspect\n"
              "    depends_on: [via_command, via_bt]\n"
              "    command: \"echo inspect\"\n"
              "    script: |\n"
              "      var cmdValue = ctx.get(\"tasks.via_command.outputs.data.value\");\n"
              "      var btValue = ctx.get(\"tasks.via_bt.outputs.data.value\");\n"
              "      var cmdSource = ctx.get(\"tasks.via_command.outputs.data.source\");\n"
              "      var btSource = ctx.get(\"tasks.via_bt.outputs.data.source\");\n"
              "      if (cmdValue != 7) fail(\"command output missing expected value\");\n"
              "      if (btValue != 7) fail(\"bt output missing expected value\");\n"
              "      if (cmdSource != \"command\") fail(\"command source mismatch\");\n"
              "      if (btSource != \"bt\") fail(\"bt source mismatch\");\n"
              "      ctx.output(\"matched\", true);\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    std::filesystem::remove_all(dir);
}

TEST_CASE("program tasks run raw process with args env stdin and structured output")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto script_path = dir / "program_probe.ps1";
#ifndef _WIN32
    script_path = dir / "program_probe.sh";
#endif
    writeFile(script_path, programProbeScript());

    writeFile(workflow_path,
              std::string("tasks:\n"
              "  - name: via_program\n") +
              programRunnerYaml(script_path) +
              "    stdin: payload\n"
              "    env:\n"
              "      PRAKTOR_PROGRAM_ENV: raw\n"
              "    output_format: json\n"
              "\n"
              "  - name: inspect\n"
              "    depends_on: [via_program]\n"
              "    script: |\n"
              "      var source = ctx.get(\"tasks.via_program.outputs.data.source\");\n"
              "      var value = ctx.get(\"tasks.via_program.outputs.data.value\");\n"
              "      if (source != \"program\") fail(\"program source mismatch\");\n"
              "      if (value != 7) fail(\"program value mismatch\");\n"
              "      ctx.output(\"matched\", true);\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    std::filesystem::remove_all(dir);
}

TEST_CASE("script-only tasks run after dependencies")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto report_path = dir / "report.txt";

    writeFile(workflow_path,
              "variables:\n"
              "  BUILD_TARGET: all\n"
              "\n"
              "tasks:\n"
              "  - name: build\n"
              "    command: \"echo build\"\n"
              "\n"
              "  - name: finish_report\n"
              "    depends_on: [build]\n"
              "    script: |\n"
              "      ctx.output(\"report_message\", \"Build of target \" + ctx.get(\"BUILD_TARGET\") + \" completed successfully.\");\n"
              "\n"
              "  - name: verify_report\n"
              "    depends_on: [finish_report]\n"
              "    command: \"echo {{ tasks.finish_report.outputs.report_message }} > " + report_path.generic_string() + "\"\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(report_path));
    auto report = FileUtils::readFile(report_path.string());
    CHECK(report.find("Build of target all completed successfully.") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("failure triggers receive failure context")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "failure.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo name={{ failed_task_name }} type={{ failed_task_type }} > )") + output_path.generic_string() + R"("

  - name: crash
    command: "definitely_missing_command_12345"
    triggers:
      on_failure: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK_FALSE(content.empty());
    CHECK(content.find("name=crash") != std::string::npos);
    CHECK(content.find("type=command") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("failure context preserves declared command runner")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "failure_type.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo {{ failed_task_type }} > )") + output_path.generic_string() + R"("

  - name: crash
    command: "definitely_missing_command_67890"
    triggers:
      on_failure: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("command") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("explicit actions tasks use the same failure trigger lifecycle")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "bt_failure.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo name={{ failed_task_name }} type={{ failed_task_type }} status={{ tasks.tree_fail.status }} > )") + output_path.generic_string() + R"("

  - name: tree_fail
    sequence:
      - shell:
          cmd: "definitely_missing_command_bt_24680"
          output_key: stdout
          stderr_key: stderr
          exit_code_key: exit_code
    triggers:
      on_failure: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("name=tree_fail") != std::string::npos);
    CHECK(content.find("type=actions") != std::string::npos);
    CHECK(content.find("status=failed") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("dynamic_tasks failure triggers expose inner failed task")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "dynamic_failure.txt";

    writeFile(workflow_path,
              "tasks:\n"
              "  - name: notify\n"
              "    command: \"echo outer={{ failed_task_name }} inner={{ failed_inner_task_name }} type={{ failed_inner_task_type }} > " + output_path.generic_string() + "\"\n"
              "\n"
              "  - name: discover\n"
              "    command: \"" + jsonEchoCommand("[\\\"ok\\\",\\\"fail\\\"]") + "\"\n"
              "    output_format: json\n"
              "\n"
              "  - name: deploy_all\n"
              "    depends_on: [discover]\n"
              "    dynamic_tasks:\n"
              "      items_variable: \"{{ tasks.discover.outputs.data }}\"\n"
              "      template:\n"
              "        name: \"deploy_{{ item }}\"\n"
              "        command: \"" + failOnMatchCommand("{{ item }}", "fail") + "\"\n"
              "    triggers:\n"
              "      on_failure: [notify]\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("outer=deploy_all") != std::string::npos);
    CHECK(content.find("inner=deploy_fail") != std::string::npos);
    CHECK(content.find("type=command") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("uses failure triggers expose inner failed task")
{
    auto dir = createTempDir();
    auto reusable_path = dir / "reusable.yml";
    auto workflow_path = dir / "main.yml";
    auto output_path = dir / "uses_failure.txt";

    writeFile(reusable_path, R"(
tasks:
  - name: inner_crash
    command: "definitely_missing_command_uses_12345"
)"
    );

    const std::string workflow_content =
        std::string(R"YML(
tasks:
  - name: notify
    command: "echo outer={{ failed_task_name }} inner={{ failed_inner_task_name }} type={{ failed_inner_task_type }} > )YML") +
        output_path.generic_string() +
        std::string(R"YML("

  - name: call
    uses: )YML") +
        reusable_path.string() +
        std::string(R"YML(
    triggers:
      on_failure: [notify]
)YML");

    writeFile(workflow_path, workflow_content);

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("outer=call") != std::string::npos);
    CHECK(content.find("inner=inner_crash") != std::string::npos);
    CHECK(content.find("type=command") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("trigger handlers observe finalized task status")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "trigger_status.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo {{ tasks.crash.status }} > )") + output_path.generic_string() + R"("

  - name: crash
    command: "definitely_missing_command_13579"
    triggers:
      on_failure: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("failed") != std::string::npos);
    CHECK(content.find("running") == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("finally runs through on_complete even when DAG stops on failure")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto cleanup_path = dir / "cleanup.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: cleanup
    depends_on: [crash]
    command: "echo cleanup > )") + cleanup_path.generic_string() + R"("

  - name: crash
    command: "definitely_missing_command_24680"
    finally: cleanup
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(cleanup_path));
    auto content = FileUtils::readFile(cleanup_path.string());
    CHECK(content.find("cleanup") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("trigger handlers are not auto-scheduled as regular root tasks")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "trigger_count.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo hit >> )") + output_path.generic_string() + R"("

  - name: build
    command: "echo done"
    triggers:
      on_success: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    auto first = content.find("hit");
    REQUIRE(first != std::string::npos);
    CHECK(content.find("hit", first + 1) == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("failed trigger handlers fail the workflow without rewriting the source task status")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: notify
    command: "definitely_missing_trigger_command_86420"

  - name: build
    command: "echo done"
    triggers:
      on_success: [notify]
)"
    );

    auto workflow = TaskParser::parseFile(workflow_path.string());
    auto graph = TaskParser::buildGraph(workflow);
    WorkflowContext context;
    WorkflowExecutor executor(graph, workflow.tasks, {}, 1, false);
    executor.execute(context);

    CHECK(context.getValueOrDefault<std::string>("workflow_status", "") == "failed");
    CHECK(context.getTaskStatus("build") == "success");
    CHECK(context.getTaskStatus("notify") == "failed");
    std::filesystem::remove_all(dir);
}

TEST_CASE("each tasks fire triggers once after aggregate completion")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "each_trigger_count.txt";

    writeFile(workflow_path, std::string(R"(
tasks:
  - name: notify
    command: "echo hit >> )") + output_path.generic_string() + R"("

  - name: fanout
    each:
      items: ["a", "b", "c"]
      as: item
    command: "echo {{ item }}"
    triggers:
      on_success: [notify]
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    auto first = content.find("hit");
    REQUIRE(first != std::string::npos);
    CHECK(content.find("hit", first + 1) == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("each failures fire on_failure once with aggregate task context")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "each_failure.txt";

    const std::string workflow_content =
        "tasks:\n"
        "  - name: notify\n"
        "    command: \"echo name={{ failed_task_name }} status={{ tasks.fanout.status }} >> " + output_path.generic_string() + "\"\n"
        "\n"
        "  - name: fanout\n"
        "    each:\n"
        "      items: [\"ok\", \"fail\", \"later\"]\n"
        "      as: item\n"
        "    command: \"" + failOnMatchCommand("{{ item }}", "fail") + "\"\n"
        "    triggers:\n"
        "      on_failure: [notify]\n";

    writeFile(workflow_path, workflow_content);

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    auto first = content.find("name=fanout");
    REQUIRE(first != std::string::npos);
    CHECK(content.find("status=failed") != std::string::npos);
    CHECK(content.find("name=fanout", first + 1) == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("trigger handlers honor unmet dependencies before execution")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "trigger_chain.txt";

    const std::string workflow_content =
        "tasks:\n"
        "  - name: prepare\n"
        "    command: \"echo prepare >> " + output_path.generic_string() + "\"\n"
        "\n"
        "  - name: notify\n"
        "    depends_on: [prepare]\n"
        "    command: \"echo notify >> " + output_path.generic_string() + "\"\n"
        "\n"
        "  - name: build\n"
        "    command: \"echo done\"\n"
        "    triggers:\n"
        "      on_success: [notify]\n";

    writeFile(workflow_path, workflow_content);

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));

    auto output_content = FileUtils::readFile(output_path.string());
    auto prepare_pos = output_content.find("prepare");
    auto notify_pos = output_content.find("notify");

    REQUIRE(prepare_pos != std::string::npos);
    REQUIRE(notify_pos != std::string::npos);
    CHECK(prepare_pos < notify_pos);
    CHECK(output_content.find("prepare", prepare_pos + 1) == std::string::npos);
    CHECK(output_content.find("notify", notify_pos + 1) == std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("dynamic_tasks reject generated names that collide with static tasks")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "static_notify.txt";

    writeFile(workflow_path,
              "tasks:\n"
              "  - name: discover_items\n"
              "    command: \"" + jsonEchoCommand("[\\\"prod\\\"]") + "\"\n"
              "    output_format: json\n"
              "\n"
              "  - name: notify_prod\n"
              "    command: \"echo static > static_notify.txt\"\n"
              "\n"
              "  - name: finalize\n"
              "    depends_on: [deploy_all]\n"
              "    command: \"echo done\"\n"
              "    triggers:\n"
              "      on_success: [notify_prod]\n"
              "\n"
              "  - name: deploy_all\n"
              "    depends_on: [discover_items]\n"
              "    dynamic_tasks:\n"
              "      items_variable: \"{{ tasks.discover_items.outputs.data }}\"\n"
              "      template:\n"
              "        name: \"notify_{{ item }}\"\n"
              "        command: \"echo {{ item }}\"\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    CHECK_FALSE(std::filesystem::exists(output_path));
    std::filesystem::remove_all(dir);
}

TEST_CASE("workflow env is exposed through ctx.env")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "env.txt";

    writeFile(workflow_path, R"(
env:
  API_TOKEN: "scoped-token"

tasks:
  - name: capture
    script: |
      ctx.output("token", ctx.get("env.API_TOKEN"));

  - name: write
    depends_on: [capture]
    working_dir: .
    command: "echo {{ tasks.capture.outputs.token }} > env.txt"
)"
    );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));
    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("scoped-token") != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("workflow env does not leak into the host process")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "workflow.yml";
    auto output_path = dir / "scope.txt";
    const std::string env_name = "PRAKTOR_TEST_SCOPE";

    setProcessEnv(env_name, "outside");

    writeFile(workflow_path,
        "env:\n"
        "  PRAKTOR_TEST_SCOPE: \"inside\"\n"
        "\n"
        "tasks:\n"
        "  - name: write\n"
        "    working_dir: .\n"
        "    command: \"echo " + shellEnvRef(env_name) + " > scope.txt\"\n");

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    REQUIRE(std::filesystem::exists(output_path));

    auto content = FileUtils::readFile(output_path.string());
    CHECK(content.find("inside") != std::string::npos);

    const char* current = std::getenv(env_name.c_str());
    REQUIRE(current != nullptr);
    CHECK(std::string(current) == "outside");

    std::filesystem::remove_all(dir);
}

TEST_CASE("workflow runner preserves structured inputs and returns task results")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "structured-input.yml";

    writeFile(workflow_path, R"(
tasks:
  - name: inspect
    script: |
      if (ctx.get("payload.name") != "demo") fail("nested name was not preserved");
      if (ctx.get("payload.count") != 7) fail("nested count was not preserved");
      ctx.output("count", ctx.get("payload.count"));
)" );

    WorkflowInputs inputs;
    inputs["payload"] = WorkflowValue::parse(R"({"name":"demo","count":7})");
    WorkflowRunner runner(workflow_path.string(), std::move(inputs));

    const auto result = runner.execute();
    REQUIRE(result.success);
    CHECK(result.value["workflow_status"].as<std::string>() == "success");
    CHECK(result.value["tasks"]["inspect"]["status"].as<std::string>() == "success");
    CHECK(result.value["tasks"]["inspect"]["outputs"]["count"].as<int>() == 7);
    CHECK_FALSE(result.value.contains("error"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("dynamic trigger cycles fail fast instead of recursing forever")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "trigger-cycle.yml";

    // build and validate_request re-trigger each other through dynamic
    // references, so the cycle is invisible to parse-time validation. The
    // runtime trigger-depth guard must turn this into a controlled workflow
    // failure instead of unbounded recursion (stack overflow).
    writeFile(workflow_path, R"(
variables:
  NEXT_B: build
  NEXT_A: validate_request
tasks:
  - name: build
    command: "echo build-done"
    triggers:
      on_complete: ["{{ NEXT_A }}"]
  - name: validate_request
    command: "echo validate-done"
    triggers:
      on_complete: ["{{ NEXT_B }}"]
)" );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE_FALSE(runner.run());
    std::filesystem::remove_all(dir);
}

TEST_CASE("multi-hop trigger chains complete without being mistaken for cycles")
{
    auto dir = createTempDir();
    auto workflow_path = dir / "trigger-chain.yml";

    // A terminating chain where each task triggers the next; only t1 is a
    // regular root, the rest are trigger-only. Depth stays well below the
    // guard limit and the workflow must succeed.
    writeFile(workflow_path, R"(
tasks:
  - name: t1
    command: "echo 1"
    triggers:
      on_success: [t2]
  - name: t2
    command: "echo 2"
    triggers:
      on_success: [t3]
  - name: t3
    command: "echo 3"
    triggers:
      on_success: [t4]
  - name: t4
    command: "echo 4"
    triggers:
      on_success: [t5]
  - name: t5
    command: "echo 5"
)" );

    WorkflowRunner runner(workflow_path.string());
    REQUIRE(runner.run());
    std::filesystem::remove_all(dir);
}

TEST_CASE("system actions execute through WorkflowRunner and preserve failure metadata")
{
#ifdef _WIN32
    const auto workflow_path = std::filesystem::path(__FILE__).parent_path()
        / "workflows" / "system-actions.yml";
    const auto fixture_path = std::filesystem::path(PRAKTOR_MANAGED_PROCESS_FIXTURE);
    REQUIRE(std::filesystem::exists(workflow_path));
    REQUIRE(std::filesystem::exists(fixture_path));

    auto backend = Praktor::System::createManagedProcessBackend();
    REQUIRE(backend != nullptr);
    ManagedProcessIdentity identity;
    identity.image_name = fixture_path.filename().string();
    const auto setup_cleanup = cleanupManagedProcessFixture(*backend, identity);
    INFO(setup_cleanup.native_error << ":" << setup_cleanup.message);
    REQUIRE(setup_cleanup.ok);
    ManagedProcessFixtureCleanup cleanup(*backend, identity);

    WorkflowInputs inputs;
    inputs["fixture_executable"] = fixture_path.generic_string();
    inputs["fixture_working_directory"] = fixture_path.parent_path().generic_string();
    inputs["fixture_image_name"] = identity.image_name;
    inputs["invalid_service_profile"] = "praktor_missing_profile";

    WorkflowRunner runner(workflow_path.string(), std::move(inputs));
    const auto result = runner.execute();

    CHECK_FALSE(result.success);
    CHECK(result.value["workflow_status"].as<std::string>() == "failed");
    CHECK(result.error_message == "Workflow execution failed");
    const WorkflowValue tasks = result.value["tasks"];

    const auto check_process = [&](const std::string& name,
                                   const std::string& operation,
                                   const std::string& state,
                                   bool changed) {
        INFO(name);
        CHECK(tasks[name]["status"].as<std::string>() == "success");
        const WorkflowValue outputs = tasks[name]["outputs"];
        CHECK(outputs["image_name"].as<std::string>() == identity.image_name);
        CHECK(outputs["operation"].as<std::string>() == operation);
        CHECK(outputs["state"].as<std::string>() == state);
        CHECK(outputs["changed"].as<bool>() == changed);
        CHECK(outputs["duration_ms"].as<std::int64_t>() >= 0);
        CHECK(outputs["pid"].as<std::int64_t>() >= 0);
    };

    check_process("process_initial_status", "status", "not_running", false);
    check_process("process_start", "start", "running", true);
    CHECK(tasks["process_start"]["outputs"]["pid"].as<std::int64_t>() > 0);
    check_process("process_running_status", "status", "running", false);
    check_process("process_restart", "restart", "running", true);
    CHECK(tasks["process_restart"]["outputs"]["pid"].as<std::int64_t>() > 0);
    check_process("process_stop", "stop", "not_running", true);
    check_process("process_final_status", "status", "not_running", false);

    CHECK(tasks["service_profile_failure"]["status"].as<std::string>() == "failed");
    const WorkflowValue service_outputs = tasks["service_profile_failure"]["outputs"];
    CHECK(service_outputs["name"].as<std::string>() == "PraktorContractService");
    CHECK(service_outputs["operation"].as<std::string>() == "status");
    CHECK(service_outputs["state"].as<std::string>().empty());
    CHECK_FALSE(service_outputs["changed"].as<bool>());
    CHECK(service_outputs["duration_ms"].as<std::int64_t>() >= 0);

    CHECK(tasks["capture_service_failure"]["status"].as<std::string>() == "success");
    const WorkflowValue failure = tasks["capture_service_failure"]["outputs"];
    CHECK(failure["task_name"].as<std::string>() == "service_profile_failure");
    CHECK(failure["task_type"].as<std::string>() == "service");
    CHECK(failure["error_code"].as<std::string>() == "service_state_failed");
    CHECK(failure["error_phase"].as<std::string>() == "profile");
    CHECK(failure["error_details"]["name"].as<std::string>() ==
          "PraktorContractService");
    CHECK(failure["error_details"]["profile"].as<std::string>() ==
          "praktor_missing_profile");
    CHECK(failure["error_details"]["operation"].as<std::string>() == "status");
    CHECK(failure["failed_outputs"]["operation"].as<std::string>() == "status");

    const auto final_cleanup = cleanupManagedProcessFixture(*backend, identity);
    INFO(final_cleanup.native_error << ":" << final_cleanup.message);
    REQUIRE(final_cleanup.ok);
#else
    SKIP("managed_process WorkflowRunner integration is Windows-only");
#endif
}

TEST_CASE("managed process cache invalidates when every action parameter changes")
{
#ifdef _WIN32
    const auto dir = createTempDir();
    const auto workflow_path = dir / "managed-process-cache.yml";
    writeFile(dir / "source.txt", "stable source");
    writeFile(dir / "marker.txt", "existing output");

    ManagedProcessCacheSpec baseline;
    baseline.image_name = "praktor_cache_probe_" + dir.filename().string() + ".exe";

    std::vector<std::pair<std::string, ManagedProcessCacheSpec>> variants;
    auto add_variant = [&](std::string name, auto mutate) {
        auto variant = baseline;
        mutate(variant);
        variants.emplace_back(std::move(name), std::move(variant));
    };
    add_variant("operation", [](auto& value) { value.operation = "stop"; });
    add_variant("executable", [](auto& value) { value.executable += ".changed"; });
    add_variant("arguments", [](auto& value) { value.arguments.push_back("changed"); });
    add_variant("working_directory", [](auto& value) { value.working_directory += "/changed"; });
    add_variant("identity.image_name", [](auto& value) {
        value.image_name = "praktor_cache_probe_changed.exe";
    });
    add_variant("startup_timeout_ms", [](auto& value) { value.startup_timeout_ms = 1001; });
    add_variant("stop_timeout_ms", [](auto& value) { value.stop_timeout_ms = 1001; });
    add_variant("force_terminate", [](auto& value) { value.force_terminate = true; });

    for (const auto& [field, variant] : variants) {
        INFO("changed field: " << field);
        std::filesystem::remove(dir / ".praktor_cache");
        writeFile(workflow_path, managedProcessCacheWorkflow(baseline));

        const auto first = WorkflowRunner(workflow_path.string()).execute();
        REQUIRE(first.success);
        CHECK(first.value["tasks"]["cached_process_status"]["status"].as<std::string>() ==
              "success");

        const auto cached = WorkflowRunner(workflow_path.string()).execute();
        REQUIRE(cached.success);
        CHECK(cached.value["tasks"]["cached_process_status"]["status"].as<std::string>() ==
              "skipped");

        writeFile(workflow_path, managedProcessCacheWorkflow(variant));
        const auto changed = WorkflowRunner(workflow_path.string()).execute();
        REQUIRE(changed.success);
        CHECK(changed.value["tasks"]["cached_process_status"]["status"].as<std::string>() ==
              "success");
    }

    std::filesystem::remove_all(dir);
#else
    SKIP("managed_process cache integration is Windows-only");
#endif
}

TEST_CASE("service action hash changes for every cache-relevant parameter")
{
    Task baseline;
    baseline.name = "cached_service_status";
    baseline.action = TaskAction::Service;
    baseline.declared_runner = "service";
    ServiceParams params;
    params.operation = SystemOperation::Status;
    params.name = "PraktorService";
    params.profile = "windows_scm";
    params.arguments = {"--mode", "baseline"};
    params.timeout_ms = 30000;
    params.poll_interval_ms = 200;
    baseline.specifics = params;

    const std::string baseline_hash =
        Praktor::Execution::Internal::computeTaskActionHash(baseline);
    std::vector<std::pair<std::string, ServiceParams>> variants;
    auto add_variant = [&](std::string name, auto mutate) {
        auto variant = params;
        mutate(variant);
        variants.emplace_back(std::move(name), std::move(variant));
    };
    add_variant("operation", [](auto& value) {
        value.operation = SystemOperation::Start;
    });
    add_variant("name", [](auto& value) { value.name += "Changed"; });
    add_variant("profile", [](auto& value) { value.profile += "_changed"; });
    add_variant("arguments", [](auto& value) {
        value.arguments.push_back("changed");
    });
    add_variant("timeout_ms", [](auto& value) { ++value.timeout_ms; });
    add_variant("poll_interval_ms", [](auto& value) {
        ++value.poll_interval_ms;
    });

    for (const auto& [field, variant] : variants) {
        INFO("changed field: " << field);
        Task changed = baseline;
        changed.specifics = variant;
        CHECK(Praktor::Execution::Internal::computeTaskActionHash(changed) !=
              baseline_hash);
    }
}
