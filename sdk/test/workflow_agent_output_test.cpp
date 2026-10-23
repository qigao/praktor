#include "praktor_test.hpp"
#include "praktor.h"
#include "data/workflow_value.hpp"


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
    check((out.is_open()));
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
    check((output.data != nullptr));
    return WorkflowValue::parse(
        std::string_view(output.data, output.size));
}

} // namespace

[[maybe_unused]] static void test_agent_output_exposes_only_declared_workflow_outputs_1() {
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
    check((executeProjection(workflow, output, error) ==
            PRAKTOR_RESULT_SUCCESS));

    const auto result = parseProjection(output);
    check((result.at("tasks").is_object()));
    check((result.at("tasks").at("secret").at("outputs")
              .at("stdout").as<std::string>()
              .find("SECRET-RAW-DETAIL") != std::string::npos));

    const auto agent = result.at("agent_output");
    check((agent.at("workflow_status").as<std::string>() == "success"));
    check((agent.at("outputs").is_object()));
    check((agent.at("outputs").at("summary").as<std::string>()
              .find("public-summary") != std::string::npos));
    check((!(agent.contains("tasks"))));
    check((agent.to_string().find("SECRET-RAW-DETAIL") == std::string::npos));

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

spec("agent_output exposes only declared workflow outputs") {
    it("complete case") { test_agent_output_exposes_only_declared_workflow_outputs_1(); }
}

[[maybe_unused]] static void test_failure_projection_preserves_failure_identity_without_requiring_outputs_2() {
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
    check((executeProjection(workflow, output, error) ==
            PRAKTOR_RESULT_EXECUTION_FAILED));

    const auto result = parseProjection(output);
    check((result.at("workflow_status").as<std::string>() == "failed"));
    check((result.at("error").as<std::string>() ==
          "Workflow execution failed"));

    const auto agent = result.at("agent_output");
    check((agent.at("workflow_status").as<std::string>() == "failed"));
    check((agent.at("error").as<std::string>() ==
          "Workflow execution failed"));
    check((agent.at("failure").is_object()));
    check((agent.at("failure").at("task_name").as<std::string>() == "fail"));
    check((agent.at("failure").at("task_type").as<std::string>() == "program"));
    check((!(agent.at("failure").contains("stdout"))));
    check((!(agent.at("failure").contains("stderr"))));
    check((!(agent.contains("outputs"))));

    praktor_release_json(&output);
    std::filesystem::remove_all(dir);
}

spec("failure projection preserves failure identity without requiring outputs") {
    it("complete case") { test_failure_projection_preserves_failure_identity_without_requiring_outputs_2(); }
}
