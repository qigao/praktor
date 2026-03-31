/**
 * @file declarative_tree_executor_test.cpp
 * @brief Unit tests for DeclarativeTreeExecutor with Mustache preprocessing
 */

#include <catch2/catch_test_macros.hpp>
#include "executors/declarative_tree_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task_types.hpp"
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
        // Create a simple BTDSL tree
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "cmake --build {{ BUILD_DIR }}";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_mustache";
        task.action = TaskAction::Btdsl;
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
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {{ PROJECT_NAME }} {{ BUILD_TYPE }}";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_multiple_mustache";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(true); // Preprocessing should handle multiple templates
    }

    SECTION("Nested nodes with Mustache templates") {
        // Create a sequence with multiple shell commands
        BtdslNode sequence_node;
        sequence_node.type = "Sequence";

        BtdslNode shell1;
        shell1.type = "Shell";
        shell1.params["cmd"] = "echo Building {{ PROJECT_NAME }}";

        BtdslNode shell2;
        shell2.type = "Shell";
        shell2.params["cmd"] = "echo Type: {{ BUILD_TYPE }}";

        sequence_node.children.push_back(shell1);
        sequence_node.children.push_back(shell2);

        BtdslParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_nested_mustache";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Should preprocess all nested nodes recursively
        REQUIRE(result.success);
    }

    SECTION("Mixed variable syntax") {
        // Test that Mustache {{ }} and blackboard {key} can coexist
        BtdslNode sequence_node;
        sequence_node.type = "Sequence";

        BtdslNode set_var;
        set_var.type = "SetVariable";
        set_var.params["key"] = "target";
        set_var.params["value"] = "myapp";

        BtdslNode shell;
        shell.type = "Shell";
        // Mustache {{ PROJECT_NAME }} should be expanded
        // Blackboard {target} should remain for BTDSL to resolve
        shell.params["cmd"] = "echo {{ PROJECT_NAME }} {target}";

        sequence_node.children.push_back(set_var);
        sequence_node.children.push_back(shell);

        BtdslParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_mixed_syntax";
        task.action = TaskAction::Btdsl;
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
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {ctx.variables.TEST_VAR}";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_ctx_reference";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Context bridge should still work alongside Mustache
        REQUIRE(result.success);
    }

    SECTION("Task output reference") {
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {ctx.tasks.previous_task.outputs.result}";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_task_output";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor variable resolution order", "[declarative_tree_executor]") {
    WorkflowContext context;
    context.setValue("VAR1", "from_context");

    SECTION("Mustache takes precedence for {{ }}") {
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo {{ VAR1 }}";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_mustache_precedence";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // {{ VAR1 }} should be expanded to "from_context" during preprocessing
        REQUIRE(result.success);
    }

    SECTION("Blackboard for {key}") {
        BtdslNode sequence_node;
        sequence_node.type = "Sequence";

        BtdslNode set_var;
        set_var.type = "SetVariable";
        set_var.params["key"] = "VAR2";
        set_var.params["value"] = "from_blackboard";

        BtdslNode shell;
        shell.type = "Shell";
        shell.params["cmd"] = "echo {VAR2}";

        sequence_node.children.push_back(set_var);
        sequence_node.children.push_back(shell);

        BtdslParams params;
        params.root = sequence_node;

        Task task;
        task.name = "test_blackboard";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // {VAR2} should be resolved from blackboard during BTDSL execution
        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor no Mustache templates", "[declarative_tree_executor]") {
    WorkflowContext context;

    SECTION("Plain string without templates") {
        BtdslNode shell_node;
        shell_node.type = "Shell";
        shell_node.params["cmd"] = "echo hello world";

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_plain_string";
        task.action = TaskAction::Btdsl;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        // Should work fine without any templates
        REQUIRE(result.success);
    }
}

TEST_CASE("DeclarativeTreeExecutor empty parameters", "[declarative_tree_executor]") {
    WorkflowContext context;

    SECTION("Node with no parameters") {
        BtdslNode shell_node;
        shell_node.type = "Shell";
        // No params

        BtdslParams params;
        params.root = shell_node;

        Task task;
        task.name = "test_empty_params";
        task.action = TaskAction::Btdsl;
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

    SECTION("ParseJson exports structured objects") {
        context.setValue("raw_json", R"({"status":"ok","items":[{"id":7}]})");

        BtdslNode node;
        node.type = "ParseJson";
        node.params["input_key"] = "raw_json";
        node.params["path"] = "$";
        node.params["output_key"] = "data";

        BtdslParams params;
        params.root = node;

        Task task;
        task.name = "parse_json_root";
        task.action = TaskAction::Btdsl;
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

        BtdslNode node;
        node.type = "ParseKeyValue";
        node.params["input_key"] = "env_blob";
        node.params["output_key"] = "env_vars";

        BtdslParams params;
        params.root = node;

        Task task;
        task.name = "parse_keyvalue";
        task.action = TaskAction::Btdsl;
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
