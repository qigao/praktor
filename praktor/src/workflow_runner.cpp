#include "workflow_runner.hpp"
#include "util/env_parser.hpp"
#include "util/logging.hpp"
#include "util/file_utils.hpp"
#include "util/native_loader.hpp"
#include "util/system_info.hpp"
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

std::filesystem::path resolveRelativePath(const std::string& source_path, const std::string& child) {
    std::filesystem::path relative(child);
    if (relative.is_absolute()) {
        return relative.lexically_normal();
    }

    std::filesystem::path base =
        source_path.empty() ? std::filesystem::current_path() : std::filesystem::path(source_path).parent_path();
    return (base / relative).lexically_normal();
}

std::unordered_map<std::string, std::string> collectWorkflowEnvironment(const Workflow& workflow) {
    std::unordered_map<std::string, std::string> env_values;

    for (const auto& env_path_str : workflow.dot_env) {
        std::filesystem::path env_path = resolveRelativePath(workflow.source_path, env_path_str);
        if (!std::filesystem::exists(env_path)) {
            TLOG_WARN("dotEnv file not found: {}", env_path.string());
            continue;
        }

        try {
            auto parsed = Praktor::util::parseDotEnvFile(env_path);
            for (auto& [key, value] : parsed) {
                env_values[key] = value;
            }
            TLOG_DEBUG("Loaded dotEnv file: {}", env_path.string());
        } catch (const std::exception& e) {
            TLOG_WARN("Failed to load dotEnv file '{}': {}", env_path.string(), e.what());
        }
    }

    for (const auto& [key, value] : workflow.env) {
        env_values[key] = value;
    }

    return env_values;
}

void mergeEnvironmentOverrides(std::unordered_map<std::string, std::string>& target,
                               const std::unordered_map<std::string, std::string>& source) {
    for (const auto& [key, value] : source) {
        if (value.empty()) {
            target.erase(key);
        } else {
            target[key] = value;
        }
    }
}

void setWorkflowVariable(WorkflowContext& context, const std::string& key, const std::string& value) {
    context.setValue(key, value);
    context.setValue("variables." + key, value);
}

void setWorkflowEnvironmentValue(WorkflowContext& context, const std::string& key, const std::string& value) {
    context.setValue("env." + key, value);
}

std::unordered_map<std::string, std::string> buildRuntimeEnvironment(
    const Workflow& workflow,
    const std::unordered_map<std::string, std::string>& base_environment) {
    auto runtime_environment = Praktor::system::getEnvironmentVariables();
    auto workflow_environment = collectWorkflowEnvironment(workflow);
    mergeEnvironmentOverrides(runtime_environment, workflow_environment);
    mergeEnvironmentOverrides(runtime_environment, base_environment);
    return runtime_environment;
}

void populateWorkflowEnvironmentContext(
    WorkflowContext& context,
    const std::unordered_map<std::string, std::string>& runtime_environment) {
    for (const auto& [key, value] : runtime_environment) {
        setWorkflowEnvironmentValue(context, key, value);
    }
}

} // namespace

WorkflowRunner::WorkflowRunner(std::string const& yamlPath,
                               std::unordered_map<std::string, std::string> inputValues,
                               std::unordered_map<std::string, std::string> baseEnvironment)
    : yamlPath_(yamlPath)
    , inputValues_(std::move(inputValues))
    , baseEnvironment_(std::move(baseEnvironment))
    , base_directory_(std::filesystem::path(yamlPath).parent_path()) {}

WorkflowRunner::~WorkflowRunner() = default;

