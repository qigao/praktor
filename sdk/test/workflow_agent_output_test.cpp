#include "praktor.h"
#include "data/workflow_value.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::filesystem::path projectionTempDir() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               ("praktor-projection-" + std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeProjectionFile(const std::filesystem::path& path,
                         const std::string& content) {
    std::ofstream out(path);
    REQUIRE(out.is_open());
    out << content;
}

praktor_result executeProjection(const std::filesystem::path& workflow,
                                 praktor_owned_json& output,
                                 praktor_error& error) {
    static const char input[] = "{}";
    const std::string stable_path = workflow.string();
    praktor_execute_request request = PRAKTOR_EXECUTE_REQUEST_INIT;
    request.workflow_path = stable_path.c_str();
    request.input_json = input;
    request.input_json_size = sizeof(input) - 1;
    return praktor_execute_workflow(&request, &output, &error);
}

WorkflowValue parseProjection(const praktor_owned_json& output) {
    REQUIRE(output.data != nullptr);
    return WorkflowValue::parse(
        std::string_view(output.data, output.size));
}

} // namespace

TEST_CASE("agent_output exposes only declared workflow outputs",
          "[sdk][projection]") {
    const auto dir = projectionTempDir();
    const auto workflow = dir / "success.yml";

    writeProjectionFile(workflow, R"(
outputs:
  summary:
    type: string
    required: true
    value: "{{ tasks.public.outputs.stdout }}"

tasks:
  - name: public
    command: "echo public-summary"

  - name: secret
    depends_on: [public]
    command: "echo SECRET-RAW-DETAIL"
)");

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(executeProjection(workflow, output, error) ==
            PRAKTOR_RESULT_SUCCESS);

    const auto result = parseProjection(output);
    REQUIRE(result.at("tasks").is_object());
    CHECK(result.at("tasks").at("secret").at("outputs")
              .at("stdout").as<std::string>()
              .find("SECRET-RAW-DETAIL") != std::string::npos);

    const auto agent = result.at("agent_output");
    CHECK(agent.at("workflow_status").as<std::string>() == "success");
    REQUIRE(agent.at("outputs").is_object());
    CHECK(agent.at("outputs").at("summary").as<std::string>()
              .find("public-summary") != std::string::npos);
    CHECK_FALSE(agent.contains("tasks"));
    CHECK(agent.to_string().find("SECRET-RAW-DETAIL") == std::string::npos);

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

TEST_CASE("failure projection preserves failure identity without requiring outputs",
          "[sdk][projection]") {
    const auto dir = projectionTempDir();
    const auto workflow = dir / "failure.yml";

    writeProjectionFile(workflow, R"(
outputs:
  artifact:
    type: string
    required: true
    value: "{{ tasks.fail.outputs.artifact }}"

tasks:
  - name: fail
    program: "definitely-not-a-real-praktor-program"
)");

    praktor_owned_json output = PRAKTOR_OWNED_JSON_INIT;
    praktor_error error = PRAKTOR_ERROR_INIT;
    REQUIRE(executeProjection(workflow, output, error) ==
            PRAKTOR_RESULT_EXECUTION_FAILED);

    const auto result = parseProjection(output);
    CHECK(result.at("workflow_status").as<std::string>() == "failed");
    CHECK(result.at("error").as<std::string>() ==
          "Workflow execution failed");

    const auto agent = result.at("agent_output");
    CHECK(agent.at("workflow_status").as<std::string>() == "failed");
    CHECK(agent.at("error").as<std::string>() ==
          "Workflow execution failed");
    REQUIRE(agent.at("failure").is_object());
    CHECK(agent.at("failure").at("task_name").as<std::string>() == "fail");
    CHECK(agent.at("failure").at("task_type").as<std::string>() == "program");
    CHECK_FALSE(agent.at("failure").contains("stdout"));
    CHECK_FALSE(agent.at("failure").contains("stderr"));
    CHECK_FALSE(agent.contains("outputs"));

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}
