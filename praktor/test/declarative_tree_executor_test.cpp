#include "praktor_test.hpp"
/**
 * @file declarative_tree_executor_test.cpp
 * @brief Unit tests for DeclarativeTreeExecutor with Mustache preprocessing
 */

#include "executors/declarative_tree_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task_types.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Praktor::Execution;

[[maybe_unused]] static void test_declarativetreeexecutor_mustache_preprocessing_1(int section_index) {
    WorkflowContext context;
    context.setValue("BUILD_DIR", "build");
    context.setValue("PROJECT_NAME", "MyApp");
    context.setValue("BUILD_TYPE", "Release");

    if (section_index == 0) {
        OrchNode set_node;
        set_node.type = "SetVariable";
        set_node.params["key"] = "build_dir";
        set_node.params["value"] = "{{ BUILD_DIR }}";

        OrchParams params;
        params.root = set_node;

        Task task;
        task.name = "test_mustache";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        check((result.success));
        auto value = context.getValueByPath("tasks.test_mustache.outputs.build_dir");
        check((value.is_string()));
        check((value.as<std::string>() == "build"));
    }

    if (section_index == 1) {
        OrchNode set_node;
        set_node.type = "SetVariable";
        set_node.params["key"] = "description";
        set_node.params["value"] = "{{ PROJECT_NAME }}-{{ BUILD_TYPE }}";

        OrchParams params;
        params.root = set_node;

        Task task;
        task.name = "test_multiple_mustache";
        task.action = TaskAction::Orch;
        task.specifics = params;

        DeclarativeTreeExecutor executor;
        auto result = executor.execute(task, context);

        check((result.success));
        auto value = context.getValueByPath("tasks.test_multiple_mustache.outputs.description");
        check((value.is_string()));
        check((value.as<std::string>() == "MyApp-Release"));
    }

    if (section_index == 2) {
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
        check((result.success));
    }

    if (section_index == 3) {
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

        check((result.success));
    }
}

