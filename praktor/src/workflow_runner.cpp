#include "workflow_runner.hpp"
#include "util/env_parser.hpp"
#include "fmtlog.h"
#include "util/file_utils.hpp"
#include "yml/task_parser.hpp"
#include "dag/workflow_executor.hpp"

#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unordered_map>

namespace {

std::unordered_map<std::string, std::string> collectWorkflowEnvironment(const Workflow& workflow) {
    std::unordered_map<std::string, std::string> env_values;

    for (const auto& env_path_str : workflow.dot_env) {
        std::filesystem::path env_path(env_path_str);
        if (!std::filesystem::exists(env_path)) {
            logw("dotEnv file not found: {}", env_path.string());
            continue;
        }

        try {
            auto parsed = Praktor::util::parseDotEnvFile(env_path);
            for (auto& [key, value] : parsed) {
                env_values[key] = value;
            }
            logi("Loaded dotEnv file: {}", env_path.string());
        } catch (const std::exception& e) {
            logw("Failed to load dotEnv file '{}': {}", env_path.string(), e.what());
        }
    }

    for (const auto& [key, value] : workflow.env) {
        env_values[key] = value;
    }

    return env_values;
}

bool setProcessEnvironmentVariable(const std::string& key, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(key.c_str(), value.c_str()) == 0;
#else
    return setenv(key.c_str(), value.c_str(), 1) == 0;
#endif
}

void applyProcessEnvironmentOverrides(const std::unordered_map<std::string, std::string>& env_values) {
    for (const auto& [key, value] : env_values) {
        if (!setProcessEnvironmentVariable(key, value)) {
            logw("Failed to set environment variable '{}'", key);
        } else {
            logd("Set environment variable '{}'", key);
        }
    }
}

void applyDefaults(Task& task, const TaskDefaults& defaults) {
    if (!task.retries && defaults.retries) {
        task.retries = defaults.retries;
        logi("Applied default retries to task '{}': count={}, delay={}", task.name,
                defaults.retries->count, defaults.retries->delay);
    }

    if (!task.timeout && defaults.timeout) {
        task.timeout = defaults.timeout;
        logi("Applied default timeout to task '{}': {}", task.name, defaults.timeout.value());
    }
}

} // namespace

WorkflowRunner::WorkflowRunner(std::string const& yamlPath, std::unordered_map<std::string, std::string> inputValues)
    : yamlPath_(yamlPath)
    , inputValues_(std::move(inputValues))
    , base_directory_(std::filesystem::path(yamlPath).parent_path()) {}

WorkflowRunner::~WorkflowRunner() = default;

bool WorkflowRunner::run(bool useConcurrent, int maxConcurrency) {
    try {
        logi("Loading workflow from: {}", yamlPath_);
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        auto env_overrides = collectWorkflowEnvironment(workflow);
        applyProcessEnvironmentOverrides(env_overrides);

        auto workflow_dir = std::filesystem::path(workflow.source_path).parent_path();
        if (workflow_dir.empty()) {
            workflow_dir = base_directory_;
        }

        WorkflowContext context(inputValues_);
        context.setEmbeddedModules(workflow.embedded);

        logi("Initializing workflow context...");
        for (const auto& [key, value] : inputValues_) {
            logi("Set input variable: {} = {}", key, value);
        }
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
            logi("Set variable: {} = {}", key, value);
        }

        for (const auto& [key, value] : env_overrides) {
            context.setValue(key, value);
            logd("Set environment variable override: {} = {}", key, value);
        }

        logi("Building task graph...");
        DependencyGraph<Task> graph = TaskParser::buildGraph(workflow);

        logi("Starting workflow execution...");
        WorkflowExecutor executor(graph, env_overrides, useConcurrent ? maxConcurrency : 1);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        if (status == "failed") {
            loge("Workflow execution failed.");
            return false;
        }

        logi("Workflow finished successfully.");
        return true;

    } catch (const std::exception& e) {
        loge("An error occurred during workflow execution: {}", e.what());
        return false;
    }
}

bool WorkflowRunner::runTask(std::string const& taskName, bool useConcurrent, int maxConcurrency) {
    try {
        logi("Loading workflow to run single task: {}", taskName);
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        auto env_overrides = collectWorkflowEnvironment(workflow);
        applyProcessEnvironmentOverrides(env_overrides);

        auto workflow_dir = std::filesystem::path(workflow.source_path).parent_path();
        if (workflow_dir.empty()) {
            workflow_dir = base_directory_;
        }

        auto targetTaskIt = std::find_if(workflow.tasks.begin(), workflow.tasks.end(),
            [&](const Task& task) { return task.name == taskName; });

        if (targetTaskIt == workflow.tasks.end()) {
            throw std::runtime_error("Target task '" + taskName + "' not found in workflow.");
        }

        DependencyGraph<Task> fullGraph = TaskParser::buildGraph(workflow);
        logi("Creating subgraph for task: {}", taskName);
        DependencyGraph<Task> subgraph = fullGraph.createSubgraphFor(*targetTaskIt);

        WorkflowContext context(inputValues_);
        context.setEmbeddedModules(workflow.embedded);
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
        }
        for (const auto& [key, value] : env_overrides) {
            context.setValue(key, value);
        }

        logi("Executing subgraph for task: {}", taskName);
        WorkflowExecutor executor(subgraph, env_overrides, useConcurrent ? maxConcurrency : 1);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        return status != "failed";

    } catch (const std::exception& e) {
        loge("An error occurred during single task execution: {}", e.what());
        return false;
    }
}
