
#include <catch2/catch_all.hpp>
#include "dag/workflow_context.hpp"
#include "executors/script_executor.hpp"
#include "yml/task_types.hpp"
#include "dag/task_executor_factory.hpp"

using namespace Prakter::Execution;

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
        REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Value was: initial_value"));
        
        // output is set in the task scope/registry normally. 
        // effectively context.set -> setCurrentTaskOutput -> setTaskOutput AND setValue("tasks.taskname.outputs.key")
        // Since we are not running full workflow runner which manages scopes, we might need to check how setCurrentTaskOutput behaves with empty scope stack.
        // It sets output for "__root__".
        
        REQUIRE(context.hasKey("tasks.__root__.outputs.new_var"));
        REQUIRE(context.getValue<std::string>("tasks.__root__.outputs.new_var") == "initial_value_updated");
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

    SECTION("Libuv/OS Integration via 'uv'") {
        Task task;
        task.name = "uv_test";
        task.action = TaskAction::Script;

        ScriptParams params;
        // js_uv_module exposes 'uv'
        params.source = R"(
            var hostname = uv.os.hostname();
            print("Hostname: " + hostname);
        )";
        task.specifics = params;

        auto executor = createScriptExecutor();
        TaskResult result = executor->execute(task, context);

        REQUIRE(result.success);
        REQUIRE_THAT(result.stdout_data, Catch::Matchers::ContainsSubstring("Hostname:"));
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
}
