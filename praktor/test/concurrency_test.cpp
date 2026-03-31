
#include <catch2/catch_all.hpp>
#include "dag/workflow_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task.hpp"
#include <chrono>
#include <atomic>

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
    taskA.action = TaskAction::Btdsl;
    BtdslParams paramsA;
    paramsA.root.type = "Sequence";
    BtdslNode shellA;
    shellA.type = "Shell";
    shellA.params["cmd"] = std::string(SLEEP_CMD);
    paramsA.root.children.push_back(std::move(shellA));
    taskA.specifics = paramsA;

    Task taskB;
    taskB.name = "taskB";
    taskB.action = TaskAction::Btdsl;
    BtdslParams paramsB;
    paramsB.root.type = "Sequence";
    BtdslNode shellB;
    shellB.type = "Shell";
    shellB.params["cmd"] = std::string(SLEEP_CMD);
    paramsB.root.children.push_back(std::move(shellB));
    taskB.specifics = paramsB;

    Task taskC;
    taskC.name = "taskC";
    taskC.action = TaskAction::Btdsl;
    BtdslParams paramsC;
    paramsC.root.type = "Sequence";
    BtdslNode shellC;
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
