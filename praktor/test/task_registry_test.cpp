#include "praktor_test.hpp"
#include "dag/task_registry.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>

using InnerFailureElement =
    typename decltype(TaskFailureContext{}.inner_failure)::element_type;
static_assert(std::is_const_v<InnerFailureElement>);

[[maybe_unused]] static void test_taskregistry_state_machine_1(int section_index) {
    TaskRegistry registry;

    if (section_index == 0) {
        check((registry.getState("new_task") == TaskState::Pending));
        check((registry.getStatus("new_task") == "pending"));
    }

    if (section_index == 1) {
        registry.startTask("task_a");
        check((registry.getState("task_a") == TaskState::Running));
        check((registry.getStatus("task_a") == "running"));
    }

    if (section_index == 2) {
        registry.startTask("task_a");
        registry.markCompleted("task_a");
        check((registry.getState("task_a") == TaskState::Completed));
        check((registry.isCompleted("task_a")));
        check((registry.isFinalized("task_a")));
    }

    if (section_index == 3) {
        registry.startTask("task_a");
        registry.markFailed("task_a", "something went wrong");
        check((registry.getState("task_a") == TaskState::Failed));
        check((registry.isFailed("task_a")));
        check((registry.isFinalized("task_a")));
        check((registry.getFailureReason("task_a") == "something went wrong"));
    }

    if (section_index == 4) {
        registry.startTask("task_a");
        registry.setOutput("task_a", "before", WorkflowValue("stale"));
        registry.markCompleted("task_a");
        check((registry.isCompleted("task_a")));

        // Restarting already completed task should work
        registry.startTask("task_a");
        check((registry.getState("task_a") == TaskState::Running));
        check_false(registry.isCompleted("task_a"));
        check((registry.getAllOutputs("task_a").empty()));

        registry.markFailed("task_a", "re-entry failure");
        check((registry.isFailed("task_a")));
        check((registry.getFailureReason("task_a") == "re-entry failure"));
    }

    if (section_index == 5) {
        registry.startTask("task_a");
        check_nothrow(registry.startTask("task_a"));
        check((registry.getState("task_a") == TaskState::Running));
    }
}

spec("TaskRegistry State Machine") {
    it("Initial state is Pending") { test_taskregistry_state_machine_1(0); }
    it("startTask transitions Pending -> Running") { test_taskregistry_state_machine_1(1); }
    it("markCompleted transitions Running -> Completed") { test_taskregistry_state_machine_1(2); }
    it("markFailed transitions Running -> Failed") { test_taskregistry_state_machine_1(3); }
    it("Task re-entry support") { test_taskregistry_state_machine_1(4); }
    it("startTask can be called multiple times") { test_taskregistry_state_machine_1(5); }
}

[[maybe_unused]] static void test_taskregistry_output_immutability_2(int section_index) {
    TaskRegistry registry;

    if (section_index == 0) {
        registry.startTask("task_a");
        check_nothrow(registry.setOutput("task_a", "result", WorkflowValue("value")));

        auto outputs = registry.getAllOutputs("task_a");
        check((outputs["result"].as<std::string>() == "value"));
    }

    if (section_index == 1) {
        check((registry.getState("task_a") == TaskState::Pending));
        registry.setOutput("task_a", "key", WorkflowValue(42));
        check((registry.getState("task_a") == TaskState::Running));
    }

    if (section_index == 2) {
        registry.startTask("task_a");
        registry.setOutput("task_a", "before", WorkflowValue("ok"));
        registry.markCompleted("task_a");

        check((praktor_test::throws_containing([&] { (void)(registry.setOutput("task_a", "after", WorkflowValue("fail"))); }, {"immutable after completion"})));
    }

    if (section_index == 3) {
        registry.startTask("task_a");
        registry.markFailed("task_a", "error");

        check((praktor_test::throws_containing([&] { (void)(registry.setOutput("task_a", "key", WorkflowValue("value"))); }, {"immutable after completion"})));
    }

    if (section_index == 4) {
        registry.startTask("task_a");

        WorkflowValue outputs = WorkflowValue::object();
        outputs["stdout"] = "hello world";
        outputs["exit_code"] = 0;

        check_nothrow(registry.mergeOutputs("task_a", outputs));

        auto all = registry.getAllOutputs("task_a");
        check((all["stdout"].as<std::string>() == "hello world"));
        check((all["exit_code"].as<int>() == 0));
    }
}

