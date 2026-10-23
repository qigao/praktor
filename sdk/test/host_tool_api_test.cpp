#include "praktor_test.hpp"
#include "praktor.h"
#include "data/workflow_value.hpp"


#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

static_assert(PRAKTOR_HOST_TOOL_MAX_RETRIES == 1024u);

std::filesystem::path hostToolTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-host-tool-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

std::string inlineHostToolWorkflow() {
    return R"(
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

std::string inlineRetryHostToolWorkflow(std::uint32_t retry_count) {
    return
        "input_policy: strict\n"
        "inputs:\n"
        "  path:\n"
        "    type: string\n"
        "    required: true\n"
        "tasks:\n"
        "  - name: inspect\n"
        "    tool: repo.inspect\n"
        "    retries:\n"
        "      count: " + std::to_string(retry_count) + "\n"
        "    with:\n"
        "      path: \"{{ variables.path }}\"\n"
        "      limit: 2\n";
}

void writeHostToolWorkflow(const std::filesystem::path& path) {
    std::ofstream out(path);
    check((out.is_open()));
    out << inlineHostToolWorkflow();
}

struct HostProbe {
    int validate_calls = 0;
    int invoke_calls = 0;
    int fail_attempts = 0;
    bool force_timeout = false;
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

int32_t PRAKTOR_CALL cancelAfterFirstInvoke(void* user_data) {
    auto* probe = static_cast<HostProbe*>(user_data);
    return probe && probe->invoke_calls >= 1 ? 1 : 0;
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

    if (probe->force_timeout) {
        return PRAKTOR_HOST_TOOL_TIMED_OUT;
    }
    if (probe->invoke_calls <= probe->fail_attempts) {
        return PRAKTOR_HOST_TOOL_FAILED;
    }

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

praktor_workflow_plan* compileInlinePlan(
    const std::string& source_id,
    const std::string& source) {
    praktor_compile_inline_request request =
        PRAKTOR_COMPILE_INLINE_REQUEST_INIT;
    request.source_id = source_id.c_str();
    request.workflow_yaml = source.data();
    request.workflow_yaml_size = source.size();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_compile_workflow_inline(&request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((plan != nullptr));
    return plan;
}

praktor_workflow_plan* compilePlan(const std::filesystem::path& workflow) {
    const std::string path = workflow.string();
    praktor_compile_request request = PRAKTOR_COMPILE_REQUEST_INIT;
    request.workflow_path = path.c_str();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_compile_workflow(&request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((plan != nullptr));
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

[[maybe_unused]] static void test_hosttool_c_abi_is_additive_and_advertised_1() {
    const praktor_api* api = praktor_get_api();
    check((api != nullptr));
    check((api->abi_major == PRAKTOR_ABI_MAJOR));
    check((api->abi_minor >= 5));
    check(((api->capabilities & PRAKTOR_CAPABILITY_HOST_TOOL) != 0));
    check((api->execute_workflow_plan_host_tools != nullptr));
}

spec("HostTool C ABI is additive and advertised") {
    it("complete case") { test_hosttool_c_abi_is_additive_and_advertised_1(); }
}

[[maybe_unused]] static void test_reviewed_hosttool_plan_preflights_and_returns_typed_result_2() {
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

    check((praktor_execute_workflow_plan_host_tools(
                &request, &control, &observer, &host, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((output.data != nullptr));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 1));
    check((probe.saw_control));
    check((probe.saw_observer));

    const auto template_value = WorkflowValue::parse(probe.template_json);
    check((template_value.at("path").as<std::string>() ==
          "{{ variables.path }}"));
    check((template_value.at("limit").as<int>() == 2));

    const auto arguments = WorkflowValue::parse(probe.arguments_json);
    check((arguments.at("path").as<std::string>() == "src"));
    check((arguments.at("limit").as<int>() == 2));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "success"));
    const auto host_result =
        result.at("tasks").at("inspect").at("outputs").at("result");
    check((host_result.at("status").as<std::string>() == "ok"));
    check((host_result.at("source").as<std::string>() == "host"));

    bool saw_task_start = false;
    bool saw_task_complete = false;
    for (const auto& event : events) {
        check((event.thread_id == "thread-host"));
        check((event.run_id == "run-host"));
        check((event.turn_id == "turn-host"));
        check((event.tool_call_id == "call-host"));
        if (event.type == PRAKTOR_EVENT_TASK_STARTED &&
            event.task_name == "inspect") {
            saw_task_start = true;
        }
        if (event.type == PRAKTOR_EVENT_TASK_COMPLETED &&
            event.task_name == "inspect") {
            saw_task_complete = true;
        }
    }
    check((saw_task_start));
    check((saw_task_complete));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("reviewed HostTool plan preflights and returns typed result") {
    it("complete case") { test_reviewed_hosttool_plan_preflights_and_returns_typed_result_2(); }
}

[[maybe_unused]] static void test_hosttool_plan_is_rejected_before_workflow_start_when_host_denies_3() {
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

    check((praktor_execute_workflow_plan_host_tools(
              &request, nullptr, &observer, &host, &output, &error) ==
          PRAKTOR_RESULT_HOST_TOOL_REJECTED));
    check((output.data == nullptr));
    check((output.size == 0));
    check((error.phase == PRAKTOR_ERROR_PHASE_HOST_TOOL));
    check((std::string_view(error.message).find("denied") !=
          std::string_view::npos));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 0));
    check((events.empty()));

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("HostTool plan is rejected before workflow start when host denies") {
    it("complete case") { test_hosttool_plan_is_rejected_before_workflow_start_when_host_denies_3(); }
}

[[maybe_unused]] static void test_ordinary_plan_execution_cannot_bypass_hosttool_authority_4() {
    const auto dir = hostToolTempDir();
    const auto workflow = dir / "workflow.yml";
    writeHostToolWorkflow(workflow);
    praktor_workflow_plan* plan = compilePlan(workflow);

    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    check((praktor_execute_workflow_plan(
              &request, nullptr, &output, &error) ==
          PRAKTOR_RESULT_HOST_TOOL_REJECTED));
    check((output.data == nullptr));
    check((error.phase == PRAKTOR_ERROR_PHASE_HOST_TOOL));

    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("ordinary plan execution cannot bypass HostTool authority") {
    it("complete case") { test_ordinary_plan_execution_cannot_bypass_hosttool_authority_4(); }
}

[[maybe_unused]] static void test_hosttool_success_requires_exactly_one_accepted_result_payload_5() {
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

    check((praktor_execute_workflow_plan_host_tools(
              &request, nullptr, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_EXECUTION_FAILED));
    check((output.data != nullptr));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 1));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "failed"));
    const auto failure = result.at("agent_output").at("failure");
    check((failure.at("error_phase").as<std::string>() == "host_tool"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}

spec("HostTool success requires exactly one accepted result payload") {
    it("complete case") { test_hosttool_success_requires_exactly_one_accepted_result_payload_5(); }
}


[[maybe_unused]] static void test_inline_workflowplan_abi_is_additive_and_advertised_6() {
    const praktor_api* api = praktor_get_api();
    check((api != nullptr));
    check((api->abi_major == PRAKTOR_ABI_MAJOR));
    check((api->abi_minor >= 6));
    check(((api->capabilities & PRAKTOR_CAPABILITY_INLINE_WORKFLOW_PLAN) != 0));
    check((api->compile_workflow_inline != nullptr));
}

spec("inline WorkflowPlan ABI is additive and advertised") {
    it("complete case") { test_inline_workflowplan_abi_is_additive_and_advertised_6(); }
}

[[maybe_unused]] static void test_inline_hosttool_plan_owns_source_and_executes_without_filesystem_7() {
    std::string source = inlineHostToolWorkflow();
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:inline-test", source);

    // Caller storage is no longer authoritative after compile.
    source.assign("this is not valid workflow yaml anymore");

    praktor_owned_json metadata = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_describe_workflow_plan(plan, &metadata, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((metadata.data != nullptr));
    const auto description = WorkflowValue::parse(
        std::string_view(metadata.data, metadata.size));
    check((description.at("source_kind").as<std::string>() == "inline"));
    check((description.at("source_id").as<std::string>() ==
          "turboagent:plan:inline-test"));
    check((description.at("dependencies").size() == 0));
    check((!(description.at("digest").as<std::string>().empty())));
    praktor_release_json(&metadata);

    HostProbe probe;
    auto host = hostExecutor(probe);
    std::vector<EventRecord> events;
    auto observer = observerFor(events);
    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.is_cancelled = &neverCancelled;
    control.timeout_ms = 5000;

    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    check((praktor_execute_workflow_plan_host_tools(
                &request, &control, &observer, &host, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((output.data != nullptr));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 1));
    check((probe.saw_control));
    check((probe.saw_observer));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "success"));
    check((result.at("tasks").at("inspect").at("outputs").at("result")
              .at("status").as<std::string>() == "ok"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("inline HostTool plan owns source and executes without filesystem") {
    it("complete case") { test_inline_hosttool_plan_owns_source_and_executes_without_filesystem_7(); }
}

[[maybe_unused]] static void test_inline_workflowplan_rejects_external_and_dynamic_topology_8() {
    struct Case {
        const char* name;
        const char* source;
    };
    const Case cases[] = {
        {"include",
         "includes:\n  shared: other.yml\n"
         "tasks:\n  - name: inspect\n    tool: repo.inspect\n"},
        {"command",
         "tasks:\n  - name: run\n    command: echo forbidden\n"},
        {"dotenv",
         "dotEnv:\n  - .env\n"
         "tasks:\n  - name: inspect\n    tool: repo.inspect\n"},
        {"dynamic-each",
         "tasks:\n  - name: inspect\n    tool: repo.inspect\n"
         "    each:\n      items: [a, b]\n"},
        {"trigger",
         "tasks:\n"
         "  - name: inspect\n    tool: repo.inspect\n"
         "    triggers:\n      on_success: [verify]\n"
         "  - name: verify\n    tool: repo.inspect\n"},
        {"cycle",
         "tasks:\n"
         "  - name: a\n    tool: repo.inspect\n    depends_on: [b]\n"
         "  - name: b\n    tool: repo.inspect\n    depends_on: [a]\n"},
    };

    for (const auto& test_case : cases) {
        info("%s", praktor_test::message([&](std::ostream& out) { out << test_case.name; }).c_str());
        praktor_compile_inline_request request =
            PRAKTOR_COMPILE_INLINE_REQUEST_INIT;
        request.source_id = "turboagent:plan:reject";
        request.workflow_yaml = test_case.source;
        request.workflow_yaml_size = std::strlen(test_case.source);
        praktor_workflow_plan* plan = nullptr;
        praktor_error error = PRAKTOR_ERROR_INIT;
        check((praktor_compile_workflow_inline(&request, &plan, &error) ==
              PRAKTOR_RESULT_PLAN_INVALID));
        check((plan == nullptr));
        check((error.phase == PRAKTOR_ERROR_PHASE_PLAN));
    }

    const std::string valid = inlineHostToolWorkflow();
    praktor_compile_inline_request bad_id =
        PRAKTOR_COMPILE_INLINE_REQUEST_INIT;
    bad_id.source_id = "tmp/plan.yml";
    bad_id.workflow_yaml = valid.data();
    bad_id.workflow_yaml_size = valid.size();
    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_compile_workflow_inline(&bad_id, &plan, &error) ==
          PRAKTOR_RESULT_PLAN_INVALID));
    check((plan == nullptr));
}

spec("inline WorkflowPlan rejects external and dynamic topology") {
    it("complete case") { test_inline_workflowplan_rejects_external_and_dynamic_topology_8(); }
}

[[maybe_unused]] static void test_inline_plan_preserves_input_contract_before_hosttool_invocation_9() {
    const std::string source = inlineHostToolWorkflow();
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:input-contract", source);

    HostProbe probe;
    auto host = hostExecutor(probe);
    static const char input[] = "{}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1u;
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    check((praktor_execute_workflow_plan_host_tools(
              &request, nullptr, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_INPUT_CONTRACT));
    check((output.data == nullptr));
    check((probe.invoke_calls == 0));

    praktor_release_workflow_plan(plan);
}

spec("inline plan preserves input contract before HostTool invocation") {
    it("complete case") { test_inline_plan_preserves_input_contract_before_hosttool_invocation_9(); }
}

[[maybe_unused]] static void test_inline_strict_plans_with_no_input_fields_reject_unknown_inputs_before_invocation_10(std::size_t row_0, std::size_t row_1, std::size_t row_2) {
    const std::string policy = std::array{"strict", "allow_extra"}.at(row_0);
    const bool explicit_empty = std::array{false, true}.at(row_1);
    const std::string source = "input_policy: " + policy + "\n" +
        (explicit_empty ? "inputs: {}\n" : "") +
        "tasks:\n  - name: inspect\n    tool: repo.inspect\n    with: {}\n";
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:empty-input-contract", source);

    HostProbe probe;
    auto host = hostExecutor(probe);
    const std::string input = std::array{std::string("{}"), std::string(R"({"extra":true})")}.at(row_2);
    const bool rejects = policy == "strict" && input != "{}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input.data();
    request.input_json_size = input.size();
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_execute_workflow_plan_host_tools(
              &request, nullptr, nullptr, &host, &output, &error) ==
          (rejects ? PRAKTOR_RESULT_INPUT_CONTRACT : PRAKTOR_RESULT_SUCCESS)));
    check((probe.invoke_calls == (rejects ? 0 : 1)));
    if (rejects) {
        check((error.phase == PRAKTOR_ERROR_PHASE_INPUT_CONTRACT));
        check((output.data == nullptr));
        check((output.size == 0));
    } else {
        check((WorkflowValue::parse(std::string_view(output.data, output.size))
                  .at("workflow_status").as<std::string>() == "success"));
    }
    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("inline strict plans with no input fields reject unknown inputs before invocation") {
    for (std::size_t row_0 = 0; row_0 < 2; ++row_0) {
        for (std::size_t row_1 = 0; row_1 < 2; ++row_1) {
            for (std::size_t row_2 = 0; row_2 < 2; ++row_2) {
                it("%s / row %zu %zu %zu", "complete case", row_0, row_1, row_2) { test_inline_strict_plans_with_no_input_fields_reject_unknown_inputs_before_invocation_10(row_0, row_1, row_2); }
            }
        }
    }
}


[[maybe_unused]] static void test_inline_hosttool_retries_only_the_admitted_task_invocation_11() {
    const std::string source = inlineRetryHostToolWorkflow(2u);
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:retry-success", source);

    praktor_owned_json metadata = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    check((praktor_describe_workflow_plan(plan, &metadata, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    const auto description = WorkflowValue::parse(
        std::string_view(metadata.data, metadata.size));
    check((description.at("host_tools").size() == 1));
    check((description.at("host_tools").at(0).at("retry_count").as<int>() == 2));
    praktor_release_json(&metadata);

    HostProbe probe;
    probe.fail_attempts = 2;
    auto host = hostExecutor(probe);
    auto request = executeRequest(plan);
    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.is_cancelled = &neverCancelled;
    control.timeout_ms = 5000;
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;

    check((praktor_execute_workflow_plan_host_tools(
                &request, &control, nullptr, &host, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 3));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "success"));
    check((result.at("tasks").at("inspect").at("outputs").at("result")
              .at("status").as<std::string>() == "ok"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("inline HostTool retries only the admitted task invocation") {
    it("complete case") { test_inline_hosttool_retries_only_the_admitted_task_invocation_11(); }
}

[[maybe_unused]] static void test_inline_hosttool_retry_exhaustion_is_finite_and_deterministic_12() {
    const std::string source = inlineRetryHostToolWorkflow(1u);
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:retry-exhausted", source);

    HostProbe probe;
    probe.fail_attempts = 4;
    auto host = hostExecutor(probe);
    auto request = executeRequest(plan);
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    check((praktor_execute_workflow_plan_host_tools(
              &request, nullptr, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_EXECUTION_FAILED));
    check((probe.validate_calls == 1));
    check((probe.invoke_calls == 2));
    check((output.data != nullptr));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "failed"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("inline HostTool retry exhaustion is finite and deterministic") {
    it("complete case") { test_inline_hosttool_retry_exhaustion_is_finite_and_deterministic_12(); }
}

[[maybe_unused]] static void test_hosttool_retry_stops_before_a_cancelled_next_attempt_13() {
    const std::string source = inlineRetryHostToolWorkflow(3u);
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:retry-cancel", source);

    HostProbe probe;
    probe.fail_attempts = 4;
    auto host = hostExecutor(probe);
    auto request = executeRequest(plan);
    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.user_data = &probe;
    control.is_cancelled = &cancelAfterFirstInvoke;
    control.timeout_ms = 5000;
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    check((praktor_execute_workflow_plan_host_tools(
              &request, &control, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_CANCELLED));
    check((probe.invoke_calls == 1));
    check((output.data != nullptr));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "cancelled"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("HostTool retry stops before a cancelled next attempt") {
    it("complete case") { test_hosttool_retry_stops_before_a_cancelled_next_attempt_13(); }
}

[[maybe_unused]] static void test_hosttool_timeout_is_terminal_and_is_never_retried_14() {
    const std::string source = inlineRetryHostToolWorkflow(3u);
    praktor_workflow_plan* plan =
        compileInlinePlan("turboagent:plan:retry-timeout", source);

    HostProbe probe;
    probe.force_timeout = true;
    auto host = hostExecutor(probe);
    auto request = executeRequest(plan);
    praktor_execution_control control = PRAKTOR_EXECUTION_CONTROL_INIT;
    control.is_cancelled = &neverCancelled;
    control.timeout_ms = 5000;
    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;

    check((praktor_execute_workflow_plan_host_tools(
              &request, &control, nullptr, &host, &output, &error) ==
          PRAKTOR_RESULT_EXECUTION_FAILED));
    check((probe.invoke_calls == 1));
    check((output.data != nullptr));

    const auto result = WorkflowValue::parse(
        std::string_view(output.data, output.size));
    check((result.at("workflow_status").as<std::string>() == "failed"));

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
}

spec("HostTool timeout is terminal and is never retried") {
    it("complete case") { test_hosttool_timeout_is_terminal_and_is_never_retried_14(); }
}
