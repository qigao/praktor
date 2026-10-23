#include "praktor_test.hpp"

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

[[maybe_unused]] static void test_workflowexecutor_concurrency_1(int section_index) {
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

    if (section_index == 0) {
        WorkflowContext context;
        WorkflowExecutor executor(graph, {}, 1);

        auto start = std::chrono::steady_clock::now();
        executor.execute(context);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
        // Sequential: both tasks run one after another
        check((duration >= SEQUENTIAL_MIN_MS));
    }

    if (section_index == 1) {
        WorkflowContext context;
        WorkflowExecutor executor(graph, {}, 4);

        auto start = std::chrono::steady_clock::now();
        executor.execute(context);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
        // Concurrent: both tasks run in parallel, so total < sequential
        check((duration < SEQUENTIAL_MIN_MS));
        check((duration >= CONCURRENT_MIN_MS));
    }
}

spec("WorkflowExecutor Concurrency") {
    it("Sequential Execution (1 thread)") { test_workflowexecutor_concurrency_1(0); }
    it("Concurrent Execution (2+ threads)") { test_workflowexecutor_concurrency_1(1); }
}

[[maybe_unused]] static void test_workflowexecutor_respects_max_concurrency_for_ready_tasks_2() {
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

    check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
    check((duration >= 1000));
}

spec("WorkflowExecutor respects max concurrency for ready tasks") {
    it("complete case") { test_workflowexecutor_respects_max_concurrency_for_ready_tasks_2(); }
}

[[maybe_unused]] static void test_forked_task_contexts_merge_local_writes_back_to_the_parent_context_3() {
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

    check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
    check((context.getValueOrDefault<std::string>("from_forked_task", "") == "visible"));
}

spec("Forked task contexts merge local writes back to the parent context") {
    it("complete case") { test_forked_task_contexts_merge_local_writes_back_to_the_parent_context_3(); }
}

[[maybe_unused]] static void test_each_iterations_merge_local_writes_back_to_the_parent_context_4() {
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

    check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
    check((context.getValueOrDefault<std::string>("last_item_seen", "") == "gamma"));
    check((context.getVariable("item").empty()));
}

spec("Each iterations merge local writes back to the parent context") {
    it("complete case") { test_each_iterations_merge_local_writes_back_to_the_parent_context_4(); }
}

[[maybe_unused]] static void test_each_iteration_index_variable_does_not_leak_into_the_parent_context_5() {
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

    check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
    check((context.getValueOrDefault<std::string>("last_index_seen", "") == "1"));
    check((context.getVariable("idx").empty()));
}

spec("Each iteration index variable does not leak into the parent context") {
    it("complete case") { test_each_iteration_index_variable_does_not_leak_into_the_parent_context_5(); }
}

[[maybe_unused]] static void test_concurrent_script_tasks_complete_without_corrupting_turboscript_state_6() {
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

    check((context.getValueOrDefault<std::string>("workflow_status", "") == "success"));
    check_within(context.getValue<double>("script_a_value"), 42.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(42.0));
    check_within(context.getValue<double>("script_b_count"), 3.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(3.0));
}

spec("Concurrent script tasks complete without corrupting TurboScript state") {
    it("complete case") { test_concurrent_script_tasks_complete_without_corrupting_turboscript_state_6(); }
}