spec("TaskRegistry Output Immutability") {
    it("setOutput allowed during Running state") { test_taskregistry_output_immutability_2(0); }
    it("setOutput auto-starts Pending task") { test_taskregistry_output_immutability_2(1); }
    it("setOutput throws after Completed") { test_taskregistry_output_immutability_2(2); }
    it("setOutput throws after Failed") { test_taskregistry_output_immutability_2(3); }
    it("mergeOutputs works during Running") { test_taskregistry_output_immutability_2(4); }
}

[[maybe_unused]] static void test_taskregistry_output_access_3(int section_index) {
    TaskRegistry registry;

    if (section_index == 0) {
        registry.startTask("task_a");
        registry.setOutput("task_a", "data", WorkflowValue::array({1, 2, 3}));
        registry.markCompleted("task_a");

        auto data = registry.getOutput("task_a", "data");
        check((data.is_array()));
        check((data.size() == 3));
    }

    if (section_index == 1) {
        check((praktor_test::throws_containing([&] { (void)(registry.getOutput("nonexistent", "key")); }, {"Task not found"})));
    }

    if (section_index == 2) {
        registry.startTask("task_a");
        registry.setOutput("task_a", "exists", WorkflowValue(true));
        registry.markCompleted("task_a");

        check((praktor_test::throws_containing([&] { (void)(registry.getOutput("task_a", "missing")); }, {"Output key not found"})));
    }

    if (section_index == 3) {
        registry.startTask("task_a");
        registry.markCompleted("task_a");

        auto outputs = registry.getAllOutputs("task_a");
        check((outputs.is_object()));
        check((outputs.empty()));
    }
}

spec("TaskRegistry Output Access") {
    it("getOutput returns correct value") { test_taskregistry_output_access_3(0); }
    it("getOutput throws for missing task") { test_taskregistry_output_access_3(1); }
    it("getOutput throws for missing key") { test_taskregistry_output_access_3(2); }
    it("getAllOutputs returns empty object for task without outputs") { test_taskregistry_output_access_3(3); }
}

[[maybe_unused]] static void test_taskregistry_backward_compatibility_4(int section_index) {
    TaskRegistry registry;

    if (section_index == 0) {
        registry.setStatus("task_a", "running");
        check((registry.getState("task_a") == TaskState::Running));
    }

    if (section_index == 1) {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "success");
        check((registry.getState("task_a") == TaskState::Completed));
        check((registry.isCompleted("task_a")));
    }

    if (section_index == 2) {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "completed");
        check((registry.isCompleted("task_a")));
    }

    if (section_index == 3) {
        registry.setStatus("task_a", "skipped");
        check((registry.getState("task_a") == TaskState::Skipped));
        check((registry.isCompleted("task_a")));
    }

    if (section_index == 4) {
        registry.setStatus("task_a", "running");
        registry.setStatus("task_a", "failed");
        check((registry.getState("task_a") == TaskState::Failed));
    }
}

spec("TaskRegistry Backward Compatibility") {
    it("setStatus with 'running' starts task") { test_taskregistry_backward_compatibility_4(0); }
    it("setStatus with 'success' completes task") { test_taskregistry_backward_compatibility_4(1); }
    it("setStatus with 'completed' completes task") { test_taskregistry_backward_compatibility_4(2); }
    it("setStatus with 'skipped' marks as completed") { test_taskregistry_backward_compatibility_4(3); }
    it("setStatus with 'failed' marks as failed") { test_taskregistry_backward_compatibility_4(4); }
}

