#include "dag/task_registry.hpp"

#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("TaskRegistry State Machine", "[registry]") {
    TaskRegistry registry;

    SECTION("Initial state is Pending") {
        REQUIRE(registry.getState("new_task") == TaskState::Pending);
        REQUIRE(registry.getStatus("new_task") == "pending");
    }

    SECTION("startTask transitions Pending -> Running") {
        registry.startTask("task_a");
        REQUIRE(registry.getState("task_a") == TaskState::Running);
        REQUIRE(registry.getStatus("task_a") == "running");
    }

    SECTION("markCompleted transitions Running -> Completed") {
        registry.startTask("task_a");
        registry.markCompleted("task_a");
        REQUIRE(registry.getState("task_a") == TaskState::Completed);
        REQUIRE(registry.isCompleted("task_a"));
        REQUIRE(registry.isFinalized("task_a"));
    }

    SECTION("markFailed transitions Running -> Failed") {
        registry.startTask("task_a");
        registry.markFailed("task_a", "something went wrong");
        REQUIRE(registry.getState("task_a") == TaskState::Failed);
        REQUIRE(registry.isFailed("task_a"));
        REQUIRE(registry.isFinalized("task_a"));
        REQUIRE(registry.getFailureReason("task_a") == "something went wrong");
    }

    SECTION("Task re-entry support") {
        registry.startTask("task_a");
        registry.markCompleted("task_a");
        REQUIRE(registry.isCompleted("task_a"));

        // Restarting already completed task should work
        registry.startTask("task_a");
        REQUIRE(registry.getState("task_a") == TaskState::Running);
        REQUIRE_FALSE(registry.isCompleted("task_a"));

        registry.markFailed("task_a", "re-entry failure");
        REQUIRE(registry.isFailed("task_a"));
        REQUIRE(registry.getFailureReason("task_a") == "re-entry failure");
    }

    SECTION("startTask can be called multiple times") {
        registry.startTask("task_a");
        REQUIRE_NOTHROW(registry.startTask("task_a"));
        REQUIRE(registry.getState("task_a") == TaskState::Running);
    }
}

TEST_CASE("TaskRegistry Output Immutability", "[registry]") {
    TaskRegistry registry;

    SECTION("setOutput allowed during Running state") {
        registry.startTask("task_a");
        REQUIRE_NOTHROW(registry.setOutput("task_a", "result", jsoncons::json("value")));

        auto outputs = registry.getAllOutputs("task_a");
        REQUIRE(outputs["result"].as<std::string>() == "value");
    }

    SECTION("setOutput auto-starts Pending task") {
        REQUIRE(registry.getState("task_a") == TaskState::Pending);
        registry.setOutput("task_a", "key", jsoncons::json(42));
        REQUIRE(registry.getState("task_a") == TaskState::Running);
    }

    SECTION("setOutput throws after Completed") {
        registry.startTask("task_a");
        registry.setOutput("task_a", "before", jsoncons::json("ok"));
        registry.markCompleted("task_a");

        REQUIRE_THROWS_WITH(
            registry.setOutput("task_a", "after", jsoncons::json("fail")),
            Catch::Matchers::ContainsSubstring("immutable after completion"));
    }

    SECTION("setOutput throws after Failed") {
        registry.startTask("task_a");
        registry.markFailed("task_a", "error");

        REQUIRE_THROWS_WITH(
            registry.setOutput("task_a", "key", jsoncons::json("value")),
            Catch::Matchers::ContainsSubstring("immutable after completion"));
    }

    SECTION("mergeOutputs works during Running") {
        registry.startTask("task_a");

        jsoncons::json outputs = jsoncons::json::object();
        outputs["stdout"] = "hello world";
        outputs["exit_code"] = 0;

        REQUIRE_NOTHROW(registry.mergeOutputs("task_a", outputs));

        auto all = registry.getAllOutputs("task_a");
        REQUIRE(all["stdout"].as<std::string>() == "hello world");
        REQUIRE(all["exit_code"].as<int>() == 0);
    }
}

