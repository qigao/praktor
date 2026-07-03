
#include <catch2/catch_all.hpp>
#include "dag/workflow_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task.hpp"
#include <chrono>
#include <atomic>
#include <string>
#include <vector>

using namespace Praktor::Execution;

// Platform-specific sleep command and timing constants
#ifdef _WIN32
static const char* SLEEP_CMD = "waitfor SomethingThatNeverHappens /T 1 >nul 2>nul & exit /b 0";
static constexpr long long SEQUENTIAL_MIN_MS = 1800;  // ~1s + ~1s
static constexpr long long CONCURRENT_MIN_MS = 500;
#else
static const char* SLEEP_CMD = "sleep 0.5";
static constexpr long long SEQUENTIAL_MIN_MS = 900;   // ~500ms + ~500ms
static constexpr long long CONCURRENT_MIN_MS = 400;
#endif

TEST_CASE("WorkflowExecutor Concurrency", "[executor][concurrency]") {
    DependencyGraph<Task> graph("test_graph");

    // Create 3 tasks: A and B are independent, C depends on both.
    // A and B sleep. If concurrent, total time < A + B.

    Task taskA;
    taskA.name = "taskA";
    taskA.action = TaskAction::Orch;
    OrchParams paramsA;
    paramsA.root.type = "Sequence";
    OrchNode shellA;
    shellA.type = "Shell";
    shellA.params["cmd"] = std::string(SLEEP_CMD);
    paramsA.root.children.push_back(std::move(shellA));
    taskA.specifics = paramsA;

    Task taskB;
    taskB.name = "taskB";
    taskB.action = TaskAction::Orch;
    OrchParams paramsB;
    paramsB.root.type = "Sequence";
    OrchNode shellB;
    shellB.type = "Shell";
    shellB.params["cmd"] = std::string(SLEEP_CMD);
    paramsB.root.children.push_back(std::move(shellB));
    taskB.specifics = paramsB;

    Task taskC;
    taskC.name = "taskC";
    taskC.action = TaskAction::Orch;
    OrchParams paramsC;
    paramsC.root.type = "Sequence";
    OrchNode shellC;
    shellC.type = "Shell";
    shellC.params["cmd"] = std::string("echo C done");
    paramsC.root.children.push_back(std::move(shellC));
    taskC.specifics = paramsC;

    graph.addNode(taskA);
    graph.addNode(taskB);
    graph.addNode(taskC);
    graph.addEdge(taskA, taskC);
    graph.addEdge(taskB, taskC);

    SECTION("Sequential Execution (1 thread)") {
        WorkflowContext context;
        WorkflowExecutor executor(graph, {}, 1);

        auto start = std::chrono::steady_clock::now();
        executor.execute(context);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
        // Sequential: both tasks run one after another
        REQUIRE(duration >= SEQUENTIAL_MIN_MS);
    }

    SECTION("Concurrent Execution (2+ threads)") {
        WorkflowContext context;
        WorkflowExecutor executor(graph, {}, 4);

        auto start = std::chrono::steady_clock::now();
        executor.execute(context);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
        // Concurrent: both tasks run in parallel, so total < sequential
        REQUIRE(duration < SEQUENTIAL_MIN_MS);
        REQUIRE(duration >= CONCURRENT_MIN_MS);
    }
}

TEST_CASE("WorkflowExecutor respects max concurrency for ready tasks", "[executor][concurrency]") {
    DependencyGraph<Task> graph("bounded_ready_graph");
    std::vector<Task> tasks;

    for (int i = 0; i < 3; ++i) {
        Task task;
        task.name = "task" + std::to_string(i);
        task.action = TaskAction::Orch;

        OrchParams params;
        params.root.type = "Sequence";

        OrchNode sleep;
        sleep.type = "Sleep";
        sleep.params["duration"] = "600ms";
        params.root.children.push_back(std::move(sleep));

        task.specifics = params;
        graph.addNode(task);
        tasks.push_back(std::move(task));
    }

    WorkflowContext context;
    WorkflowExecutor executor(graph, tasks, {}, 2, false);

    auto start = std::chrono::steady_clock::now();
    executor.execute(context);
    auto end = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
    REQUIRE(duration >= 1000);
}

TEST_CASE("Forked task contexts merge local writes back to the parent context",
          "[executor][concurrency][context_merge]") {
    DependencyGraph<Task> graph("merge_graph");

    Task task;
    task.name = "taskA";
    task.script = R"(
        ctx.set("from_forked_task", "visible");
    )";

    graph.addNode(task);

    WorkflowContext context;
    WorkflowExecutor executor(graph, {task}, {}, 4, false);
    executor.execute(context);

    REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
    CHECK(context.getValueOrDefault<std::string>("from_forked_task", "") == "visible");
}

TEST_CASE("Each iterations merge local writes back to the parent context",
          "[executor][each][context_merge]") {
    DependencyGraph<Task> graph("each_merge_graph");

    Task task;
    task.name = "fanout";
    task.each = Each{};
    task.each->items = {"alpha", "beta", "gamma"};
    task.each->as = "item";
    task.script = R"(
        ctx.set("last_item_seen", ctx.get("item"));
    )";

    graph.addNode(task);

    WorkflowContext context;
    WorkflowExecutor executor(graph, {task}, {}, 1, false);
    executor.execute(context);

    REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
    CHECK(context.getValueOrDefault<std::string>("last_item_seen", "") == "gamma");
    CHECK(context.getVariable("item").empty());
}

TEST_CASE("Each iteration index variable does not leak into the parent context",
          "[executor][each][context_merge]") {
    DependencyGraph<Task> graph("each_index_graph");

    Task task;
    task.name = "fanout";
    task.each = Each{};
    task.each->items = {"alpha", "beta"};
    task.each->as = "item";
    task.each->index_variable = "idx";
    task.script = R"(
        ctx.set("last_index_seen", ctx.get("idx"));
    )";

    graph.addNode(task);

    WorkflowContext context;
    WorkflowExecutor executor(graph, {task}, {}, 1, false);
    executor.execute(context);

    REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
    CHECK(context.getValueOrDefault<std::string>("last_index_seen", "") == "1");
    CHECK(context.getVariable("idx").empty());
}

TEST_CASE("Concurrent script tasks complete without corrupting TurboScript state",
          "[executor][concurrency][script]") {
    DependencyGraph<Task> graph("script_concurrency_graph");

    Task taskA;
    taskA.name = "scriptA";
    taskA.script = R"script(
        payload = json.parse("{\"value\":41,\"items\":[1]}");
        ctx.set("script_a_value", payload.value + 1);
    )script";

    Task taskB;
    taskB.name = "scriptB";
    taskB.script = R"script(
        payload = json.parse("{\"value\":39,\"items\":[1,2,3]}");
        ctx.set("script_b_count", json.query(payload.items, "length(@)"));
    )script";

    graph.addNode(taskA);
    graph.addNode(taskB);

    WorkflowContext context;
    WorkflowExecutor executor(graph, {taskA, taskB}, {}, 4, false);
    executor.execute(context);

    REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
    CHECK(context.getValue<double>("script_a_value") == Catch::Approx(42.0));
    CHECK(context.getValue<double>("script_b_count") == Catch::Approx(3.0));
}
