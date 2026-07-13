#include "workflow_runner.hpp"
#include "util/env_parser.hpp"
#include "util/logging.hpp"
#include "util/file_utils.hpp"
#include "util/native_loader.hpp"
#include "util/path_utils.hpp"
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

std::unordered_map<std::string, std::string> collectWorkflowEnvironment(const Workflow& workflow) {
    std::unordered_map<std::string, std::string> env_values;

    for (const auto& env_path_str : workflow.dot_env) {
        std::filesystem::path env_path = Praktor::util::resolveRelativePath(workflow.source_path, env_path_str);
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

void preloadNativeModules(const Workflow& workflow) {
    if (workflow.native_modules.empty()) {
        return;
    }

    TLOG_DEBUG("Pre-loading {} native module libraries", workflow.native_modules.size());
    Praktor::Native::NativeLoader::instance().loadModuleLibraries(
        workflow.native_modules, workflow.source_path);
}

void populateWorkflowVariables(WorkflowContext& context,
                               const Workflow& workflow,
                               const std::unordered_map<std::string, std::string>& input_values) {
    for (const auto& [key, value] : input_values) {
        setWorkflowVariable(context, key, value);
        TLOG_DEBUG("Set input variable: {}", key);
    }

    for (const auto& [key, value] : workflow.variables) {
        if (input_values.find(key) == input_values.end()) {
            setWorkflowVariable(context, key, value);
            TLOG_DEBUG("Set workflow variable: {}", key);
        } else {
            TLOG_DEBUG("Preserved input override for variable: {}", key);
        }
    }
}

struct PreparedWorkflow {
    Workflow workflow;
    DependencyGraph<Task> graph{"prepared_workflow"};
    std::unordered_map<std::string, std::string> runtime_environment;
    std::unique_ptr<WorkflowContext> context;
};

PreparedWorkflow prepareExecution(const std::string& yaml_path,
                                  const std::filesystem::path& base_directory,
                                  const std::unordered_map<std::string, std::string>& input_values,
                                  const std::unordered_map<std::string, std::string>& base_environment) {
    PreparedWorkflow prepared;
    prepared.workflow = TaskParser::parseFileWithImports(yaml_path, base_directory.string());
    prepared.runtime_environment = buildRuntimeEnvironment(prepared.workflow, base_environment);
    prepared.graph = TaskParser::buildGraph(prepared.workflow);
    prepared.context = std::make_unique<WorkflowContext>(input_values);
    prepared.context->setEmbeddedModules(prepared.workflow.embedded);
    prepared.context->setNativeModules(prepared.workflow.native_modules);
    prepared.context->setSourcePath(prepared.workflow.source_path);
    populateWorkflowEnvironmentContext(*prepared.context, prepared.runtime_environment);
    preloadNativeModules(prepared.workflow);
    populateWorkflowVariables(*prepared.context, prepared.workflow, input_values);
    return prepared;
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
        auto prepared = prepareExecution(yamlPath_, base_directory_, inputValues_, baseEnvironment_);

        TLOG_DEBUG("Loaded {} workflow environment variables",
                   prepared.runtime_environment.size());

        TLOG_DEBUG("Starting workflow execution...");
        WorkflowExecutor executor(
            prepared.graph,
            prepared.workflow.tasks,
            prepared.runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            false);
        executor.execute(*prepared.context);

        std::string status =
            prepared.context->getValueOrDefault<std::string>("workflow_status", "unknown");
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
        auto prepared = prepareExecution(yamlPath_, base_directory_, inputValues_, baseEnvironment_);

        auto targetTaskIt = std::find_if(prepared.workflow.tasks.begin(), prepared.workflow.tasks.end(),
            [&](const Task& task) { return task.name == taskName; });

        if (targetTaskIt == prepared.workflow.tasks.end()) {
            throw std::runtime_error("Target task '" + taskName + "' not found in workflow.");
        }

        TLOG_DEBUG("Creating subgraph for task: {}", taskName);
        DependencyGraph<Task> subgraph = prepared.graph.createSubgraphFor(*targetTaskIt);

        TLOG_DEBUG("Executing subgraph for task: {}", taskName);
        WorkflowExecutor executor(
            subgraph,
            prepared.workflow.tasks,
            prepared.runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            true);
        executor.execute(*prepared.context);

        std::string status =
            prepared.context->getValueOrDefault<std::string>("workflow_status", "unknown");
        if (!Praktor::Logging::isVerboseEnabled()) {
            Praktor::Logging::printWorkflowStatus(status == "failed" ? "FAILED" : "SUCCESS");
        }
        return status != "failed";

    } catch (const std::exception& e) {
        TLOG_ERROR("An error occurred during single task execution: {}", e.what());
        return false;
    }
}
