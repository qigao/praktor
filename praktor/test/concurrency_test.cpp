
#include <catch2/catch_all.hpp>
#include "dag/workflow_executor.hpp"
#include "dag/workflow_context.hpp"
#include "yml/task.hpp"
#include <chrono>
#include <atomic>

using namespace Praktor::Execution;

TEST_CASE("WorkflowExecutor Concurrency", "[executor][concurrency]") {
    DependencyGraph<Task> graph("test_graph");

    // Create 3 tasks: A and B are independent, C depends on both.
    // A and B sleep. If concurrent, total time < A + B.
    
    Task taskA;
    taskA.name = "taskA";
    taskA.action = TaskAction::Script;
    ScriptParams paramsA;
    paramsA.source = "var start = Date.now(); while(Date.now() - start < 500); print('A done');";
    taskA.specifics = paramsA;

    Task taskB;
    taskB.name = "taskB";
    taskB.action = TaskAction::Script;
    ScriptParams paramsB;
    paramsB.source = "var start = Date.now(); while(Date.now() - start < 500); print('B done');";
    taskB.specifics = paramsB;

    Task taskC;
    taskC.name = "taskC";
    taskC.action = TaskAction::Script;
    ScriptParams paramsC;
    paramsC.source = "print('C done');";
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
        REQUIRE(duration >= 1000); // 500ms + 500ms
    }

    SECTION("Concurrent Execution (2+ threads)") {
        WorkflowContext context;
        // Use 4 threads to be sure
        WorkflowExecutor executor(graph, {}, 4);
        
        auto start = std::chrono::steady_clock::now();
        executor.execute(context);
        auto end = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        REQUIRE(context.getValueOrDefault<std::string>("workflow_status", "") == "success");
        // Should be around 500ms + overhead, definitely less than 1000ms
        REQUIRE(duration < 900); 
        REQUIRE(duration >= 500);
    }
}
