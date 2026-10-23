#include "praktor_test.hpp"
#include "dag/workflow_context.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

namespace {

bool testCancellationProbe(void* user_data) noexcept {
    return static_cast<std::atomic<bool>*>(user_data)->load(std::memory_order_acquire);
}

} // namespace

[[maybe_unused]] static void test_workflowcontext_getvariable_default_value_1(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        check((context.getVariable("nonexistent") == ""));
        check((context.getVariable("nonexistent").empty()));
    }

    if (section_index == 1) {
        context.setVariable("mykey", "myvalue");
        check((context.getVariable("mykey") == "myvalue"));
    }
}

spec("WorkflowContext::getVariable default value") {
    it("Returns empty string for missing key") { test_workflowcontext_getvariable_default_value_1(0); }
    it("Returns actual value when key exists") { test_workflowcontext_getvariable_default_value_1(1); }
}

[[maybe_unused]] static void test_workflowcontext_core_functionality_2(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        context.setValue("my_string", std::string("hello"));
        context.setValue("my_int", 42);
        context.setValue("my_bool", true);

        check((context.hasKey("my_string")));
        check((context.getValue<std::string>("my_string") == "hello"));
        check((context.getValue<int>("my_int") == 42));
        check((context.getValue<bool>("my_bool") == true));
    }

    if (section_index == 1) {
        context.setValue("key", std::string("initial"));
        check((context.getValue<std::string>("key") == "initial"));

        context.setValue("key", std::string("overwritten"));
        check((context.getValue<std::string>("key") == "overwritten"));
    }

    if (section_index == 2) {
        context.setValue("a", 1);
        check((context.hasKey("a")));
        check_false(context.hasKey("b"));
    }

    if (section_index == 3) {
        context.setValue("a", std::string("actual"));
        check((context.getValueOrDefault<std::string>("a", "default") == "actual"));
        check((context.getValueOrDefault<std::string>("b", "default") == "default"));
    }

    if (section_index == 4) {
        WorkflowValue payload = WorkflowValue::object();
        payload["meta"] = WorkflowValue::object();
        payload["meta"]["region"] = "apac";
        context.setValue("payload", payload);

        check((context.getValueByPath("payload.meta.region").as<std::string>() == "apac"));
    }

    if (section_index == 5) {
        context.setTaskStatus("build", "running");
        context.setTaskOutput("build", "artifact", "pkg.zip");
        context.setTaskStatus("build", "success");

        check((context.getValueByPath("tasks.build.status").as<std::string>() == "success"));
        check((context.getValueByPath("tasks.build.outputs.artifact").as<std::string>() == "pkg.zip"));
    }

    if (section_index == 6) {
        WorkflowValue payload = WorkflowValue::object();
        payload["servers"] = WorkflowValue::array();
        payload["servers"].push_back(WorkflowValue::object({{"name", "api"}, {"port", 8080}}));
        payload["servers"].push_back(WorkflowValue::object({{"name", "worker"}, {"port", 9090}}));
        context.setValue("payload", payload);

        auto ports = context.getJsonValue("payload", "servers[*].port");
        check((ports.is_array()));
        check((ports.size() == 2));
        check((ports[0].as<int>() == 8080));
        check((ports[1].as<int>() == 9090));
    }

    if (section_index == 7) {
        context.addCompletedTask("task_A");
        context.addCompletedTask("task_B");
        context.addFailedTask("task_C", "it broke");

        check((context.getCompletedTasks().size() == 2));
        check((context.getFailedTasks().size() == 1));

        check((context.getCompletedTasks().count("task_A")));
        check((!(context.getCompletedTasks().count("task_C"))));

        check((context.getFailedTasks().count("task_C")));
        check((context.getFailedTasks().at("task_C") == "it broke"));
    }

    if (section_index == 8) {
        TaskFailureContext failure;
        failure.task_name = "deploy";
        failure.task_type = "script";
        failure.error_message = "boom";
        failure.captured_outputs["artifact"] = "pkg.zip";

        context.setFailureContext(failure);

        check((context.isInFailureContext()));
        check((context.getVariable("failed_task_name") == "deploy"));
        check((context.getVariable("failed_task_type") == "script"));
        check((context.getValueByPath("failed_task.outputs.artifact").as<std::string>() == "pkg.zip"));

        context.clearFailureContext();

        check_false(context.isInFailureContext());
        check((context.getVariable("failed_task_name").empty()));
        check((context.getValueByPath("failed_task").is_null()));
    }

    if (section_index == 9) {
        context.setSourcePath("/tmp/praktor/workflow.yml");

        auto child = context.fork();

        check((child->getSourcePath() == "/tmp/praktor/workflow.yml"));
    }

    if (section_index == 10) {
        auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
        context.setExecutionControl(control);

        auto child = context.fork();

        check((child->getExecutionControl() == control));
        child->getExecutionControl()->requestCancel();
        check((context.getExecutionControl()->stopReason() ==
              Praktor::Execution::ExecutionControl::StopReason::Cancelled));
    }
}

