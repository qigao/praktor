#include "praktor_test.hpp"
#include "yml/task_parser.hpp"
#include "dag/task_executor.hpp"
#include "dag/task_failure_context.hpp"
#include "dag/workflow_executor.hpp"
#include "dag/dependency_graph.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

std::string writeTextCommand(const fs::path& path, const std::string& text)
{
#ifdef _WIN32
    return "echo " + text + " > " + path.string();
#else
    return "printf '" + text + "' > " + path.string();
#endif
}

}

[[maybe_unused]] static void test_taskresult_failure_keeps_legacy_message_and_structured_metadata_1() {
    auto details = WorkflowValue::object();
    details["state"] = "start_pending";
    auto result = TaskResult::fail(
        Praktor::Execution::TaskErrorCode::Timeout,
        "poll", "service did not reach running",
        std::move(details));

    check((!(result.success)));
    check((result.error_message == "service did not reach running"));
    check((result.error_code == "timeout"));
    check((result.error_phase == "poll"));
    check((result.error_details["state"].as<std::string>() == "start_pending"));
}

spec("TaskResult failure keeps legacy message and structured metadata") {
    it("complete case") { test_taskresult_failure_keeps_legacy_message_and_structured_metadata_1(); }
}

[[maybe_unused]] static void test_task_error_code_rejects_unknown_enum_values_2() {
    const auto unknown = static_cast<Praktor::Execution::TaskErrorCode>(999);

    check_throws_as(Praktor::Execution::taskErrorCodeName(unknown), std::invalid_argument);
    check_throws_as(TaskResult::fail(unknown, "poll", "invalid task error code"),
                    std::invalid_argument);
}

spec("Task error code rejects unknown enum values") {
    it("complete case") { test_task_error_code_rejects_unknown_enum_values_2(); }
}

[[maybe_unused]] static void test_taskfailurecontext_keeps_legacy_failure_data_with_structured_metadata_3() {
    TaskFailureContext failure;
    failure.task_name = "start_service";
    failure.task_type = "service";
    failure.error_message = "service did not reach running";
    failure.error_code = "timeout";
    failure.error_phase = "poll";
    failure.error_details = WorkflowValue::object();
    failure.error_details["state"] = "start_pending";

    const auto variables = failure.toFailureVariables();
    const auto serialized = failure.toJson();

    check((variables.at("failed_task_error").as<std::string>() == "service did not reach running"));
    check((variables.at("failed_task_error_code").as<std::string>() == "timeout"));
    check((variables.at("failed_task_error_phase").as<std::string>() == "poll"));
    check((variables.at("failed_task_error_details")["state"].as<std::string>() == "start_pending"));
    check((serialized["error"].as<std::string>() == "service did not reach running"));
    check((serialized["error_code"].as<std::string>() == "timeout"));
    check((serialized["error_phase"].as<std::string>() == "poll"));
    check((serialized["error_details"]["state"].as<std::string>() == "start_pending"));
}

spec("TaskFailureContext keeps legacy failure data with structured metadata") {
    it("complete case") { test_taskfailurecontext_keeps_legacy_failure_data_with_structured_metadata_3(); }
}

[[maybe_unused]] static void test_parser_improvements_line_numbers_validation_4(int section_index) {
    fs::path temp_file = "temp_invalid.yml";

    if (section_index == 0) {
        std::string invalid_yml = "name: Test\ntasks:\n  - name: my_task\n    typO: command\n";
        {
            std::ofstream ofs(temp_file);
            ofs << invalid_yml;
        }

        try {
            TaskParser::parseFile(temp_file.string());
            check(false, "%s", praktor_test::message([&](std::ostream& out) { out << "Should have thrown for unknown key 'typO'"; }).c_str());
        } catch (const std::exception& e) {
            std::string msg = e.what();
            // Expected something like "temp_invalid.yml: Parse error: Unknown key: 'typO'"
            check((msg.find("temp_invalid.yml") != std::string::npos));
            check((msg.find("Unknown key: 'typO'") != std::string::npos));
        }
    }

    if (fs::exists(temp_file)) fs::remove(temp_file);
}

spec("Parser Improvements - Line Numbers & Validation") {
    it("Unknown keys throw with line info") { test_parser_improvements_line_numbers_validation_4(0); }
}

[[maybe_unused]] static void test_include_system_circular_detection_5(int section_index) {
    fs::path a = fs::absolute("circ_a.yml");
    fs::path b = fs::absolute("circ_b.yml");

    {
        std::ofstream(a) << "includes:\n  b: ./circ_b.yml\ntasks:\n  - name: t1\n    command: echo A\n";
        std::ofstream(b) << "includes:\n  a: ./circ_a.yml\ntasks:\n  - name: t2\n    command: echo B\n";
    }

    if (section_index == 0) {
        try {
            TaskParser::parseFile(a.string());
            check(false, "%s", praktor_test::message([&](std::ostream& out) { out << "Should have thrown for circular include"; }).c_str());
        } catch (const std::exception& e) {
            std::string msg = e.what();
            check((msg.find("Circular include detected") != std::string::npos));
            check((msg.find("circ_a.yml") != std::string::npos));
            check((msg.find("circ_b.yml") != std::string::npos));
        }
    }

    if (fs::exists(a)) fs::remove(a);
    if (fs::exists(b)) fs::remove(b);
}