TEST_CASE("TaskRegistry Output Access", "[registry]") {
    TaskRegistry registry;

    SECTION("getOutput returns correct value") {
        registry.startTask("task_a");
        registry.setOutput("task_a", "data", jsoncons::json::array({1, 2, 3}));
        registry.markCompleted("task_a");

        auto data = registry.getOutput("task_a", "data");
        REQUIRE(data.is_array());
        REQUIRE(data.size() == 3);
    }

    SECTION("getOutput throws for missing task") {
        REQUIRE_THROWS_WITH(registry.getOutput("nonexistent", "key"),
            Catch::Matchers::ContainsSubstring("Task not found"));
    }

    SECTION("getOutput throws for missing key") {
        registry.startTask("task_a");
        registry.setOutput("task_a", "exists", jsoncons::json(true));
        registry.markCompleted("task_a");

        REQUIRE_THROWS_WITH(registry.getOutput("task_a", "missing"),
            Catch::Matchers::ContainsSubstring("Output key not found"));
    }

    SECTION("getAllOutputs returns empty object for task without outputs") {
        registry.startTask("task_a");
        registry.markCompleted("task_a");

        auto outputs = registry.getAllOutputs("task_a");
        REQUIRE(outputs.is_object());
        REQUIRE(outputs.empty());
    }
}

TEST_CASE("TaskRegistry Backward Compatibility", "[registry]") {
    TaskRegistry registry;

    SECTION("setStatus with 'running' starts task") {
        registry.setStatus("task_a", "running");
        REQUIRE(registry.getState("task_a") == TaskState::Running);
    }

    SECTION("setStatus with 'success' completes task") {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "success");
        REQUIRE(registry.getState("task_a") == TaskState::Completed);
        REQUIRE(registry.isCompleted("task_a"));
    }

    SECTION("setStatus with 'completed' completes task") {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "completed");
        REQUIRE(registry.isCompleted("task_a"));
    }

    SECTION("setStatus with 'skipped' marks as completed") {
        registry.setStatus("task_a", "skipped");
        REQUIRE(registry.getState("task_a") == TaskState::Skipped);
        REQUIRE(registry.isCompleted("task_a"));
    }

    SECTION("setStatus with 'failed' marks as failed") {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "failed");
        REQUIRE(registry.getState("task_a") == TaskState::Failed);
    }
}

TEST_CASE("TaskRegistry toJson", "[registry]") {
    TaskRegistry registry;

    registry.startTask("task_a");
    registry.setOutput("task_a", "result", jsoncons::json("success"));
    registry.markCompleted("task_a");

    registry.startTask("task_b");
    registry.markFailed("task_b", "error");

    auto json = registry.toJson();

    REQUIRE(json.is_object());
    REQUIRE(json.contains("task_a"));
    REQUIRE(json.contains("task_b"));

    REQUIRE(json["task_a"]["status"].as<std::string>() == "success");
    REQUIRE(json["task_a"]["outputs"]["result"].as<std::string>() == "success");

    REQUIRE(json["task_b"]["status"].as<std::string>() == "failed");
}

TEST_CASE("TaskRegistry Query Methods", "[registry]") {
    TaskRegistry registry;

    registry.startTask("a");
    registry.markCompleted("a");

    registry.startTask("b");
    registry.markCompleted("b");

    registry.startTask("c");
    registry.markFailed("c", "error c");

    registry.startTask("d");
    registry.markFailed("d", "error d");

    SECTION("getCompletedTasks returns all completed") {
        auto completed = registry.getCompletedTasks();
        REQUIRE(completed.size() == 2);
        REQUIRE(completed.count("a"));
        REQUIRE(completed.count("b"));
    }

    SECTION("getFailedTasks returns all failed with reasons") {
        auto failed = registry.getFailedTasks();
        REQUIRE(failed.size() == 2);
        REQUIRE(failed["c"] == "error c");
        REQUIRE(failed["d"] == "error d");
    }
}