spec("WorkflowContext Core Functionality") {
    it("Set and Get basic values") { test_workflowcontext_core_functionality_2(0); }
    it("Overwrite existing value") { test_workflowcontext_core_functionality_2(1); }
    it("hasKey works correctly") { test_workflowcontext_core_functionality_2(2); }
    it("getValueOrDefault provides default") { test_workflowcontext_core_functionality_2(3); }
    it("Nested paths resolve through JSON objects") { test_workflowcontext_core_functionality_2(4); }
    it("Task paths resolve through the task registry facade") { test_workflowcontext_core_functionality_2(5); }
    it("JSONPath queries work through the facade") { test_workflowcontext_core_functionality_2(6); }
    it("Task Status Tracking") { test_workflowcontext_core_functionality_2(7); }
    it("Failure context variables are scoped and removable") { test_workflowcontext_core_functionality_2(8); }
    it("Source path is copied on fork") { test_workflowcontext_core_functionality_2(9); }
    it("Execution control is shared across forked contexts") { test_workflowcontext_core_functionality_2(10); }
}

[[maybe_unused]] static void test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(int section_index) {
    using Control = Praktor::Execution::ExecutionControl;
    using namespace std::chrono_literals;

    if (section_index == 0) {
        Control control;
        check((!(control.stopRequested())));
        check((control.stopReason() == Control::StopReason::None));
        check((!(control.deadline().has_value())));
        check((!(control.remainingDeadline().has_value())));
    }

    if (section_index == 1) {
        Control control;
        control.requestCancel();

        check((control.cancellationRequested()));
        check((control.stopRequested()));
        check((control.stopReason() == Control::StopReason::Cancelled));
    }

    if (section_index == 2) {
        std::atomic<bool> requested{false};
        Control control(std::nullopt, testCancellationProbe, &requested);

        check((!(control.stopRequested())));

        requested.store(true, std::memory_order_release);
        check((control.cancellationRequested()));
        check((control.stopReason() == Control::StopReason::Cancelled));

        requested.store(false, std::memory_order_release);
        check((control.stopReason() == Control::StopReason::Cancelled));
    }

    if (section_index == 3) {
        Control control(Control::Clock::now() - 1ms);

        check((control.stopRequested()));
        check((control.stopReason() == Control::StopReason::DeadlineExceeded));
        check((control.remainingDeadline().has_value()));
        check((*control.remainingDeadline() == Control::Clock::duration::zero()));
    }

    if (section_index == 4) {
        Control control(Control::Clock::now() + 1h);
        control.requestCancel();

        check((control.stopReason() == Control::StopReason::Cancelled));
        check((control.remainingDeadline().has_value()));
        check((*control.remainingDeadline() > Control::Clock::duration::zero()));
    }

    if (section_index == 5) {
        Control control(Control::Clock::now() - 1ms);
        control.requestCancel();

        check((control.stopReason() == Control::StopReason::DeadlineExceeded));
    }
}

spec("ExecutionControl resolves cancellation and deadline deterministically") {
    it("No signal leaves execution active") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(0); }
    it("Cancellation is visible without a polling thread") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(1); }
    it("Host cancellation probe is observed lazily and latched") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(2); }
    it("Expired deadline is distinguished from cancellation") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(3); }
    it("Earlier cancellation wins over a later deadline") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(4); }
    it("An already-expired deadline remains terminal after cancellation") { test_executioncontrol_resolves_cancellation_and_deadline_deterministically_3(5); }
}
