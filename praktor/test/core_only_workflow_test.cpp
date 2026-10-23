#include "praktor_test.hpp"
#include "workflow_runner.hpp"
#include "executors/declarative_tree_executor.hpp"


#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#if PRAKTOR_C11_ACTIONS_ENABLED
#include <salts/process_retainer.h>
#include "execution/c11_workflow_process_owner.hpp"
#endif

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
    check((out.is_open()));
    out << text;
}

} // namespace

static_assert(PRAKTOR_SCRIPT_ENGINE_ENABLED == 0,
              "core_only_workflow_test must only build with scripts disabled");

[[maybe_unused]] static void test_workflow_value_converts_integral_decimal_json_numbers_1() {
    const auto value = WorkflowValue::parse(R"({"count":7.0})");
    check((value["count"].as<int>() == 7));
}

spec("workflow value converts integral decimal JSON numbers") {
    it("complete case") { test_workflow_value_converts_integral_decimal_json_numbers_1(); }
}

[[maybe_unused]] static void test_core_only_build_executes_workflows_without_script_tasks_2() {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "command.yml";

    writeText(workflow, R"(
tasks:
  - name: hello
    command: "echo core-only"
)");

    WorkflowRunner runner(workflow.string());
    const auto result = runner.execute();

    check((result.success));
    check((result.value["workflow_status"].as<std::string>() == "success"));

    std::filesystem::remove_all(dir);
}

spec("core-only build executes workflows without script tasks") {
    it("complete case") { test_core_only_build_executes_workflows_without_script_tasks_2(); }
}

[[maybe_unused]] static void test_core_only_build_rejects_script_workflows_before_scheduling_3() {
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

    check((!(result.success)));
    check((result.error_message.find("ENABLE_SCRIPT_ENGINE=OFF") != std::string::npos));
    check((result.value["workflow_status"].as<std::string>() == "failed"));
    check((result.value["tasks"].size() == 0));
    check((!(std::filesystem::exists(side_effect))));

    std::filesystem::remove_all(dir);
}

