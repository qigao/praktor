#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("WorkflowContext::getVariable default value", "[context]") {
    WorkflowContext context;

    SECTION("Returns empty string for missing key") {
        REQUIRE(context.getVariable("nonexistent") == "");
        REQUIRE(context.getVariable("nonexistent").empty());
    }

    SECTION("Returns actual value when key exists") {
        context.setVariable("mykey", "myvalue");
        REQUIRE(context.getVariable("mykey") == "myvalue");
    }
}

TEST_CASE("WorkflowContext Core Functionality", "[context]") {
    WorkflowContext context;

    SECTION("Set and Get basic values") {
        context.setValue("my_string", std::string("hello"));
        context.setValue("my_int", 42);
        context.setValue("my_bool", true);

        REQUIRE(context.hasKey("my_string"));
        REQUIRE(context.getValue<std::string>("my_string") == "hello");
        REQUIRE(context.getValue<int>("my_int") == 42);
        REQUIRE(context.getValue<bool>("my_bool") == true);
    }

    SECTION("Overwrite existing value") {
        context.setValue("key", std::string("initial"));
        CHECK(context.getValue<std::string>("key") == "initial");

        context.setValue("key", std::string("overwritten"));
        REQUIRE(context.getValue<std::string>("key") == "overwritten");
    }

    SECTION("hasKey works correctly") {
        context.setValue("a", 1);
        REQUIRE(context.hasKey("a"));
        REQUIRE_FALSE(context.hasKey("b"));
    }

    SECTION("getValueOrDefault provides default") {
        context.setValue("a", std::string("actual"));
        REQUIRE(context.getValueOrDefault<std::string>("a", "default") == "actual");
        REQUIRE(context.getValueOrDefault<std::string>("b", "default") == "default");
    }

    SECTION("Nested paths resolve through JSON objects") {
        WorkflowValue payload = jsoncons::json::object();
        payload["meta"] = jsoncons::json::object();
        payload["meta"]["region"] = "apac";
        context.setValue("payload", payload);

        REQUIRE(context.getValueByPath("payload.meta.region").as<std::string>() == "apac");
    }

    SECTION("Task paths resolve through the task registry facade") {
        context.setTaskStatus("build", "running");
        context.setTaskOutput("build", "artifact", "pkg.zip");
        context.setTaskStatus("build", "success");

        REQUIRE(context.getValueByPath("tasks.build.status").as<std::string>() == "success");
        REQUIRE(context.getValueByPath("tasks.build.outputs.artifact").as<std::string>() == "pkg.zip");
    }

    SECTION("JMESPath queries still work through the facade") {
        WorkflowValue payload = jsoncons::json::object();
        payload["servers"] = jsoncons::json::array();
        payload["servers"].push_back(jsoncons::json::object({{"name", "api"}, {"port", 8080}}));
        payload["servers"].push_back(jsoncons::json::object({{"name", "worker"}, {"port", 9090}}));
        context.setValue("payload", payload);

        auto ports = context.getJsonValue("payload", "servers[*].port");
        REQUIRE(ports.is_array());
        REQUIRE(ports.size() == 2);
        REQUIRE(ports[0].as<int>() == 8080);
        REQUIRE(ports[1].as<int>() == 9090);
    }

    SECTION("Task Status Tracking") {
        context.addCompletedTask("task_A");
        context.addCompletedTask("task_B");
        context.addFailedTask("task_C", "it broke");

        REQUIRE(context.getCompletedTasks().size() == 2);
        REQUIRE(context.getFailedTasks().size() == 1);

        CHECK(context.getCompletedTasks().count("task_A"));
        CHECK_FALSE(context.getCompletedTasks().count("task_C"));

        REQUIRE(context.getFailedTasks().count("task_C"));
        CHECK(context.getFailedTasks().at("task_C") == "it broke");
    }

    SECTION("Failure context variables are scoped and removable") {
        TaskFailureContext failure;
        failure.task_name = "deploy";
        failure.task_type = "script";
        failure.error_message = "boom";
        failure.captured_outputs["artifact"] = "pkg.zip";

        context.setFailureContext(failure);

        REQUIRE(context.isInFailureContext());
        REQUIRE(context.getVariable("failed_task_name") == "deploy");
        REQUIRE(context.getVariable("failed_task_type") == "script");
        REQUIRE(context.getValueByPath("failed_task.outputs.artifact").as<std::string>() == "pkg.zip");

        context.clearFailureContext();

        REQUIRE_FALSE(context.isInFailureContext());
        CHECK(context.getVariable("failed_task_name").empty());
        CHECK(context.getValueByPath("failed_task").is_null());
    }

    SECTION("Module store is copied on fork and queried through the facade") {
        EmbeddedModule embedded;
        embedded.language = "javascript";
        embedded.source = "export const value = 1;";

        NativeModule native;
        native.name = "demo";
        native.path = "demo.dll";
        native.hooks["run"] = "demo_run";

        context.setEmbeddedModules({{"demo", embedded}});
        context.setNativeModules({native});

        auto child = context.fork();

        REQUIRE(child->hasEmbeddedModule("demo"));
        REQUIRE(child->getEmbeddedModule("demo") != nullptr);
        CHECK(child->getEmbeddedModule("demo")->source == "export const value = 1;");
        REQUIRE(child->getNativeModules().size() == 1);
        CHECK(child->getNativeModules().front().name == "demo");
    }

    SECTION("Source path is copied on fork") {
        context.setSourcePath("/tmp/praktor/workflow.yml");

        auto child = context.fork();

        CHECK(child->getSourcePath() == "/tmp/praktor/workflow.yml");
    }
}
