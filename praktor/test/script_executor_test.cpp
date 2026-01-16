
#include "dag/task_executor_factory.hpp"
#include "dag/workflow_context.hpp"
#include "executors/script_executor.hpp"
#include "yml/task_types.hpp"
#include <catch2/catch_all.hpp>

using namespace Praktor::Execution;

TEST_CASE("ScriptExecutor (QuickJS)", "[executor][script]") {
  WorkflowContext context;
  context.setValue("test_var", "initial_value");

  SECTION("Basic JavaScript Execution") {
    Task task;
    task.name = "basic_js";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.source = "var x = 1 + 2; x;";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE(result.stdout_data == "3");
  }

  SECTION("Context Access (get/set)") {
    Task task;
    task.name = "context_access";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.source = R"(
            var val = context.get("test_var");
            context.set("new_var", val + "_updated");
            print("Value was: " + val);
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data,
                 Catch::Matchers::ContainsSubstring("Value was: initial_value"));

    // output is set in the task scope/registry normally.
    // effectively context.set -> setCurrentTaskOutput -> setTaskOutput AND
    // setValue("tasks.taskname.outputs.key") Since we are not running full workflow runner which
    // manages scopes, we might need to check how setCurrentTaskOutput behaves with empty scope
    // stack. It sets output for "__root__".

    REQUIRE(context.hasKey("tasks.__root__.outputs.new_var"));
    REQUIRE(context.getValue<std::string>("tasks.__root__.outputs.new_var") ==
            "initial_value_updated");
  }

  SECTION("Global Variables Injection") {
    Task task;
    task.name = "globals_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.globals["MY_GLOBAL"] = "global_value";
    params.source = "print(MY_GLOBAL);";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("global_value"));
  }

  SECTION("TurboNet Integration via 'turbo'") {
    Task task;
    task.name = "turbo_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    // js_module exposes 'turbo' with dns, fs, http, timers
    params.source = R"(
            var stat = turbo.fs.stat(".");
            print("IsDirectory: " + stat.isDirectory);
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("IsDirectory: true"));
  }

  SECTION("Variable Substitution in Source") {
    Task task;
    task.name = "subst_test";
    task.action = TaskAction::Script;

    context.setValue("name", "World");

    ScriptParams params;
    params.source = "print('Hello {{name}}');";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Hello World"));
  }

  SECTION("ES6 Module Import") {
    Task task;
    task.name = "es6_import_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.source = R"(
import os from 'turbo:os';
const hostname = os.hostname();
print("Hostname: " + hostname);
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Hostname:"));
  }

  SECTION("ES6 Named Import") {
    Task task;
    task.name = "es6_named_import_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.source = R"(
import { stat } from 'turbo:fs';
const info = stat(".");
print("IsDir: " + info.isDirectory);
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("IsDir: true"));
  }

  SECTION("External File Module Import") {
    Task task;
    task.name = "external_module_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    // Use absolute path to test fixture
    params.source = R"(
import testModule from 'fixtures/test_module.js';
print("Version: " + testModule.version);
print("Greeting: " + testModule.greet("World"));
print("Sum: " + testModule.add(2, 3));
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    INFO("Error: " << result.error_message);
    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Version: 1.0.0"));
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Greeting: Hello, World!"));
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Sum: 5"));
  }

  SECTION("External File Named Import") {
    Task task;
    task.name = "external_named_import_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    params.source = R"(
import { greet, add } from 'fixtures/test_module.js';
print(greet("Praktor"));
print("Result: " + add(10, 20));
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Hello, Praktor!"));
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Result: 30"));
  }

  SECTION("Chained Module Import") {
    Task task;
    task.name = "chained_import_test";
    task.action = TaskAction::Script;

    ScriptParams params;
    // helper_module.js imports test_module.js
    params.source = R"(
import { addAndMultiply } from 'fixtures/helper_module.js';
const result = addAndMultiply(2, 3, 4);
print("(2 + 3) * 4 = " + result);
        )";
    task.specifics = params;

    auto executor = createScriptExecutor();
    TaskResult result = executor->execute(task, context);

    REQUIRE(result.success);
    REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("(2 + 3) * 4 = 20"));
  }
}
