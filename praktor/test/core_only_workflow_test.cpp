#include "workflow_runner.hpp"
#include "executors/declarative_tree_executor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path createCoreOnlyTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-core-only-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << text;
}

} // namespace

static_assert(PRAKTOR_SCRIPT_ENGINE_ENABLED == 0,
              "core_only_workflow_test must only build with scripts disabled");

TEST_CASE("workflow value converts integral decimal JSON numbers") {
    const auto value = WorkflowValue::parse(R"({"count":7.0})");
    CHECK(value["count"].as<int>() == 7);
}

TEST_CASE("core-only build executes workflows without script tasks") {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "command.yml";

    writeText(workflow, R"(
tasks:
  - name: hello
    command: "echo core-only"
)");

    WorkflowRunner runner(workflow.string());
    const auto result = runner.execute();

    CHECK(result.success);
    CHECK(result.value["workflow_status"].as<std::string>() == "success");

    std::filesystem::remove_all(dir);
}

TEST_CASE("core-only build rejects script workflows before scheduling") {
    const auto dir = createCoreOnlyTempDir();
    const auto side_effect = dir / "should-not-exist.txt";
    const auto workflow = dir / "script.yml";

#ifdef _WIN32
    const std::string first_command = "cmd /C echo should-not-run > \"" +
                                      side_effect.string() + "\"";
#else
    const std::string first_command = "sh -c 'echo should-not-run > \"" +
                                      side_effect.string() + "\"'";
#endif

    writeText(workflow,
              "tasks:\n"
              "  - name: first\n"
              "    command: |-\n"
              "      " + first_command + "\n"
              "\n"
              "  - name: script_later\n"
              "    depends_on: [first]\n"
              "    script: |\n"
              "      ctx.output(\"value\", 1);\n");

    WorkflowRunner runner(workflow.string());
    const auto result = runner.execute();

    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("ENABLE_SCRIPT_ENGINE=OFF") != std::string::npos);
    CHECK(result.value["workflow_status"].as<std::string>() == "failed");
    CHECK(result.value["tasks"].size() == 0);
    CHECK_FALSE(std::filesystem::exists(side_effect));

    std::filesystem::remove_all(dir);
}

namespace {

Task makeCoreOnlyActionTask(const std::string& name, OrchNode root) {
    Task task;
    task.name = name;
    task.action = TaskAction::Orch;
    OrchParams params;
    params.root = std::move(root);
    task.specifics = std::move(params);
    return task;
}

} // namespace

TEST_CASE("core-only default action backend remains legacy") {
    // The production factory must retain its current default while the C11
    // integration gate is still experimental.
    WorkflowContext context;
    OrchNode node;
    node.type = "SetVariable";
    node.params["key"] = "answer";
    node.params["value"] = "legacy-default";
    auto task = makeCoreOnlyActionTask("default_backend", std::move(node));
    Praktor::Execution::DeclarativeTreeExecutor executor;
    const auto result = executor.execute(task, context);
    REQUIRE(result.success);
    const auto output = context.getValueByPath("tasks.default_backend.outputs.answer");
    REQUIRE(output.is_string());
    CHECK(output.as<std::string>() == "legacy-default");
}

#if PRAKTOR_C11_ACTIONS_ENABLED

TEST_CASE("opt-in C11 action backend preserves workflow context and task output") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    for (const auto backend : {ActionTreeBackend::Legacy, ActionTreeBackend::C11}) {
        WorkflowContext context;
        context.setValue("BUILD_DIR", "build");
        context.setValue("BUILD_TYPE", "Release");

        OrchNode root;
        root.type = "Sequence";
        OrchNode template_step;
        template_step.type = "SetVariable";
        template_step.params["key"] = "description";
        template_step.params["value"] = "{{ BUILD_DIR }}-{{ BUILD_TYPE }}";
        OrchNode blackboard_step;
        blackboard_step.type = "SetVariable";
        blackboard_step.params["key"] = "result";
        blackboard_step.params["value"] = "{description}";
        root.children.push_back(std::move(template_step));
        root.children.push_back(std::move(blackboard_step));
        auto task = makeCoreOnlyActionTask("shared_contract", std::move(root));

        DeclarativeTreeExecutor executor({}, backend);
        const auto result = executor.execute(task, context);
        REQUIRE(result.success);
        CHECK(result.error_message.empty());
        CHECK(result.stdout_data.empty());
        CHECK(result.stderr_data.empty());

        const auto description = context.getValueByPath(
            "tasks.shared_contract.outputs.description");
        const auto resolved = context.getValueByPath(
            "tasks.shared_contract.outputs.result");
        const auto status = context.getValueByPath(
            "tasks.shared_contract.outputs.node_status");
        REQUIRE(description.is_string());
        REQUIRE(resolved.is_string());
        REQUIRE(status.is_string());
        CHECK(description.as<std::string>() == "build-Release");
        CHECK(resolved.as<std::string>() == "build-Release");
        CHECK(status.as<std::string>() == "SUCCESS");
    }
}

