#include "praktor.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path eventTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-events-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeEventFile(const std::filesystem::path& path,
                    const std::string& content) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << content;
}

struct EventRecord {
    std::uint64_t sequence = 0;
    praktor_event_type type = PRAKTOR_EVENT_WORKFLOW_STARTED;
    std::string task_name;
    std::string status;
    std::string message;
    std::string thread_id;
    std::string run_id;
    std::string turn_id;
    std::string tool_call_id;
};

void PRAKTOR_CALL collectEvent(const praktor_execution_event* event,
                               void* user_data) {
    auto* records = static_cast<std::vector<EventRecord>*>(user_data);
    if (!event || !records) {
        return;
    }

    EventRecord record;
    record.sequence = event->sequence;
    record.type = event->type;
    record.task_name = event->task_name ? event->task_name : "";
    record.status = event->status ? event->status : "";
    record.message = event->message ? event->message : "";
    record.thread_id = event->thread_id ? event->thread_id : "";
    record.run_id = event->run_id ? event->run_id : "";
    record.turn_id = event->turn_id ? event->turn_id : "";
    record.tool_call_id = event->tool_call_id ? event->tool_call_id : "";
    records->push_back(std::move(record));
}

} // namespace

TEST_CASE("WorkflowPlan observed execution emits ordered lifecycle events with lineage",
          "[sdk][plan][events]") {
    const praktor_api* api = praktor_get_api();
    REQUIRE(api != nullptr);
    CHECK(api->abi_minor >= 4);
    CHECK((api->capabilities & PRAKTOR_CAPABILITY_EXECUTION_EVENTS) != 0);
    REQUIRE(api->execute_workflow_plan_observed != nullptr);

    const auto dir = eventTempDir();
    const auto workflow = dir / "workflow.yml";
    writeEventFile(workflow, R"(
tasks:
  - name: prepare
    command: "echo prepare"
  - name: verify
    depends_on: [prepare]
    command: "echo verify"
)");

    const std::string stable_path = workflow.string();
    praktor_compile_request compile_request = PRAKTOR_COMPILE_REQUEST_INIT;
    compile_request.workflow_path = stable_path.c_str();

    praktor_workflow_plan* plan = nullptr;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(praktor_compile_workflow(&compile_request, &plan, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(plan != nullptr);

    static const char input[] = "{}";
    praktor_plan_execute_request request = PRAKTOR_PLAN_EXECUTE_REQUEST_INIT;
    request.plan = plan;
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;

    std::vector<EventRecord> events;
    praktor_execution_observer observer = PRAKTOR_EXECUTION_OBSERVER_INIT;
    observer.on_event = &collectEvent;
    observer.user_data = &events;
    observer.thread_id = "thread-17";
    observer.run_id = "run-9";
    observer.turn_id = "turn-3";
    observer.tool_call_id = "call-42";

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    REQUIRE(praktor_execute_workflow_plan_observed(
                &request, nullptr, &observer, &output, &error) ==
            PRAKTOR_RESULT_SUCCESS);
    REQUIRE(output.data != nullptr);

    REQUIRE(events.size() == 6);
    for (std::size_t index = 0; index < events.size(); ++index) {
        CHECK(events[index].sequence == index + 1);
        CHECK(events[index].thread_id == "thread-17");
        CHECK(events[index].run_id == "run-9");
        CHECK(events[index].turn_id == "turn-3");
        CHECK(events[index].tool_call_id == "call-42");
    }

    CHECK(events[0].type == PRAKTOR_EVENT_WORKFLOW_STARTED);
    CHECK(events[0].status == "running");

    CHECK(events[1].type == PRAKTOR_EVENT_TASK_STARTED);
    CHECK(events[1].task_name == "prepare");
    CHECK(events[2].type == PRAKTOR_EVENT_TASK_COMPLETED);
    CHECK(events[2].task_name == "prepare");
    CHECK(events[2].status == "success");

    CHECK(events[3].type == PRAKTOR_EVENT_TASK_STARTED);
    CHECK(events[3].task_name == "verify");
    CHECK(events[4].type == PRAKTOR_EVENT_TASK_COMPLETED);
    CHECK(events[4].task_name == "verify");
    CHECK(events[4].status == "success");

    CHECK(events[5].type == PRAKTOR_EVENT_WORKFLOW_COMPLETED);
    CHECK(events[5].status == "success");

    praktor_release_json(&output);
    praktor_release_workflow_plan(plan);
    std::filesystem::remove_all(dir);
}
