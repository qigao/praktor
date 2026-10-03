#include "praktor.h"
#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path hostToolTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-host-tool-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeHostToolWorkflow(const std::filesystem::path& path) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << R"(
input_policy: strict
inputs:
  path:
    type: string
    required: true

tasks:
  - name: inspect
    tool: repo.inspect
    with:
      path: "{{ variables.path }}"
      limit: 2
)";
}

struct HostProbe {
    int validate_calls = 0;
    int invoke_calls = 0;
    bool deny = false;
    bool double_sink = false;
    bool saw_control = false;
    bool saw_observer = false;
    std::string template_json;
    std::string arguments_json;
};

int32_t PRAKTOR_CALL neverCancelled(void*) {
    return 0;
}

int32_t PRAKTOR_CALL validateTool(
    void* user_data,
    const char* tool_name,
    const char* arguments_template_json,
    size_t arguments_template_json_size,
    praktor_error* error) {
    auto* probe = static_cast<HostProbe*>(user_data);
    if (!probe || !tool_name || !arguments_template_json) {
        return PRAKTOR_HOST_TOOL_FAILED;
    }
    ++probe->validate_calls;
    probe->template_json.assign(
        arguments_template_json, arguments_template_json_size);

    if (std::string_view(tool_name) != "repo.inspect") {
        if (error) {
            error->phase = PRAKTOR_ERROR_PHASE_HOST_TOOL;
            std::snprintf(
                error->message, sizeof(error->message), "%s",
                "unexpected tool identity");
        }
        return PRAKTOR_HOST_TOOL_NOT_FOUND;
    }
    if (probe->deny) {
        if (error) {
            error->phase = PRAKTOR_ERROR_PHASE_HOST_TOOL;
            std::snprintf(
                error->message, sizeof(error->message), "%s",
                "tool denied by mock host");
        }
        return PRAKTOR_HOST_TOOL_DENIED;
    }
    return PRAKTOR_HOST_TOOL_OK;
}

int32_t PRAKTOR_CALL invokeTool(
    void* user_data,
    const char* tool_name,
    const char* arguments_json,
    size_t arguments_json_size,
    const praktor_execution_control* control,
    const praktor_execution_observer* observer,
    praktor_host_tool_result_sink_fn result_sink,
    void* result_sink_user_data,
    praktor_error*) {
    auto* probe = static_cast<HostProbe*>(user_data);
    if (!probe || !tool_name || !arguments_json || !result_sink) {
        return PRAKTOR_HOST_TOOL_FAILED;
    }
    ++probe->invoke_calls;
    probe->arguments_json.assign(arguments_json, arguments_json_size);

    probe->saw_control =
        control &&
        control->struct_size >= sizeof(praktor_execution_control) &&
        control->timeout_ms > 0 &&
        control->is_cancelled &&
        control->is_cancelled(control->user_data) == 0;

    probe->saw_observer =
        observer &&
        observer->struct_size >= sizeof(praktor_execution_observer) &&
        observer->thread_id &&
        observer->run_id &&
        observer->turn_id &&
        observer->tool_call_id &&
        std::string_view(observer->thread_id) == "thread-host" &&
        std::string_view(observer->run_id) == "run-host" &&
        std::string_view(observer->turn_id) == "turn-host" &&
        std::string_view(observer->tool_call_id) == "call-host";

    static const char result[] = "{\"status\":\"ok\",\"source\":\"host\"}";
    if (result_sink(result, sizeof(result) - 1u, result_sink_user_data) != 0) {
        return PRAKTOR_HOST_TOOL_FAILED;
    }
    if (probe->double_sink) {
        (void)result_sink(result, sizeof(result) - 1u, result_sink_user_data);
    }
    return PRAKTOR_HOST_TOOL_OK;
}

struct EventRecord {
    praktor_event_type type = PRAKTOR_EVENT_WORKFLOW_STARTED;
    std::string task_name;
    std::string thread_id;
    std::string run_id;
    std::string turn_id;
    std::string tool_call_id;
};

void PRAKTOR_CALL collectEvent(
    const praktor_execution_event* event,
    void* user_data) {
    auto* events = static_cast<std::vector<EventRecord>*>(user_data);
    if (!event || !events) {
        return;
    }
    EventRecord record;
    record.type = event->type;
    record.task_name = event->task_name ? event->task_name : "";
    record.thread_id = event->thread_id ? event->thread_id : "";
    record.run_id = event->run_id ? event->run_id : "";
    record.turn_id = event->turn_id ? event->turn_id : "";
    record.tool_call_id = event->tool_call_id ? event->tool_call_id : "";
    events->push_back(std::move(record));
}

praktor_workflow_plan* compilePlan(const std::filesystem::path& workflow) {
    const std::string path = workflow.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = path.c_str();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(praktor_compile_workflow(&request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(plan != nullptr);
    return plan;
}

praktor_plan_execute_request executeRequest(
    const praktor_workflow_plan* plan) {
    static const char input[] = "{\"path\":\"src\"}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1u;
    return request;
}

praktor_host_tool_executor hostExecutor(HostProbe& probe) {
    praktor_host_tool_executor executor = PRAKTOR_HOST_TOOL_EXECUTOR_INIT;
    executor.user_data = &probe;
    executor.validate = &validateTool;
    executor.invoke = &invokeTool;
    return executor;
}

praktor_execution_observer observerFor(std::vector<EventRecord>& events) {
    praktor_execution_observer observer = PRAKTOR_EXECUTION_OBSERVER_INIT;
    observer.on_event = &collectEvent;
    observer.user_data = &events;
    observer.thread_id = "thread-host";
    observer.run_id = "run-host";
    observer.turn_id = "turn-host";
    observer.tool_call_id = "call-host";
    return observer;
}

} // namespace

TEST_CASE("HostTool C ABI is additive and advertised",
          "[sdk][plan][host-tool]") {
    const praktor_api* api = praktor_get_api();
    REQUIRE(api != nullptr);
    CHECK(api->abi_major == PRAKTOR_ABI_MAJOR);
    CHECK(api->abi_minor >= 5);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_HOST_TOOL) != 0);
    REQUIRE(api->execute_workflow_plan_host_tools != nullptr);
}

