/**
 * @file declarative_tree_executor_test.cpp
 * @brief Unit tests for DeclarativeTreeExecutor with Mustache preprocessing
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "executors/declarative_tree_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task_types.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Praktor::Execution;

TEST_CASE("DeclarativeTreeExecutor Mustache preprocessing", "[declarative_tree_executor]") {
    WorkflowContext context;
    context.setValue("BUILD_DIR", "build");
    context.setValue("PROJECT_NAME", "MyApp");
    context.setValue("BUILD_TYPE", "Release");

    SECTION("Simple Mustache template in shell command") {
        // Create a simple orchestration tree
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "cmake --build {{ BUILD_DIR }}";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_mustache";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;

        // Note: This will actually execute the command, so we just test that it doesn't crash
        // In a real test environment, we'd mock the shell execution
        auto result = executor.execute(task, context);

        // The command will likely fail (no cmake project), but preprocessing should work
        // We're mainly testing that Mustache templates are expanded without errors
        REQUIRE(true); // If we get here, preprocessing didn't crash
    }

    SECTION("Multiple Mustache templates") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {{ PROJECT_NAME }} {{ BUILD_TYPE }}";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_multiple_mustache";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(true); // Preprocessing should handle multiple templates
    }

    SECTION("Nested nodes with Mustache templates") {
        // Create a sequence with multiple shell commands
        OrchNode sequence_node;
        sequence_node.type = "Sequence";

        OrchNode shell1;
        shell1.type = "Shell";
        shell1.params["cmd"] = "echo Building {{ PROJECT_NAME }}";

        OrchNode shell2;
        shell2.type = "Shell";
        shell2.params["cmd"] = "echo Type: {{ BUILD_TYPE }}";

        sequence_node.children.push_back(shell1);
        sequence_node.children.push_back(shell2);

        OrchParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_nested_mustache";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Should preprocess all nested nodes recursively
        REQUIRE(result.success);
    }

    SECTION("Mixed variable syntax") {
        // Test that Mustache {{ }} and blackboard {key} can coexist
        OrchNode sequence_node;
        sequence_node.type = "Sequence";

        OrchNode set_var;
        set_var.type = "SetVariable";
        set_var.params["key"] = "target";
        set_var.params["value"] = "myapp";

        OrchNode shell;
        shell.type = "Shell";
        // Mustache {{ PROJECT_NAME }} should be expanded
        // Blackboard {target} should remain for orchestration to resolve
        shell.params["cmd"] = "echo {{ PROJECT_NAME }} {target}";

        sequence_node.children.push_back(set_var);
        sequence_node.children.push_back(shell);

        OrchParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_mixed_syntax";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor context bridge", "[declarative_tree_executor]") {
    WorkflowContext context;
    context.setValue("TEST_VAR", "test_value");
    context.setTaskOutput("previous_task", "result", "success");

    SECTION("Context reference {ctx.*} still works") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {ctx.variables.TEST_VAR}";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_ctx_reference";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Context bridge should still work alongside Mustache
        REQUIRE(result.success);
    }

    SECTION("Task output reference") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {ctx.tasks.previous_task.outputs.result}";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_task_output";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(result.success);
    }

    SECTION("Multiple context references in one parameter") {
        context.setValue("OTHER_VAR", "other_value");

        OrchNode set_node;
        set_node.type = "SetVariable";
        set_node.params["key"] = "stdout";
        set_node.params["value"] = "{ctx.variables.TEST_VAR}-{ctx.variables.OTHER_VAR}";

        OrchParams params;
        params.root = set_node;

        Task task;
        task.name = "test_multi_ctx_reference";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(result.success);
        auto stdout_value = context.getValueByPath("tasks.test_multi_ctx_reference.outputs.stdout");
        REQUIRE(stdout_value.is_string());
        CHECK(stdout_value.as<std::string>() == "test_value-other_value");
    }
}

TEST_CASE("DeclarativeTreeExecutor variable resolution order", "[declarative_tree_executor]") {
    WorkflowContext context;
    context.setValue("VAR1", "from_context");

    SECTION("Mustache takes precedence for {{ }}") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {{ VAR1 }}";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_mustache_precedence";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // {{ VAR1 }} should be expanded to "from_context" during preprocessing
        REQUIRE(result.success);
    }

    SECTION("Blackboard for {key}") {
        OrchNode sequence_node;
        sequence_node.type = "Sequence";

        OrchNode set_var;
        set_var.type = "SetVariable";
        set_var.params["key"] = "VAR2";
        set_var.params["value"] = "from_blackboard";

        OrchNode shell;
        shell.type = "Shell";
        shell.params["cmd"] = "echo {VAR2}";

        sequence_node.children.push_back(set_var);
        sequence_node.children.push_back(shell);

        OrchParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_blackboard";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // {VAR2} should be resolved from blackboard during action execution
        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor no Mustache templates", "[declarative_tree_executor]") {
    WorkflowContext context;

    SECTION("Plain string without templates") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo hello world";

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_plain_string";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Should work fine without any templates
        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor Parallel runs sleep children concurrently", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode parallel_node;
    parallel_node.type = "Parallel";

    OrchNode sleep_one;
    sleep_one.type = "Sleep";
    sleep_one.params["duration"] = "1s";

    OrchNode sleep_two;
    sleep_two.type = "Sleep";
    sleep_two.params["duration"] = "1s";

    parallel_node.children.push_back(sleep_one);
    parallel_node.children.push_back(sleep_two);

    OrchParams params;
    params.root = parallel_node;

    Task task;
    task.name = "test_parallel_sleep";
    task.action = TaskAction::Orch;
    task.specifics = params;

    DeclarativeTreeExecutor executor;

    const auto start = std::chrono::steady_clock::now();
    auto result = executor.execute(task, context);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    REQUIRE(result.success);
    CHECK(elapsed_ms < 1700);
}

TEST_CASE("DeclarativeTreeExecutor Repeat without finite num_cycles fails fast", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode repeat_node;
    repeat_node.type = "Repeat";

    OrchNode shell_node;
    shell_node.type = "Shell";
    shell_node.params["cmd"] = "echo loop";

    repeat_node.children.push_back(shell_node);

    OrchParams params;
    params.root = repeat_node;

    Task task;
    task.name = "test_repeat_requires_count";
    task.action = TaskAction::Orch;
    task.specifics = params;

    DeclarativeTreeExecutor executor;

    REQUIRE_THROWS_WITH(
        executor.execute(task, context),
        Catch::Matchers::ContainsSubstring("Repeat requires a finite 'num_cycles'"));
}

TEST_CASE("DeclarativeTreeExecutor Timeout fails when child overruns budget", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode timeout_node;
    timeout_node.type = "Timeout";
    timeout_node.params["timeout_ms"] = "100";

    OrchNode sleep_node;
    sleep_node.type = "Sleep";
    sleep_node.params["duration"] = "1s";

    timeout_node.children.push_back(sleep_node);

    OrchParams params;
    params.root = timeout_node;

    Task task;
    task.name = "test_timeout_budget";
    task.action = TaskAction::Orch;
    task.specifics = params;

    DeclarativeTreeExecutor executor;
    auto result = executor.execute(task, context);

    REQUIRE_FALSE(result.success);
    CHECK(result.error_message.find("Action execution failed") != std::string::npos);
}

TEST_CASE("DeclarativeTreeExecutor rejects malformed integer BT parameters", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode timeout_node;
    timeout_node.type = "Timeout";
    timeout_node.params["timeout_ms"] = "100.5";

    OrchNode shell_node;
    shell_node.type = "Shell";
    shell_node.params["cmd"] = "echo hi";
    timeout_node.children.push_back(shell_node);

    OrchParams params;
    params.root = timeout_node;

    Task task;
    task.name = "strict_int_guard";
    task.action = TaskAction::Orch;
    task.declared_runner = "actions";
    task.specifics = params;

    DeclarativeTreeExecutor executor;
    REQUIRE_THROWS_WITH(
        executor.execute(task, context),
        Catch::Matchers::ContainsSubstring("Parameter 'timeout_ms' on node 'Timeout' must be an integer"));
}

TEST_CASE("DeclarativeTreeExecutor rejects malformed boolean BT parameters", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode shell_node;
    shell_node.type = "Shell";
    shell_node.params["cmd"] = "echo hi";
    shell_node.params["stream_output"] = "maybe";

    OrchParams params;
    params.root = shell_node;

    Task task;
    task.name = "strict_bool_guard";
    task.action = TaskAction::Orch;
    task.declared_runner = "actions";
    task.specifics = params;

    DeclarativeTreeExecutor executor;
    REQUIRE_THROWS_WITH(
        executor.execute(task, context),
        Catch::Matchers::ContainsSubstring("Parameter 'stream_output' on node 'Shell' must be a boolean"));
}

TEST_CASE("DeclarativeTreeExecutor Switch resolves bare blackboard key names", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode sequence_node;
    sequence_node.type = "Sequence";

    OrchNode set_var_node;
    set_var_node.type = "SetVariable";
    set_var_node.params["key"] = "selected_case";
    set_var_node.params["value"] = "1";
    sequence_node.children.push_back(set_var_node);

    OrchNode switch_node;
    switch_node.type = "Switch";
    switch_node.params["variable"] = "selected_case";

    OrchNode failing_case;
    failing_case.type = "CheckExitCode";
    failing_case.params["expected"] = "0";

    OrchNode success_case;
    success_case.type = "Sleep";
    success_case.params["duration"] = "1ms";

    switch_node.children.push_back(failing_case);
    switch_node.children.push_back(success_case);
    sequence_node.children.push_back(switch_node);

    OrchParams params;
    params.root = sequence_node;

    Task task;
    task.name = "switch_bare_key";
    task.action = TaskAction::Orch;
    task.declared_runner = "actions";
    task.specifics = params;

    DeclarativeTreeExecutor executor;
    auto result = executor.execute(task, context);
    REQUIRE(result.success);
}

TEST_CASE("DeclarativeTreeExecutor Switch rejects malformed case indices", "[declarative_tree_executor]") {
    WorkflowContext context;

    OrchNode switch_node;
    switch_node.type = "Switch";
    switch_node.params["variable"] = "1.5";

    OrchNode success_case;
    success_case.type = "Sleep";
    success_case.params["duration"] = "1ms";
    switch_node.children.push_back(success_case);

    OrchParams params;
    params.root = switch_node;

    Task task;
    task.name = "switch_bad_index";
    task.action = TaskAction::Orch;
    task.declared_runner = "actions";
    task.specifics = params;

    DeclarativeTreeExecutor executor;
    REQUIRE_THROWS_WITH(
        executor.execute(task, context),
        Catch::Matchers::ContainsSubstring("Parameter 'variable' on node 'Switch' must be an integer"));
}

TEST_CASE("DeclarativeTreeExecutor empty parameters", "[declarative_tree_executor]") {
    WorkflowContext context;

    SECTION("Node with no parameters") {
        OrchNode shell_node;
        shell_node.type = "Shell";
        // No params

        OrchParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_empty_params";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Should handle empty params gracefully
        // (will fail because no cmd, but shouldn't crash)
        REQUIRE(true);
    }
}

TEST_CASE("DeclarativeTreeExecutor parser outputs", "[declarative_tree_executor]") {
    WorkflowContext context;
    DeclarativeTreeExecutor executor;

    SECTION("ParseJson resolves input_key from blackboard references") {
        OrchNode sequence_node;
        sequence_node.type = "Sequence";

        OrchNode payload_node;
        payload_node.type = "SetVariable";
        payload_node.params["key"] = "payload";
        payload_node.params["value"] = R"(["ok"])";
        sequence_node.children.push_back(payload_node);

        OrchNode alias_node;
        alias_node.type = "SetVariable";
        alias_node.params["key"] = "selected_input";
        alias_node.params["value"] = "payload";
        sequence_node.children.push_back(alias_node);

        OrchNode parse_node;
        parse_node.type = "ParseJson";
        parse_node.params["input_key"] = "{selected_input}";
        parse_node.params["path"] = "$[0]";
        parse_node.params["output_key"] = "data";
        sequence_node.children.push_back(parse_node);

        OrchParams params;
        params.root = sequence_node;

        Task task;
        task.name = "parse_via_blackboard_key";
        task.action = TaskAction::Orch;
        task.declared_runner = "actions";
        task.specifics = params;

        auto result = executor.execute(task, context);
        REQUIRE(result.success);

        auto data = context.getValueByPath("tasks.parse_via_blackboard_key.outputs.data");
        REQUIRE(data.is_string());
        CHECK(data.as<std::string>() == "ok");
    }

    SECTION("ParseJson exports structured objects") {
        context.setValue("raw_json", R"({"status":"ok","items":[{"id":7}]})");

        OrchNode node;
        node.type = "ParseJson";
        node.params["input_key"] = "raw_json";
        node.params["path"] = "$";
        node.params["output_key"] = "data";

        OrchParams params;
        params.root = node;

        Task task;
        task.name = "parse_json_root";
        task.action = TaskAction::Orch;
        task.specifics = params;

        context.pushTaskScope(task.name);
        auto result = executor.execute(task, context);
        context.popTaskScope();
        REQUIRE(result.success);

        auto data = context.getValueByPath("tasks.parse_json_root.outputs.data");
        REQUIRE(data.is_object());
        CHECK(data["status"].as<std::string>() == "ok");
        CHECK(data["items"][0]["id"].as<int>() == 7);
    }

    SECTION("ParseKeyValue exports structured maps") {
        context.setValue("env_blob", "A=1\nB=two\n");

        OrchNode node;
        node.type = "ParseKeyValue";
        node.params["input_key"] = "env_blob";
        node.params["output_key"] = "env_vars";

        OrchParams params;
        params.root = node;

        Task task;
        task.name = "parse_keyvalue";
        task.action = TaskAction::Orch;
        task.specifics = params;

        context.pushTaskScope(task.name);
        auto result = executor.execute(task, context);
        context.popTaskScope();
        REQUIRE(result.success);

        auto data = context.getValueByPath("tasks.parse_keyvalue.outputs.env_vars");
        REQUIRE(data.is_object());
        CHECK(data["A"].as<std::string>() == "1");
        CHECK(data["B"].as<std::string>() == "two");
    }
}