[[maybe_unused]] static void test_taskregistry_tojson_5() {
    TaskRegistry registry;

    registry.startTask("task_a");
    registry.setOutput("task_a", "result", WorkflowValue("success"));
    registry.markCompleted("task_a");

    registry.startTask("task_b");
    registry.markFailed("task_b", "error");

    auto json = registry.toJson();

    check((json.is_object()));
    check((json.contains("task_a")));
    check((json.contains("task_b")));

    check((json["task_a"]["status"].as<std::string>() == "success"));
    check((json["task_a"]["outputs"]["result"].as<std::string>() == "success"));

    check((json["task_b"]["status"].as<std::string>() == "failed"));
}

spec("TaskRegistry toJson") {
    it("complete case") { test_taskregistry_tojson_5(); }
}

[[maybe_unused]] static void test_taskregistry_query_methods_6(int section_index) {
    TaskRegistry registry;

    registry.startTask("a");
    registry.markCompleted("a");

    registry.startTask("b");
    registry.markCompleted("b");

    registry.startTask("c");
    registry.markFailed("c", "error c");

    registry.startTask("d");
    registry.markFailed("d", "error d");

    if (section_index == 0) {
        auto completed = registry.getCompletedTasks();
        check((completed.size() == 2));
        check((completed.count("a")));
        check((completed.count("b")));
    }

    if (section_index == 1) {
        auto failed = registry.getFailedTasks();
        check((failed.size() == 2));
        check((failed["c"] == "error c"));
        check((failed["d"] == "error d"));
    }
}

spec("TaskRegistry Query Methods") {
    it("getCompletedTasks returns all completed") { test_taskregistry_query_methods_6(0); }
    it("getFailedTasks returns all failed with reasons") { test_taskregistry_query_methods_6(1); }
}

[[maybe_unused]] static void test_taskregistry_failure_snapshot_is_assigned_once_7() {
    TaskRegistry registry;
    registry.startTask("task_a");

    TaskFailureContext first;
    first.task_name = "task_a";
    first.error_code = "timeout";
    first.error_message = "first";
    registry.markFailed("task_a", first);

    TaskFailureContext second;
    second.task_name = "task_a";
    second.error_code = "service_state_failed";
    second.error_message = "second";
    check_throws_as(registry.markFailed("task_a", second), std::logic_error);

    const auto stored = registry.getFailureSnapshot("task_a");
    check((stored.has_value()));
    check((stored->error_code == "timeout"));
    check((stored->error_message == "first"));
}

spec("TaskRegistry failure snapshot is assigned once") {
    it("complete case") { test_taskregistry_failure_snapshot_is_assigned_once_7(); }
}

[[maybe_unused]] static void test_taskregistry_rejects_failure_after_another_terminal_state_8(int section_index) {
    if (section_index == 0) {
        TaskRegistry registry;
        registry.startTask("task_a");
        registry.markCompleted("task_a");

        check_throws_as(registry.markFailed("task_a", "late failure"),
                        std::logic_error);
        check((registry.getState("task_a") == TaskState::Completed));
        check((registry.isCompleted("task_a")));
        check((!(registry.getFailureSnapshot("task_a").has_value())));
    }

    if (section_index == 1) {
        TaskRegistry registry;
        registry.setStatus("task_a", "skipped");

        check_throws_as(registry.markFailed("task_a", "late failure"),
                        std::logic_error);
        check((registry.getState("task_a") == TaskState::Skipped));
        check((registry.isCompleted("task_a")));
        check((!(registry.getFailureSnapshot("task_a").has_value())));
    }
}

spec("TaskRegistry rejects failure after another terminal state") {
    it("completed") { test_taskregistry_rejects_failure_after_another_terminal_state_8(0); }
    it("skipped") { test_taskregistry_rejects_failure_after_another_terminal_state_8(1); }
}