bool WorkflowRunner::run(bool useConcurrent, int maxConcurrency) {
    try {
        TLOG_DEBUG("Loading workflow from: {}", yamlPath_);
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        auto runtime_environment = buildRuntimeEnvironment(workflow, baseEnvironment_);

        auto workflow_dir = std::filesystem::path(workflow.source_path).parent_path();
        if (workflow_dir.empty()) {
            workflow_dir = base_directory_;
        }

        WorkflowContext context(inputValues_);
        context.setEmbeddedModules(workflow.embedded);
        context.setNativeModules(workflow.native_modules);
        context.setSourcePath(workflow.source_path);
        populateWorkflowEnvironmentContext(context, runtime_environment);

        // Pre-load native module DLLs (dlopen + resolve hooks)
        if (!workflow.native_modules.empty()) {
            TLOG_DEBUG("Pre-loading {} native module libraries", workflow.native_modules.size());
            Praktor::Native::NativeLoader::instance().loadModuleLibraries(
                workflow.native_modules, workflow.source_path);
        }

        TLOG_DEBUG("Initializing workflow context...");
        for (const auto& [key, value] : inputValues_) {
            setWorkflowVariable(context, key, value);
            TLOG_DEBUG("Set input variable: {} = {}", key, value);
        }
        for (const auto& [key, value] : workflow.variables) {
            if (inputValues_.find(key) == inputValues_.end()) {
                setWorkflowVariable(context, key, value);
                TLOG_DEBUG("Set variable: {} = {}", key, value);
            } else {
                TLOG_DEBUG("Preserved input override for variable: {}", key);
            }
        }

        for (const auto& [key, value] : runtime_environment) {
            TLOG_DEBUG("Set workflow environment: {} = {}", key, value);
        }

        TLOG_DEBUG("Building task graph...");
        DependencyGraph<Task> graph = TaskParser::buildGraph(workflow);

        TLOG_DEBUG("Starting workflow execution...");
        WorkflowExecutor executor(
            graph,
            workflow.tasks,
            runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            false);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        if (status == "failed") {
            if (!Praktor::Logging::isVerboseEnabled()) {
                Praktor::Logging::printWorkflowStatus("FAILED");
            }
            TLOG_ERROR("Workflow execution failed.");
            return false;
        }

        if (!Praktor::Logging::isVerboseEnabled()) {
            Praktor::Logging::printWorkflowStatus("SUCCESS");
        }
        TLOG_DEBUG("Workflow finished successfully.");
        return true;

    } catch (const std::exception& e) {
        TLOG_ERROR("An error occurred during workflow execution: {}", e.what());
        return false;
    }
}

bool WorkflowRunner::runTask(std::string const& taskName, bool useConcurrent, int maxConcurrency) {
    try {
        TLOG_DEBUG("Loading workflow to run single task: {}", taskName);
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        auto runtime_environment = buildRuntimeEnvironment(workflow, baseEnvironment_);

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
        TLOG_DEBUG("Creating subgraph for task: {}", taskName);
        DependencyGraph<Task> subgraph = fullGraph.createSubgraphFor(*targetTaskIt);

        WorkflowContext context(inputValues_);
        context.setEmbeddedModules(workflow.embedded);
        context.setNativeModules(workflow.native_modules);
        context.setSourcePath(workflow.source_path);
        populateWorkflowEnvironmentContext(context, runtime_environment);

        // Pre-load native module DLLs (dlopen + resolve hooks)
        if (!workflow.native_modules.empty()) {
            TLOG_DEBUG("Pre-loading {} native module libraries", workflow.native_modules.size());
            Praktor::Native::NativeLoader::instance().loadModuleLibraries(
                workflow.native_modules, workflow.source_path);
        }

        for (const auto& [key, value] : workflow.variables) {
            if (inputValues_.find(key) == inputValues_.end()) {
                setWorkflowVariable(context, key, value);
            }
        }

        TLOG_DEBUG("Executing subgraph for task: {}", taskName);
        WorkflowExecutor executor(
            subgraph,
            workflow.tasks,
            runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            true);
        executor.execute(context);

        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        if (!Praktor::Logging::isVerboseEnabled()) {
            Praktor::Logging::printWorkflowStatus(status == "failed" ? "FAILED" : "SUCCESS");
        }
        return status != "failed";

    } catch (const std::exception& e) {
        TLOG_ERROR("An error occurred during single task execution: {}", e.what());
        return false;
    }
}