spec("Include System - Circular Detection") {
    it("Circular include detected") { test_include_system_circular_detection_5(0); }
}

[[maybe_unused]] static void test_execution_engine_task_caching_6(int section_index) {
    fs::path src = fs::absolute("test_input.txt");
    fs::path gen = fs::absolute("test_output.txt");
    fs::path wf_path = fs::absolute("cache_wf.yml");
    fs::path cache_file = fs::absolute(".praktor_cache");

    if (fs::exists(gen)) fs::remove(gen);
    if (fs::exists(cache_file)) fs::remove(cache_file);

    {
        std::ofstream(src) << "initial content";
        std::ofstream(wf_path) << "tasks:\n  - name: cache_task\n    command: echo 'working' > " << gen.string() << "\n    sources: [" << src.string() << "]\n    generates: [" << gen.string() << "]\n";
    }

    Workflow wf = TaskParser::parseFile(wf_path.string());
    DependencyGraph<Task> graph = TaskParser::buildGraph(wf);
    WorkflowContext context;
    WorkflowExecutor executor(graph);

    if (section_index == 0) {
        executor.execute(context);
        check((fs::exists(gen)));
        check((context.getTaskStatus("cache_task") == "success"));
        // Verify cache was created (usually in the workflow's parent dir)
        fs::path expected_cache = wf_path.parent_path() / ".praktor_cache";
        check((fs::exists(expected_cache)));
    }

    if (section_index == 1) {
        // Execute first to ensure it's cached
        executor.execute(context);

        // Execute again
        WorkflowContext context2;
        executor.execute(context2);
        check((context2.getTaskStatus("cache_task") == "skipped"));
    }

    if (section_index == 2) {
        executor.execute(context); // cache it

        // Change source
        {
            std::ofstream ofs(src);
            ofs << "updated content";
        }

        WorkflowContext context3;
        executor.execute(context3);
        check((context3.getTaskStatus("cache_task") == "success"));
    }

    if (section_index == 3) {
        executor.execute(context); // cache the first command

        {
            std::ofstream(wf_path)
                << "tasks:\n"
                << "  - name: cache_task\n"
                << "    command: " << writeTextCommand(gen, "changed") << "\n"
                << "    sources: [" << src.string() << "]\n"
                << "    generates: [" << gen.string() << "]\n";
        }

        Workflow updated_wf = TaskParser::parseFile(wf_path.string());
        DependencyGraph<Task> updated_graph = TaskParser::buildGraph(updated_wf);
        WorkflowContext context4;
        WorkflowExecutor updated_executor(updated_graph);

        updated_executor.execute(context4);

        check((context4.getTaskStatus("cache_task") == "success"));
        std::ifstream in(gen);
        std::string output;
        std::getline(in, output);
        check((output.find("changed") != std::string::npos));
    }

    if (fs::exists(src)) fs::remove(src);
    if (fs::exists(gen)) fs::remove(gen);
    if (fs::exists(wf_path)) fs::remove(wf_path);
    if (fs::exists(wf_path.parent_path() / ".praktor_cache")) fs::remove(wf_path.parent_path() / ".praktor_cache");
}

spec("Execution Engine - Task Caching") {
    it("First run: execute and cache") { test_execution_engine_task_caching_6(0); }
    it("Second run: skip") { test_execution_engine_task_caching_6(1); }
    it("Third run: source changed, re-execute") { test_execution_engine_task_caching_6(2); }
    it("Action changed: re-execute") { test_execution_engine_task_caching_6(3); }
}

[[maybe_unused]] static void test_execution_engine_each_outputs_are_aggregated_7() {
    fs::path wf_path = fs::absolute("each_outputs_wf.yml");

    {
        std::ofstream(wf_path) <<
            "tasks:\n"
            "  - name: fanout\n"
            "    each:\n"
            "      items: [alpha, beta, gamma]\n"
            "      as: item\n"
            "    command: echo {{ item }}\n";
    }

    Workflow wf = TaskParser::parseFile(wf_path.string());
    DependencyGraph<Task> graph = TaskParser::buildGraph(wf);
    WorkflowContext context;
    WorkflowExecutor executor(graph, wf.tasks, {}, 1, false);

    executor.execute(context);

    auto outputs = context.getValueByPath("tasks.fanout.outputs");
    check((outputs.is_object()));
    check((outputs.contains("stdout")));
    check((outputs["stdout"].as<std::string>().find("gamma") != std::string::npos));
    check((outputs.contains("iterations")));
    check((outputs["iterations"].is_array()));
    check((outputs["iterations"].size() == 3));

    auto first = outputs["iterations"][0];
    auto second = outputs["iterations"][1];
    auto third = outputs["iterations"][2];

    check((first["status"].as<std::string>() == "success"));
    check((first["outputs"]["stdout"].as<std::string>().find("alpha") != std::string::npos));
    check((second["outputs"]["stdout"].as<std::string>().find("beta") != std::string::npos));
    check((third["outputs"]["stdout"].as<std::string>().find("gamma") != std::string::npos));

    if (fs::exists(wf_path)) fs::remove(wf_path);
}

spec("Execution Engine - Each outputs are aggregated") {
    it("complete case") { test_execution_engine_each_outputs_are_aggregated_7(); }
}