TEST_CASE("reviewed HostTool plan preflights and returns typed result",
          "[sdk][plan][host-tool]") {
    const auto dir = hostToolTempDir();
    const auto workflow = dir / "workflow.yml";
    writeHostToolWorkflow(workflow);
    praktor_workflow_plan* plan = compilePlan(workflow);

    HostProbe probe;
    auto host = hostExecutor(probe);
    std::vector<EventRecord> events;
    auto observer = observerFor(events);

    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.is_cancelled = &neverCancelled;
    control.timeout_ms = 5000;

    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    REQUIRE(praktor_execute_workflow_plan_host_tools(
                &request, &control, &observer, &host, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(output.data != nullptr);
    CHECK(probe.validate_calls == 1);
    CHECK(probe.invoke_calls == 1);
    CHECK(probe.saw_control);
    CHECK(probe.saw_observer);

    const auto template_value = WorkflowValue::parse(probe.template_json);
    CHECK(template_value.at("path").as<std::string>() ==
          "{{ variables.path }}");
    CHECK(template_value.at("limit").as<int>() == 2);

    const auto arguments = WorkflowValue::parse(probe.arguments_json);
    CHECK(arguments.at("path").as<std::string>() == "src");
    CHECK(arguments.at("limit").as<int>() == 2);

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    CHECK(result.at("workflow_status").as<std::string>() == "success");
    const auto host_result =
        result.at("tasks").at("inspect").at("outputs").at("result");
    CHECK(host_result.at("status").as<std::string>() == "ok");
    CHECK(host_result.at("source").as<std::string>() == "host");

    bool saw_task_start = false;
    bool saw_task_complete = false;
    for (const auto& event : events) {
        CHECK(event.thread_id == "thread-host");
        CHECK(event.run_id == "run-host");
        CHECK(event.turn_id == "turn-host");
        CHECK(event.tool_call_id == "call-host");
        if (event.type == PRAKTOR_EVENT_TASK_STARTED &&
            event.task_name == "inspect") {
            saw_task_start = true;
        }
        if (event.type == PRAKTOR_EVENT_TASK_COMPLETED &&
            event.task_name == "inspect") {
            saw_task_complete = true;
        }
    }
    CHECK(saw_task_start);
    CHECK(saw_task_complete);

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("HostTool plan is rejected before workflow start when host denies",
          "[sdk][plan][host-tool]") {
    const auto dir = hostToolTempDir();
    const auto workflow = dir / "workflow.yml";
    writeHostToolWorkflow(workflow);
    praktor_workflow_plan* plan = compilePlan(workflow);

    HostProbe probe;
    probe.deny = true;
    auto host = hostExecutor(probe);
    std::vector<EventRecord> events;
    auto observer = observerFor(events);

    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    CHECK(praktor_execute_workflow_plan_host_tools(
              &request, nullptr, &observer, &host, &output, &error) ==
          PRAKTOR_RESULT_HOST_TOOL_REJECTED);
    CHECK(output.data == nullptr);
    CHECK(output.size == 0);
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_HOST_TOOL);
    CHECK(std::string_view(error.message).find("denied") !=
          std::string_view::npos);
    CHECK(probe.validate_calls == 1);
    CHECK(probe.invoke_calls == 0);
    CHECK(events.empty());

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("ordinary plan execution cannot bypass HostTool authority",
          "[sdk][plan][host-tool]") {
    const auto dir = hostToolTempDir();
    const auto workflow = dir / "workflow.yml";
    writeHostToolWorkflow(workflow);
    praktor_workflow_plan* plan = compilePlan(workflow);

    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    CHECK(praktor_execute_workflow_plan(
              &request, nullptr, &output, &error) ==
          PRAKTOR_RESULT_HOST_TOOL_REJECTED);
    CHECK(output.data == nullptr);
    CHECK(error.phase == PRAKTOR_ERROR_PHASE_HOST_TOOL);

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

TEST_CASE("HostTool success requires exactly one accepted result payload",
          "[sdk][plan][host-tool]") {
    const auto dir = hostToolTempDir();
    const auto workflow = dir / "workflow.yml";
    writeHostToolWorkflow(workflow);
    praktor_workflow_plan* plan = compilePlan(workflow);

    HostProbe probe;
    probe.double_sink = true;
    auto host = hostExecutor(probe);
    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    CHECK(praktor_execute_workflow_plan_host_tools(
              &request, nullptr, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_EXECUTION_FAILED);
    REQUIRE(output.data != nullptr);
    CHECK(probe.validate_calls == 1);
    CHECK(probe.invoke_calls == 1);

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    CHECK(result.at("workflow_status").as<std::string>() == "failed");
    const auto failure = result.at("agent_output").at("failure");
    CHECK(failure.at("error_phase").as<std::string>() == "host_tool");

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}
