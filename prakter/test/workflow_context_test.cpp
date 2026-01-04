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
}
