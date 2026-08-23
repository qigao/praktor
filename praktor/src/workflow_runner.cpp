#include "workflow_runner.hpp"
#include "util/env_parser.hpp"
#include "util/logging.hpp"
#include "util/file_utils.hpp"
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
            TLOG_WARNF("dotEnv file not found: {}", env_path.string());
            continue;
        }

        try {
            auto parsed = Praktor::util::parseDotEnvFile(env_path);
            for (auto& [key, value] : parsed) {
                env_values[key] = value;
            }
            TLOG_DEBUGF("Loaded dotEnv file: {}", env_path.string());
        } catch (const std::exception& e) {
            TLOG_WARNF("Failed to load dotEnv file '{}': {}", env_path.string(), e.what());
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

void setWorkflowVariable(WorkflowContext& context, const std::string& key,
                         const WorkflowValue& value) {
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

void populateWorkflowVariables(WorkflowContext& context,
                               const Workflow& workflow,
                               const WorkflowInputs& input_values) {
    for (const auto& [key, value] : input_values) {
        setWorkflowVariable(context, key, value);
        TLOG_DEBUGF("Set input variable: {}", key);
    }

    for (const auto& [key, value] : workflow.variables) {
        if (input_values.find(key) == input_values.end()) {
            setWorkflowVariable(context, key, value);
            TLOG_DEBUGF("Set workflow variable: {}", key);
        } else {
            TLOG_DEBUGF("Preserved input override for variable: {}", key);
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
                                  const WorkflowInputs& input_values,
                                  const std::unordered_map<std::string, std::string>& base_environment) {
    PreparedWorkflow prepared;
    prepared.workflow = TaskParser::parseFileWithIncludes(yaml_path, base_directory.string());
    prepared.runtime_environment = buildRuntimeEnvironment(prepared.workflow, base_environment);
    prepared.graph = TaskParser::buildGraph(prepared.workflow);
    prepared.context = std::make_unique<WorkflowContext>();
    prepared.context->setSourcePath(prepared.workflow.source_path);
    populateWorkflowEnvironmentContext(*prepared.context, prepared.runtime_environment);
    populateWorkflowVariables(*prepared.context, prepared.workflow, input_values);
    return prepared;
}

WorkflowInputs convertStringInputs(
    std::unordered_map<std::string, std::string> input_values) {
    WorkflowInputs result;
    result.reserve(input_values.size());
    for (auto& [key, value] : input_values) {
        result.emplace(std::move(key), WorkflowValue(std::move(value)));
    }
    return result;
}

WorkflowExecutionResult makeExecutionResult(const WorkflowContext& context) {
    WorkflowExecutionResult result;
    result.value["workflow_status"] =
        context.getValueOrDefault<std::string>("workflow_status", "unknown");
    result.value["tasks"] = context.getTasksSnapshot();
    result.success = result.value["workflow_status"].as<std::string>() != "failed";
    if (!result.success) {
        result.error_message = "Workflow execution failed";
        result.value["error"] = result.error_message;
    }
    return result;
}

WorkflowExecutionResult makeFailedExecutionResult(std::string message) {
    WorkflowExecutionResult result;
    result.value["workflow_status"] = "failed";
    result.value["tasks"] = WorkflowValue::object();
    result.value["error"] = message;
    result.error_message = std::move(message);
    return result;
}

} // namespace

WorkflowRunner::WorkflowRunner(std::string const& yamlPath,
                               std::unordered_map<std::string, std::string> inputValues,
                               std::unordered_map<std::string, std::string> baseEnvironment,
                               size_t maxTriggerDepth)
    : WorkflowRunner(yamlPath, convertStringInputs(std::move(inputValues)),
                     std::move(baseEnvironment), maxTriggerDepth) {}

WorkflowRunner::WorkflowRunner(std::string const& yamlPath,
                               WorkflowInputs inputValues,
                               std::unordered_map<std::string, std::string> baseEnvironment,
                               size_t maxTriggerDepth)
    : yamlPath_(yamlPath)
    , inputValues_(std::move(inputValues))
    , baseEnvironment_(std::move(baseEnvironment))
    , base_directory_(std::filesystem::path(yamlPath).parent_path())
    , max_trigger_depth_(maxTriggerDepth) {}

WorkflowRunner::~WorkflowRunner() = default;

WorkflowExecutionResult WorkflowRunner::execute(bool useConcurrent, int maxConcurrency) {
    try {
        TLOG_DEBUGF("Loading workflow from: {}", yamlPath_);
        auto prepared = prepareExecution(yamlPath_, base_directory_, inputValues_, baseEnvironment_);

        TLOG_DEBUGF("Loaded {} workflow environment variables",
                   prepared.runtime_environment.size());

        TLOG_DEBUG("Starting workflow execution...");
        WorkflowExecutor executor(
            prepared.graph,
            prepared.workflow.tasks,
            prepared.runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            false,
            max_trigger_depth_);
        executor.execute(*prepared.context);

        auto result = makeExecutionResult(*prepared.context);
        if (!result.success) {
            if (!Praktor::Logging::isVerboseEnabled()) {
                Praktor::Logging::printWorkflowStatus("FAILED");
            }
            TLOG_ERROR("Workflow execution failed.");
            return result;
        }

        if (!Praktor::Logging::isVerboseEnabled()) {
            Praktor::Logging::printWorkflowStatus("SUCCESS");
        }
        TLOG_DEBUG("Workflow finished successfully.");
        return result;

    } catch (const std::exception& e) {
        TLOG_ERRORF("An error occurred during workflow execution: {}", e.what());
        return makeFailedExecutionResult(e.what());
    }
}

bool WorkflowRunner::run(bool useConcurrent, int maxConcurrency) {
    return execute(useConcurrent, maxConcurrency).success;
}

bool WorkflowRunner::runTask(std::string const& taskName, bool useConcurrent, int maxConcurrency) {
    try {
        TLOG_DEBUGF("Loading workflow to run single task: {}", taskName);
        auto prepared = prepareExecution(yamlPath_, base_directory_, inputValues_, baseEnvironment_);

        auto targetTaskIt = std::find_if(prepared.workflow.tasks.begin(), prepared.workflow.tasks.end(),
            [&](const Task& task) { return task.name == taskName; });

        if (targetTaskIt == prepared.workflow.tasks.end()) {
            throw std::runtime_error("Target task '" + taskName + "' not found in workflow.");
        }

        TLOG_DEBUGF("Creating subgraph for task: {}", taskName);
        DependencyGraph<Task> subgraph = prepared.graph.createSubgraphFor(*targetTaskIt);

        TLOG_DEBUGF("Executing subgraph for task: {}", taskName);
        WorkflowExecutor executor(
            subgraph,
            prepared.workflow.tasks,
            prepared.runtime_environment,
            useConcurrent ? maxConcurrency : 1,
            true,
            max_trigger_depth_);
        executor.execute(*prepared.context);

        std::string status =
            prepared.context->getValueOrDefault<std::string>("workflow_status", "unknown");
        if (!Praktor::Logging::isVerboseEnabled()) {
            Praktor::Logging::printWorkflowStatus(status == "failed" ? "FAILED" : "SUCCESS");
        }
        return status != "failed";

    } catch (const std::exception& e) {
        TLOG_ERRORF("An error occurred during single task execution: {}", e.what());
        return false;
    }
}
