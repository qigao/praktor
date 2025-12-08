#include "workflow_runner.hpp"
#include "util/logger.hpp"
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

std::string trimWhitespace(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::unordered_map<std::string, std::string> parseDotEnvFile(const std::filesystem::path& file_path) {
    std::unordered_map<std::string, std::string> result;
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open .env file: " + file_path.string());
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trimWhitespace(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.rfind("export ", 0) == 0) {
            trimmed = trimWhitespace(trimmed.substr(7));
        }

        size_t equals_pos = trimmed.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        std::string key = trimWhitespace(trimmed.substr(0, equals_pos));
        std::string value = trimWhitespace(trimmed.substr(equals_pos + 1));

        if (!value.empty()) {
            bool quoted = value.size() >= 2
                && ((value.front() == '"' && value.back() == '"')
                    || (value.front() == '\'' && value.back() == '\''));
            if (quoted) {
                value = value.substr(1, value.size() - 2);
            }
        }

        if (!key.empty()) {
            result[key] = value;
        }
    }

    return result;
}

std::unordered_map<std::string, std::string> collectWorkflowEnvironment(const Workflow& workflow) {
    std::unordered_map<std::string, std::string> env_values;

    for (const auto& env_path_str : workflow.dot_env) {
        std::filesystem::path env_path(env_path_str);
        if (!std::filesystem::exists(env_path)) {
            LOG_WARNING("dotEnv file not found: " + env_path.string());
            continue;
        }

        try {
            auto parsed = parseDotEnvFile(env_path);
            for (auto& [key, value] : parsed) {
                env_values[key] = value;
            }
            LOG_INFO("Loaded dotEnv file: " + env_path.string());
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to load dotEnv file '" + env_path.string() + "': " + e.what());
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
            LOG_WARNING("Failed to set environment variable '" + key + "'");
        } else {
            LOG_DEBUG("Set environment variable '" + key + "'");
        }
    }
}

void applyDefaults(Task& task, const TaskDefaults& defaults) {
    if (!task.retries && defaults.retries) {
        task.retries = defaults.retries;
        LOG_INFO("Applied default retries to task '" + task.name + "': count=" +
                std::to_string(defaults.retries->count) + ", delay=" + defaults.retries->delay);
    }

    if (!task.timeout && defaults.timeout) {
        task.timeout = defaults.timeout;
        LOG_INFO("Applied default timeout to task '" + task.name + "': " + defaults.timeout.value());
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
        LOG_INFO("Loading workflow from: " + yamlPath_);
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        auto env_overrides = collectWorkflowEnvironment(workflow);
        applyProcessEnvironmentOverrides(env_overrides);

        auto workflow_dir = std::filesystem::path(workflow.source_path).parent_path();
        if (workflow_dir.empty()) {
            workflow_dir = base_directory_;
        }

        WorkflowContext context;
        context.setEmbeddedModules(workflow.embedded);

        LOG_INFO("Initializing workflow context...");
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
            LOG_INFO("Set variable: " + key + " = " + value);
        }

        for (const auto& [key, value] : env_overrides) {
            context.setValue(key, value);
            LOG_DEBUG("Set environment variable override: " + key + " = " + value);
        }

        LOG_INFO("Building task graph...");
        DependencyGraph<Task> graph = TaskParser::buildGraph(workflow);

        LOG_INFO("Starting workflow execution...");
        WorkflowExecutor executor(graph, env_overrides);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        if (status == "failed") {
            LOG_ERROR("Workflow execution failed.");
            return false;
        }

        LOG_INFO("Workflow finished successfully.");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("An error occurred during workflow execution: " + std::string(e.what()));
        return false;
    }
}

bool WorkflowRunner::runTask(std::string const& taskName, bool useConcurrent, int maxConcurrency) {
    try {
        LOG_INFO("Loading workflow to run single task: " + taskName);
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
        LOG_INFO("Creating subgraph for task: " + taskName);
        DependencyGraph<Task> subgraph = fullGraph.createSubgraphFor(*targetTaskIt);

        WorkflowContext context;
        context.setEmbeddedModules(workflow.embedded);
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
        }
        for (const auto& [key, value] : env_overrides) {
            context.setValue(key, value);
        }

        LOG_INFO("Executing subgraph for task: " + taskName);
        WorkflowExecutor executor(subgraph, env_overrides);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        return status != "failed";

    } catch (const std::exception& e) {
        LOG_ERROR("An error occurred during single task execution: " + std::string(e.what()));
        return false;
    }
}