TEST_CASE("opt-in C11 shell result reaches TaskResult without legacy fallback") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    for (const auto backend : {ActionTreeBackend::Legacy, ActionTreeBackend::C11}) {
        WorkflowContext context;
        OrchNode shell;
        shell.type = "Shell";
        shell.params["cmd"] = "exit 7";
        shell.params["timeout"] = "5000";
        shell.params["stream_output"] = "false";
        auto task = makeCoreOnlyActionTask("shell_failure", std::move(shell));
        DeclarativeTreeExecutor executor({}, backend);
        const auto result = executor.execute(task, context);
        CHECK_FALSE(result.success);
        CHECK(result.exit_code == 7);
        CHECK_FALSE(result.error_message.empty());
        const auto exit_code = context.getValueByPath(
            "tasks.shell_failure.outputs.exit_code");
        REQUIRE(exit_code.is_string());
        CHECK(exit_code.as<std::string>() == "7");
    }
}

TEST_CASE("opt-in C11 successful Shell output reaches workflow context") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    for (const auto backend : {ActionTreeBackend::Legacy, ActionTreeBackend::C11}) {
        WorkflowContext context;
        OrchNode shell;
        shell.type = "Shell";
        shell.params["cmd"] = "echo workflow-c11-bridge";
        shell.params["timeout"] = "5000";
        shell.params["stream_output"] = "false";
        auto task = makeCoreOnlyActionTask("shell_success", std::move(shell));

        DeclarativeTreeExecutor executor({}, backend);
        const auto result = executor.execute(task, context);
        REQUIRE(result.success);
        CHECK(result.exit_code == 0);
        CHECK(result.stdout_data.find("workflow-c11-bridge") != std::string::npos);
        const auto stdout_output = context.getValueByPath(
            "tasks.shell_success.outputs.stdout");
        REQUIRE(stdout_output.is_string());
        CHECK(stdout_output.as<std::string>().find("workflow-c11-bridge") !=
              std::string::npos);
    }
}

TEST_CASE("opt-in C11 task deadline cancels a delayed leaf") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    WorkflowContext context;
    OrchNode delay;
    delay.type = "Delay";
    delay.params["delay_ms"] = "2000";
    OrchNode leaf;
    leaf.type = "SetVariable";
    leaf.params["key"] = "must_not_publish";
    leaf.params["value"] = "late";
    delay.children.push_back(std::move(leaf));
    auto task = makeCoreOnlyActionTask("deadline_guard", std::move(delay));
    task.timeout = "20";

    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11);
    const auto started = std::chrono::steady_clock::now();
    const auto result = executor.execute(task, context);
    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("task deadline") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - started <
          std::chrono::seconds(3));
    CHECK(context.getValueByPath(
        "tasks.deadline_guard.outputs.must_not_publish").is_null());
}

TEST_CASE("opt-in C11 workflow rejects bad plan before leaf side effects") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    WorkflowContext context;
    OrchNode root;
    root.type = "Sequence";
    OrchNode first;
    first.type = "SetVariable";
    first.params["key"] = "should_not_publish";
    first.params["value"] = "side-effect";
    OrchNode invalid_retry;
    invalid_retry.type = "Retry";
    invalid_retry.params["num_attempts"] = "1.5";
    OrchNode retry_leaf;
    retry_leaf.type = "SetVariable";
    retry_leaf.params["key"] = "never_run";
    retry_leaf.params["value"] = "no";
    invalid_retry.children.push_back(std::move(retry_leaf));
    root.children.push_back(std::move(first));
    root.children.push_back(std::move(invalid_retry));

    auto task = makeCoreOnlyActionTask("reject_plan", std::move(root));
    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11);
    CHECK_THROWS_AS(executor.execute(task, context), std::invalid_argument);
    const auto absent = context.getValueByPath(
        "tasks.reject_plan.outputs.should_not_publish");
    CHECK(absent.is_null());
}

TEST_CASE("opt-in C11 respects cancellation before leaf entry") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    WorkflowContext context;
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    control->requestCancel();
    context.setExecutionControl(control);
    OrchNode node;
    node.type = "SetVariable";
    node.params["key"] = "not_executed";
    node.params["value"] = "side-effect";
    auto task = makeCoreOnlyActionTask("cancelled_action", std::move(node));

    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11);
    const auto result = executor.execute(task, context);
    CHECK_FALSE(result.success);
    CHECK(result.error_message.find("cancelled") != std::string::npos);
    CHECK(context.getValueByPath(
        "tasks.cancelled_action.outputs.not_executed").is_null());
}

TEST_CASE("opt-in C11 Parallel fails closed without branch-host hooks") {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    WorkflowContext context;
    OrchNode root;
    root.type = "Parallel";
    for (const std::string& key : {"a", "b"}) {
        OrchNode step;
        step.type = "SetVariable";
        step.params["key"] = key;
        step.params["value"] = "1";
        root.children.push_back(std::move(step));
    }
    auto task = makeCoreOnlyActionTask("parallel_no_host", std::move(root));
    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11);
    CHECK_THROWS_AS(executor.execute(task, context), std::invalid_argument);
    CHECK(context.getValueByPath("tasks.parallel_no_host.outputs.a").is_null());
}

#else

TEST_CASE("C11 action backend selection fails closed when adapter is disabled") {
    WorkflowContext context;
    OrchNode node;
    node.type = "SetVariable";
    node.params["key"] = "not_executed";
    node.params["value"] = "side-effect";
    auto task = makeCoreOnlyActionTask("disabled_c11", std::move(node));
    Praktor::Execution::DeclarativeTreeExecutor executor(
        {}, Praktor::Execution::ActionTreeBackend::C11);
    CHECK_THROWS_AS(executor.execute(task, context), std::runtime_error);
    CHECK(context.getValueByPath(
        "tasks.disabled_c11.outputs.not_executed").is_null());
}

#endif