spec("core-only build rejects script workflows before scheduling") {
    it("complete case") { test_core_only_build_rejects_script_workflows_before_scheduling_3(); }
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

[[maybe_unused]] static void test_core_only_default_action_backend_remains_legacy_4() {
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
    check((result.success));
    const auto output = context.getValueByPath("tasks.default_backend.outputs.answer");
    check((output.is_string()));
    check((output.as<std::string>() == "legacy-default"));
}

spec("core-only default action backend remains legacy") {
    it("complete case") { test_core_only_default_action_backend_remains_legacy_4(); }
}

#if PRAKTOR_C11_ACTIONS_ENABLED

namespace {
struct ScopedWorkflowProcessOwner {
    cflow_process_retainer owner{};
    ScopedWorkflowProcessOwner() {
        if (cflow_process_retainer_init(&owner, 8u) != SALTS_OK)
            throw std::runtime_error("Process recovery owner initialization failed");
    }
    ~ScopedWorkflowProcessOwner() {
        if (owner.impl == nullptr) return;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            size_t reaped = 0u, pending = 0u;
            const int status = cflow_process_retainer_run_ready(
                &owner, 64u, &reaped, &pending);
            if (status == SALTS_OK && pending == 0u) break;
            if (status != SALTS_EBUSY) std::terminate();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (cflow_process_retainer_destroy(&owner) != SALTS_OK)
            std::terminate();
    }
};
} // namespace

[[maybe_unused]] static void test_c11_workflowprocessowner_executes_and_closes_only_after_owner_drain_5() {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "owner-workflow.yml";
    const auto marker = dir / "owner-produced.txt";
#ifdef _WIN32
    const std::string cmd = "echo owner > \"" + marker.string() + "\"";
#else
    const std::string cmd = "printf owner > '" + marker.string() + "'";
#endif
    writeText(workflow, "tasks:\n"
                       "  - name: owner_task\n"
                       "    sequence:\n"
                       "      - shell:\n"
                       "          cmd: |-\n"
                       "            " + cmd + "\n");
    WorkflowRunner runner(workflow.string());
    Praktor::Execution::C11WorkflowProcessOwner host(1u);
    const auto result = host.execute(runner);
    check((result.success));
    check((std::filesystem::exists(marker)));
    std::size_t reaped = 0u, pending = 0u;
    check((host.runReady(64u, &reaped, &pending) == SALTS_OK));
    check((pending == 0u));
    check((host.close() == SALTS_OK));
    const auto denied = host.execute(runner);
    check((!(denied.success)));
    check((denied.error_message.find("closed") != std::string::npos));
    std::filesystem::remove_all(dir);
}

spec("C11 WorkflowProcessOwner executes and closes only after owner drain") {
    it("complete case") { test_c11_workflowprocessowner_executes_and_closes_only_after_owner_drain_5(); }
}

[[maybe_unused]] static void test_c11_workflowprocessowner_serializes_concurrent_execution_and_recovery_6() {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "serialized-owner.yml";
    const auto marker = dir / "started.txt";
#ifdef _WIN32
    const std::string cmd = "echo started > \"" + marker.string() +
        "\" & powershell.exe -NoProfile -NonInteractive -Command Start-Sleep -Seconds 30";
#else
    const std::string cmd = "printf started > '" + marker.string() + "'; exec sleep 30";
#endif
    writeText(workflow, "tasks:\n"
                       "  - name: serialized_task\n"
                       "    timeout: 15000\n"
                       "    sequence:\n"
                       "      - shell:\n"
                       "          cmd: |-\n"
                       "            " + cmd + "\n"
                       "          timeout: 30000\n");

    WorkflowRunner runner(workflow.string());
    Praktor::Execution::C11WorkflowProcessOwner host(1u);
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    WorkflowExecutionResult running_result{};
    std::thread running([&] {
        running_result = host.execute(runner, control);
    });
    struct JoinOnExit {
        std::thread& thread;
        ~JoinOnExit() {
            if (thread.joinable()) thread.join();
        }
    } join_guard{running};

    const auto started_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(8);
    while (!std::filesystem::exists(marker) &&
           std::chrono::steady_clock::now() < started_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool admitted = std::filesystem::exists(marker);
    std::size_t reaped = 0u, pending = 0u;
    if (admitted) {
        check((host.runReady(64u, &reaped, &pending) == SALTS_EBUSY));
        check((host.close() == SALTS_EBUSY));
        const auto rejected = host.execute(runner);
        check((!(rejected.success)));
        check((rejected.error_message.find("active driver") != std::string::npos));
    }
    control->requestCancel();
    running.join();
    check((admitted));
    check((!(running_result.success)));

    const auto finished_deadline = std::chrono::steady_clock::now() +
                                   std::chrono::seconds(8);
    for (;;) {
        const int status = host.runReady(64u, &reaped, &pending);
        check(((status == SALTS_OK || status == SALTS_EBUSY)));
        if (pending == 0u) break;
        check((std::chrono::steady_clock::now() < finished_deadline));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check((host.close() == SALTS_OK));
    std::filesystem::remove_all(dir);
}

spec("C11 WorkflowProcessOwner serializes concurrent execution and recovery") {
    it("complete case") { test_c11_workflowprocessowner_serializes_concurrent_execution_and_recovery_6(); }
}

[[maybe_unused]] static void test_workflowrunner_selects_c11_only_with_a_live_single_driver_process_host_7() {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "hosted-shell.yml";
    const auto marker = dir / "c11-entry.txt";
#ifdef _WIN32
    const std::string command = "echo hosted-c11 > \"" + marker.string() + "\"";
#else
    const std::string command = "printf hosted-c11 > '" + marker.string() + "'";
#endif
    writeText(workflow,
        "tasks:\n"
        "  - name: hosted_shell\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: |-\n"
        "            " + command + "\n"
        "          stream_output: false\n");
    WorkflowRunner runner(workflow.string());
    ScopedWorkflowProcessOwner recovery;

    // Neither a missing owner nor a parallel scheduler may silently choose
    // legacy Shell or touch the filesystem.
    const auto without_owner = runner.executeWithC11ProcessOwner(nullptr);
    check((!(without_owner.success)));
    check((without_owner.error_message.find("Process recovery owner") !=
          std::string::npos));
    check((!(std::filesystem::exists(marker))));

    const auto concurrently = runner.executeObservedWithHostTools(
        {}, {}, {}, true, 2, Praktor::Execution::ActionTreeBackend::C11,
        &recovery.owner);
    check((!(concurrently.success)));
    check((concurrently.error_message.find("single driver") !=
          std::string::npos));
    check((!(std::filesystem::exists(marker))));

    const auto result = runner.executeWithC11ProcessOwner(&recovery.owner);
    check((result.success));
    check((result.value["workflow_status"].as<std::string>() == "success"));
    check((std::filesystem::exists(marker)));
    {
        std::ifstream in(marker);
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        check((content.find("hosted-c11") != std::string::npos));
    }

    size_t reaped = 0u, pending = 0u;
    check((cflow_process_retainer_run_ready(
        &recovery.owner, 64u, &reaped, &pending) == SALTS_OK));
    check((pending == 0u));
    std::filesystem::remove_all(dir);
}

spec("WorkflowRunner selects C11 only with a live single-driver Process host") {
    it("complete case") { test_workflowrunner_selects_c11_only_with_a_live_single_driver_process_host_7(); }
}

[[maybe_unused]] static void test_c11_workflowrunner_rejects_nested_uses_without_silently_running_legacy_8() {
    const auto dir = createCoreOnlyTempDir();
    const auto parent = dir / "outer.yml";
    const auto child = dir / "nested.yml";
    const auto marker = dir / "must-not-be-created.txt";
#ifdef _WIN32
    const std::string legacy_command =
        "echo hidden-legacy > \"" + marker.string() + "\"";
#else
    const std::string legacy_command =
        "printf hidden-legacy > '" + marker.string() + "'";
#endif
    writeText(child,
        "tasks:\n"
        "  - name: nested_effect\n"
        "    command: |-\n"
        "      " + legacy_command + "\n");
    writeText(parent,
        "tasks:\n"
        "  - name: nested_uses\n"
        "    uses: ./nested.yml\n");

    WorkflowRunner runner(parent.string());
    ScopedWorkflowProcessOwner recovery;
    const auto result = runner.executeWithC11ProcessOwner(&recovery.owner);
    check((!(result.success)));
    check((result.error_message.find(
        "nested uses requires explicit recovery-owner propagation") !=
        std::string::npos));
    check((!(std::filesystem::exists(marker))));
    size_t reaped = 0u, pending = 0u;
    check((cflow_process_retainer_run_ready(
        &recovery.owner, 64u, &reaped, &pending) == SALTS_OK));
    check((pending == 0u));
    std::filesystem::remove_all(dir);
}

spec("C11 WorkflowRunner rejects nested Uses without silently running Legacy") {
    it("complete case") { test_c11_workflowrunner_rejects_nested_uses_without_silently_running_legacy_8(); }
}

[[maybe_unused]] static void test_workflowrunner_cancellation_retains_active_shell_leases_after_workflow_return_9() {
    const auto dir = createCoreOnlyTempDir();
    const auto workflow = dir / "cancel-hosted-shell.yml";
    const auto started = dir / "child-admitted.txt";
#ifdef _WIN32
    const std::string command =
        "echo admitted > \"" + started.string() +
        "\" & powershell.exe -NoProfile -NonInteractive -Command Start-Sleep -Seconds 30";
#else
    const std::string command =
        "printf admitted > '" + started.string() + "'; exec sleep 30";
#endif
    writeText(workflow,
        "tasks:\n"
        "  - name: hosted_cancel\n"
        "    timeout: 15000\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: |-\n"
        "            " + command + "\n"
        "          stream_output: false\n"
        "          timeout: 30000\n");
    // The persistent host, rather than any WorkflowRunner or Session,
    // owns late Process completions and schedules subsequent recovery turns.
    Praktor::Execution::C11WorkflowProcessOwner recovery(1u);
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    std::atomic<bool> observed_start{false};
    std::thread canceller([&] {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::exists(started)) {
                observed_start.store(true, std::memory_order_relaxed);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        control->requestCancel();
    });
    struct ThreadJoinGuard {
        std::thread& worker;
        ~ThreadJoinGuard() { if (worker.joinable()) worker.join(); }
    } join_guard{canceller};
    const auto began = std::chrono::steady_clock::now();
    WorkflowExecutionResult result{};
    {
        WorkflowRunner runner(workflow.string());
        result = recovery.execute(runner, control);
    } // WorkflowRunner and its task-scoped ExecutionContext are now destroyed.
    canceller.join();
    check((observed_start.load(std::memory_order_relaxed)));
    check((!(result.success)));
    check((std::chrono::steady_clock::now() - began <
          std::chrono::seconds(12)));

    // The original WorkflowRunner and all its DAG/Executor/Blackboard state
    // are gone. Only the persistent host owner can drive and release leases.
    size_t total_reaped = 0u;
    size_t pending = 0u;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(8);
    for (;;) {
        size_t reaped = 0u;
        const int status = recovery.runReady(64u, &reaped, &pending);
        total_reaped += reaped;
        check(((status == SALTS_OK || status == SALTS_EBUSY)));
        if (pending == 0u) {
            check((status == SALTS_OK));
            break;
        }
        check((std::chrono::steady_clock::now() < deadline));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Release of a retained callback owner must return recovery capacity to
    // the same host. A second, distinct WorkflowRunner must be admissible
    // without an extra Process owner or any legacy action fallback.
    const auto next_workflow = dir / "after-recovery.yml";
    const auto next_marker = dir / "after-recovery.txt";
#ifdef _WIN32
    const std::string next_cmd = "echo recovered > \"" + next_marker.string() + "\"";
#else
    const std::string next_cmd = "printf recovered > '" + next_marker.string() + "'";
#endif
    writeText(next_workflow,
        "tasks:\n"
        "  - name: after_recovery\n"
        "    sequence:\n"
        "      - shell:\n"
        "          cmd: |-\n"
        "            " + next_cmd + "\n"
        "          stream_output: false\n");
    {
        WorkflowRunner next_runner(next_workflow.string());
        const auto next_result = recovery.execute(next_runner);
        check((next_result.success));
    }
    check((std::filesystem::exists(next_marker)));

    const auto next_deadline = std::chrono::steady_clock::now() +
                               std::chrono::seconds(8);
    for (;;) {
        size_t reaped = 0u;
        const int status = recovery.runReady(64u, &reaped, &pending);
        check(((status == SALTS_OK || status == SALTS_EBUSY)));
        if (pending == 0u) {
            check((status == SALTS_OK));
            break;
        }
        check((std::chrono::steady_clock::now() < next_deadline));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check((recovery.close() == SALTS_OK));
    std::filesystem::remove_all(dir);
}

spec("WorkflowRunner cancellation retains active Shell leases after workflow return") {
    it("complete case") { test_workflowrunner_cancellation_retains_active_shell_leases_after_workflow_return_9(); }
}

[[maybe_unused]] static void test_opt_in_c11_action_backend_preserves_workflow_context_and_task_output_10() {
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
        check((result.success));
        check((result.error_message.empty()));
        check((result.stdout_data.empty()));
        check((result.stderr_data.empty()));

        const auto description = context.getValueByPath(
            "tasks.shared_contract.outputs.description");
        const auto resolved = context.getValueByPath(
            "tasks.shared_contract.outputs.result");
        const auto status = context.getValueByPath(
            "tasks.shared_contract.outputs.node_status");
        check((description.is_string()));
        check((resolved.is_string()));
        check((status.is_string()));
        check((description.as<std::string>() == "build-Release"));
        check((resolved.as<std::string>() == "build-Release"));
        check((status.as<std::string>() == "SUCCESS"));
    }
}

spec("opt-in C11 action backend preserves workflow context and task output") {
    it("complete case") { test_opt_in_c11_action_backend_preserves_workflow_context_and_task_output_10(); }
}

[[maybe_unused]] static void test_opt_in_c11_explicitly_differs_on_an_empty_shell_command_11() {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    // Record an intentional semantic change instead of asserting false parity:
    // legacy treats Shell with no cmd as a successful empty operation, while
    // the C11 Process adapter rejects it before starting any process.
    OrchNode shell;
    shell.type = "Shell";
    shell.params["stream_output"] = "false";
    auto task = makeCoreOnlyActionTask("empty_shell", std::move(shell));

    WorkflowContext legacy_context;
    DeclarativeTreeExecutor legacy({}, ActionTreeBackend::Legacy);
    const auto legacy_result = legacy.execute(task, legacy_context);
    check((legacy_result.success));
    check((legacy_result.stdout_data.empty()));
    check((legacy_result.stderr_data.empty()));

    WorkflowContext c11_context;
    DeclarativeTreeExecutor c11({}, ActionTreeBackend::C11);
    const auto c11_result = c11.execute(task, c11_context);
    check((!(c11_result.success)));
    check((c11_result.exit_code == -1));
    check((c11_result.error_message.find("Shell requires parameter 'cmd'") !=
          std::string::npos));
    check((c11_result.stdout_data.empty()));
    check((c11_result.stderr_data.empty()));
}

spec("opt-in C11 explicitly differs on an empty Shell command") {
    it("complete case") { test_opt_in_c11_explicitly_differs_on_an_empty_shell_command_11(); }
}

[[maybe_unused]] static void test_opt_in_c11_shell_result_reaches_taskresult_without_legacy_fallback_12() {
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
        ScopedWorkflowProcessOwner recovery;
        DeclarativeTreeExecutor executor({}, backend, &recovery.owner);
        const auto result = executor.execute(task, context);
        check((!(result.success)));
        check((result.exit_code == 7));
        check((!(result.error_message.empty())));
        const auto exit_code = context.getValueByPath(
            "tasks.shell_failure.outputs.exit_code");
        check((exit_code.is_string()));
        check((exit_code.as<std::string>() == "7"));
    }
}

spec("opt-in C11 shell result reaches TaskResult without legacy fallback") {
    it("complete case") { test_opt_in_c11_shell_result_reaches_taskresult_without_legacy_fallback_12(); }
}

[[maybe_unused]] static void test_opt_in_c11_successful_shell_output_reaches_workflow_context_13() {
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

        ScopedWorkflowProcessOwner recovery;
        DeclarativeTreeExecutor executor({}, backend, &recovery.owner);
        const auto result = executor.execute(task, context);
        check((result.success));
        check((result.exit_code == 0));
        check((result.stdout_data.find("workflow-c11-bridge") != std::string::npos));
        const auto stdout_output = context.getValueByPath(
            "tasks.shell_success.outputs.stdout");
        check((stdout_output.is_string()));
        check((stdout_output.as<std::string>().find("workflow-c11-bridge") !=
              std::string::npos));
    }
}

spec("opt-in C11 successful Shell output reaches workflow context") {
    it("complete case") { test_opt_in_c11_successful_shell_output_reaches_workflow_context_13(); }
}

[[maybe_unused]] static void test_opt_in_c11_task_deadline_cancels_a_delayed_leaf_14() {
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
    check((!(result.success)));
    check((result.error_message.find("task deadline") != std::string::npos));
    check((std::chrono::steady_clock::now() - started <
          std::chrono::seconds(3)));
    check((context.getValueByPath(
        "tasks.deadline_guard.outputs.must_not_publish").is_null()));
}

spec("opt-in C11 task deadline cancels a delayed leaf") {
    it("complete case") { test_opt_in_c11_task_deadline_cancels_a_delayed_leaf_14(); }
}

[[maybe_unused]] static void test_opt_in_c11_cancels_a_shell_after_the_child_actually_starts_15() {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    const auto dir = createCoreOnlyTempDir();
    const auto started_file = dir / "child-started.txt";
    std::string command;
#ifdef _WIN32
    // Keep the sleeper in the native PowerShell process rather than spawning
    // an unowned nested process. The marker proves execution reached the leaf.
    std::string escaped_path = started_file.string();
    for (size_t i = 0; (i = escaped_path.find('\'', i)) != std::string::npos; i += 2) {
        escaped_path.insert(i, 1, '\'');
    }
    command = "powershell.exe -NoProfile -NonInteractive -Command \""
              "[System.IO.File]::WriteAllText('" + escaped_path +
              "', 'started'); Start-Sleep -Seconds 30\"";
#else
    command = "printf started > '" + started_file.string() +
              "'; exec sleep 30";
#endif

    WorkflowContext context;
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    context.setExecutionControl(control);
    OrchNode shell;
    shell.type = "Shell";
    shell.params["cmd"] = command;
    shell.params["timeout"] = "30000";
    shell.params["stream_output"] = "false";
    auto task = makeCoreOnlyActionTask("cancel_running_shell", std::move(shell));
    task.timeout = "15000";

    std::atomic<bool> observed_start{false};
    std::thread canceller([&] {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::exists(started_file)) {
                observed_start.store(true, std::memory_order_relaxed);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        control->requestCancel();
    });

    const auto begin = std::chrono::steady_clock::now();
    ScopedWorkflowProcessOwner recovery;
    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11, &recovery.owner);
    TaskResult result(false);
    try {
        result = executor.execute(task, context);
    } catch (...) {
        // Do not destroy an unjoined cancellation thread on an unexpected
        // workflow adapter exception.
        control->requestCancel();
        canceller.join();
        std::filesystem::remove_all(dir);
        throw;
    }
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    canceller.join();

    check((observed_start.load(std::memory_order_relaxed)));
    check((!(result.success)));
    check((result.error_message.find("cancelled") != std::string::npos));
    check((elapsed < std::chrono::seconds(13)));
    const auto node_status = context.getValueByPath(
        "tasks.cancel_running_shell.outputs.node_status");
    check((node_status.is_string()));
    check((node_status.as<std::string>() == "FAILURE"));

    std::filesystem::remove_all(dir);
}

spec("opt-in C11 cancels a Shell after the child actually starts") {
    it("complete case") { test_opt_in_c11_cancels_a_shell_after_the_child_actually_starts_15(); }
}

[[maybe_unused]] static void test_opt_in_c11_workflow_rejects_bad_plan_before_leaf_side_effects_16() {
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
    check_throws_as(executor.execute(task, context), std::invalid_argument);
    const auto absent = context.getValueByPath(
        "tasks.reject_plan.outputs.should_not_publish");
    check((absent.is_null()));
}

spec("opt-in C11 workflow rejects bad plan before leaf side effects") {
    it("complete case") { test_opt_in_c11_workflow_rejects_bad_plan_before_leaf_side_effects_16(); }
}

[[maybe_unused]] static void test_opt_in_c11_respects_cancellation_before_leaf_entry_17() {
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
    check((!(result.success)));
    check((result.error_message.find("cancelled") != std::string::npos));
    check((context.getValueByPath(
        "tasks.cancelled_action.outputs.not_executed").is_null()));
}

spec("opt-in C11 respects cancellation before leaf entry") {
    it("complete case") { test_opt_in_c11_respects_cancellation_before_leaf_entry_17(); }
}

[[maybe_unused]] static void test_opt_in_c11_parallel_fails_closed_without_branch_host_hooks_18() {
    using Praktor::Execution::ActionTreeBackend;
    using Praktor::Execution::DeclarativeTreeExecutor;

    WorkflowContext context;
    OrchNode root;
    root.type = "Parallel";
    for (const char* key : {"a", "b"}) {
        OrchNode step;
        step.type = "SetVariable";
        step.params["key"] = key;
        step.params["value"] = "1";
        root.children.push_back(std::move(step));
    }
    auto task = makeCoreOnlyActionTask("parallel_no_host", std::move(root));
    DeclarativeTreeExecutor executor({}, ActionTreeBackend::C11);
    check_throws_as(executor.execute(task, context), std::invalid_argument);
    check((context.getValueByPath("tasks.parallel_no_host.outputs.a").is_null()));
}

spec("opt-in C11 Parallel fails closed without branch-host hooks") {
    it("complete case") { test_opt_in_c11_parallel_fails_closed_without_branch_host_hooks_18(); }
}

#else

[[maybe_unused]] static void test_c11_action_backend_selection_fails_closed_when_adapter_is_disabled_19() {
    WorkflowContext context;
    OrchNode node;
    node.type = "SetVariable";
    node.params["key"] = "not_executed";
    node.params["value"] = "side-effect";
    auto task = makeCoreOnlyActionTask("disabled_c11", std::move(node));
    Praktor::Execution::DeclarativeTreeExecutor executor(
        {}, Praktor::Execution::ActionTreeBackend::C11);
    check_throws_as(executor.execute(task, context), std::runtime_error);
    check((context.getValueByPath(
        "tasks.disabled_c11.outputs.not_executed").is_null()));
}

spec("C11 action backend selection fails closed when adapter is disabled") {
    it("complete case") { test_c11_action_backend_selection_fails_closed_when_adapter_is_disabled_19(); }
}

#endif
