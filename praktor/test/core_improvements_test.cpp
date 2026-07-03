#include <catch2/catch_all.hpp>
#include "yml/task_parser.hpp"
#include "dag/workflow_executor.hpp"
#include "dag/dependency_graph.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

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

TEST_CASE("Parser Improvements - Line Numbers & Validation", "[parser][schema]") {
    fs::path temp_file = "temp_invalid.yml";

    SECTION("Unknown keys throw with line info") {
        std::string invalid_yml = "version: '1.0'\nname: Test\ntasks:\n  - name: my_task\n    typO: command\n";
        {
            std::ofstream ofs(temp_file);
            ofs << invalid_yml;
        }

        try {
            TaskParser::parseFile(temp_file.string());
            FAIL("Should have thrown for unknown key 'typO'");
        } catch (const std::exception& e) {
            std::string msg = e.what();
            // Expected something like "temp_invalid.yml: Parse error: Unknown key: 'typO'"
            REQUIRE(msg.find("temp_invalid.yml") != std::string::npos);
            REQUIRE(msg.find("Unknown key: 'typO'") != std::string::npos);
        }
    }

    if (fs::exists(temp_file)) fs::remove(temp_file);
}

TEST_CASE("Import System - Circular Detection", "[parser][imports]") {
    fs::path a = fs::absolute("circ_a.yml");
    fs::path b = fs::absolute("circ_b.yml");

    {
        std::ofstream(a) << "includes:\n  b: ./circ_b.yml\ntasks:\n  - name: t1\n    command: echo A\n";
        std::ofstream(b) << "includes:\n  a: ./circ_a.yml\ntasks:\n  - name: t2\n    command: echo B\n";
    }

    SECTION("Circular import detected") {
        try {
            TaskParser::parseFile(a.string());
            FAIL("Should have thrown for circular import");
        } catch (const std::exception& e) {
            std::string msg = e.what();
            REQUIRE(msg.find("Circular import detected") != std::string::npos);
            REQUIRE(msg.find("circ_a.yml") != std::string::npos);
            REQUIRE(msg.find("circ_b.yml") != std::string::npos);
        }
    }

    if (fs::exists(a)) fs::remove(a);
    if (fs::exists(b)) fs::remove(b);
}

TEST_CASE("Execution Engine - Task Caching", "[execution][caching]") {
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

    SECTION("First run: execute and cache") {
        executor.execute(context);
        REQUIRE(fs::exists(gen));
        REQUIRE(context.getTaskStatus("cache_task") == "success");
        // Verify cache was created (usually in the workflow's parent dir)
        fs::path expected_cache = wf_path.parent_path() / ".praktor_cache";
        REQUIRE(fs::exists(expected_cache));
    }

    SECTION("Second run: skip") {
        // Execute first to ensure it's cached
        executor.execute(context);

        // Execute again
        WorkflowContext context2;
        executor.execute(context2);
        REQUIRE(context2.getTaskStatus("cache_task") == "skipped");
    }

    SECTION("Third run: source changed, re-execute") {
        executor.execute(context); // cache it

        // Change source
        {
            std::ofstream ofs(src);
            ofs << "updated content";
        }

        WorkflowContext context3;
        executor.execute(context3);
        REQUIRE(context3.getTaskStatus("cache_task") == "success");
    }

    SECTION("Action changed: re-execute") {
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

        REQUIRE(context4.getTaskStatus("cache_task") == "success");
        std::ifstream in(gen);
        std::string output;
        std::getline(in, output);
        CHECK(output.find("changed") != std::string::npos);
    }

    if (fs::exists(src)) fs::remove(src);
    if (fs::exists(gen)) fs::remove(gen);
    if (fs::exists(wf_path)) fs::remove(wf_path);
    if (fs::exists(wf_path.parent_path() / ".praktor_cache")) fs::remove(wf_path.parent_path() / ".praktor_cache");
}

TEST_CASE("Execution Engine - Each outputs are aggregated", "[execution][each]") {
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
    REQUIRE(outputs.is_object());
    REQUIRE(outputs.contains("stdout"));
    CHECK(outputs["stdout"].as<std::string>().find("gamma") != std::string::npos);
    REQUIRE(outputs.contains("iterations"));
    REQUIRE(outputs["iterations"].is_array());
    REQUIRE(outputs["iterations"].size() == 3);

    auto first = outputs["iterations"][0];
    auto second = outputs["iterations"][1];
    auto third = outputs["iterations"][2];

    CHECK(first["status"].as<std::string>() == "success");
    CHECK(first["outputs"]["stdout"].as<std::string>().find("alpha") != std::string::npos);
    CHECK(second["outputs"]["stdout"].as<std::string>().find("beta") != std::string::npos);
    CHECK(third["outputs"]["stdout"].as<std::string>().find("gamma") != std::string::npos);

    if (fs::exists(wf_path)) fs::remove(wf_path);
}