spec("DeclarativeTreeExecutor Mustache preprocessing") {
    it("Simple Mustache template is expanded into an observable output") { test_declarativetreeexecutor_mustache_preprocessing_1(0); }
    it("Multiple Mustache templates preserve both resolved values") { test_declarativetreeexecutor_mustache_preprocessing_1(1); }
    it("Nested nodes with Mustache templates") { test_declarativetreeexecutor_mustache_preprocessing_1(2); }
    it("Mixed variable syntax") { test_declarativetreeexecutor_mustache_preprocessing_1(3); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_context_bridge_2(int section_index) {
    WorkflowContext context;
    context.setValue("TEST_VAR", "test_value");
    context.setTaskOutput("previous_task", "result", "success");

    if (section_index == 0) {
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
        check((result.success));
    }

    if (section_index == 1) {
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

        check((result.success));
    }

    if (section_index == 2) {
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

        check((result.success));
        auto stdout_value = context.getValueByPath("tasks.test_multi_ctx_reference.outputs.stdout");
        check((stdout_value.is_string()));
        check((stdout_value.as<std::string>() == "test_value-other_value"));
    }
}

spec("DeclarativeTreeExecutor context bridge") {
    it("Context reference {ctx.*} still works") { test_declarativetreeexecutor_context_bridge_2(0); }
    it("Task output reference") { test_declarativetreeexecutor_context_bridge_2(1); }
    it("Multiple context references in one parameter") { test_declarativetreeexecutor_context_bridge_2(2); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_variable_resolution_order_3(int section_index) {
    WorkflowContext context;
    context.setValue("VAR1", "from_context");

    if (section_index == 0) {
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
        check((result.success));
    }

    if (section_index == 1) {
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
        check((result.success));
    }
}

spec("DeclarativeTreeExecutor variable resolution order") {
    it("Mustache takes precedence for {{ }}") { test_declarativetreeexecutor_variable_resolution_order_3(0); }
    it("Blackboard for {key}") { test_declarativetreeexecutor_variable_resolution_order_3(1); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_no_mustache_templates_4(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
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

        // Retain the exact native Process/Shell failure evidence on CI. A
        // previously observed macOS failure reported only result.success=false
        // for plain echo; never weaken or retry the acceptance assertion.
        if (!result.success) {
            info("plain Shell failed: error=%s exit=%lld stdout=%s stderr=%s",
                 result.error_message.c_str(),
                 static_cast<long long>(result.exit_code),
                 result.stdout_data.c_str(), result.stderr_data.c_str());
        }
        check((result.success));
    }
}

spec("DeclarativeTreeExecutor no Mustache templates") {
    it("Plain string without templates") { test_declarativetreeexecutor_no_mustache_templates_4(0); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_parallel_runs_sleep_children_concurrently_5() {
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

    check((result.success));
    check((elapsed_ms < 1700));
}

spec("DeclarativeTreeExecutor Parallel runs sleep children concurrently") {
    it("complete case") { test_declarativetreeexecutor_parallel_runs_sleep_children_concurrently_5(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_repeat_without_finite_num_cycles_fails_fast_6() {
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

    check((praktor_test::throws_containing([&] { (void)(executor.execute(task, context)); }, {"Repeat requires a finite 'num_cycles'"})));
}

spec("DeclarativeTreeExecutor Repeat without finite num_cycles fails fast") {
    it("complete case") { test_declarativetreeexecutor_repeat_without_finite_num_cycles_fails_fast_6(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_timeout_fails_when_child_overruns_budget_7() {
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

    check_false(result.success);
    check((result.error_message.find("Action execution failed") != std::string::npos));
}

spec("DeclarativeTreeExecutor Timeout fails when child overruns budget") {
    it("complete case") { test_declarativetreeexecutor_timeout_fails_when_child_overruns_budget_7(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_rejects_malformed_integer_bt_parameters_8() {
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
    check((praktor_test::throws_containing([&] { (void)(executor.execute(task, context)); }, {"Parameter 'timeout_ms' on node 'Timeout' must be an integer"})));
}

spec("DeclarativeTreeExecutor rejects malformed integer BT parameters") {
    it("complete case") { test_declarativetreeexecutor_rejects_malformed_integer_bt_parameters_8(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_rejects_malformed_boolean_bt_parameters_9() {
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
    check((praktor_test::throws_containing([&] { (void)(executor.execute(task, context)); }, {"Parameter 'stream_output' on node 'Shell' must be a boolean"})));
}

spec("DeclarativeTreeExecutor rejects malformed boolean BT parameters") {
    it("complete case") { test_declarativetreeexecutor_rejects_malformed_boolean_bt_parameters_9(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_switch_resolves_bare_blackboard_key_names_10() {
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
    check((result.success));
}

spec("DeclarativeTreeExecutor Switch resolves bare blackboard key names") {
    it("complete case") { test_declarativetreeexecutor_switch_resolves_bare_blackboard_key_names_10(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_switch_rejects_malformed_case_indices_11() {
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
    check((praktor_test::throws_containing([&] { (void)(executor.execute(task, context)); }, {"Parameter 'variable' on node 'Switch' must be an integer"})));
}

spec("DeclarativeTreeExecutor Switch rejects malformed case indices") {
    it("complete case") { test_declarativetreeexecutor_switch_rejects_malformed_case_indices_11(); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_empty_parameters_12(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
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

        // The legacy action executor treats a Shell node with no command as a successful no-op.
        // Keep this compatibility behavior explicit until the C11 backend has a differential oracle.
        check((result.success));
        check((result.error_message.empty()));
        check((result.stdout_data.empty()));
        check((result.stderr_data.empty()));
    }
}

spec("DeclarativeTreeExecutor empty parameters") {
    it("Node with no parameters") { test_declarativetreeexecutor_empty_parameters_12(0); }
}

[[maybe_unused]] static void test_declarativetreeexecutor_parser_outputs_13(int section_index) {
    WorkflowContext context;
    DeclarativeTreeExecutor executor;

    if (section_index == 0) {
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
        check((result.success));

        auto data = context.getValueByPath("tasks.parse_via_blackboard_key.outputs.data");
        check((data.is_string()));
        check((data.as<std::string>() == "ok"));
    }

    if (section_index == 1) {
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
        check((result.success));

        auto data = context.getValueByPath("tasks.parse_json_root.outputs.data");
        check((data.is_object()));
        check((data["status"].as<std::string>() == "ok"));
        check((data["items"][0]["id"].as<int>() == 7));
    }

    if (section_index == 2) {
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
        check((result.success));

        auto data = context.getValueByPath("tasks.parse_keyvalue.outputs.env_vars");
        check((data.is_object()));
        check((data["A"].as<std::string>() == "1"));
        check((data["B"].as<std::string>() == "two"));
    }
}

spec("DeclarativeTreeExecutor parser outputs") {
    it("ParseJson resolves input_key from blackboard references") { test_declarativetreeexecutor_parser_outputs_13(0); }
    it("ParseJson exports structured objects") { test_declarativetreeexecutor_parser_outputs_13(1); }
    it("ParseKeyValue exports structured maps") { test_declarativetreeexecutor_parser_outputs_13(2); }
}
