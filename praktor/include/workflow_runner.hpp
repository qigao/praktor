#pragma once

#include "dag/dependency_graph.hpp"
#include "dag/workflow_executor.hpp"
#include "yml/task_parser.hpp"

#include <cstddef>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

using WorkflowInputs = std::unordered_map<std::string, WorkflowValue>;

struct WorkflowExecutionResult {
    bool success{false};
    WorkflowValue value{WorkflowValue::object()};
    std::string error_message;
};

class WorkflowRunner {
public:
    explicit WorkflowRunner(std::string const& yamlPath,
                            std::unordered_map<std::string, std::string> inputValues = {},
                            std::unordered_map<std::string, std::string> baseEnvironment = {},
                            size_t maxTriggerDepth = Praktor::Execution::kMaxTriggerChainDepth);
    WorkflowRunner(std::string const& yamlPath,
                   WorkflowInputs inputValues,
                   std::unordered_map<std::string, std::string> baseEnvironment = {},
                   size_t maxTriggerDepth = Praktor::Execution::kMaxTriggerChainDepth);
    ~WorkflowRunner(); // Declared destructor

    WorkflowExecutionResult execute(bool useConcurrent = false, int maxConcurrency = 4);
    bool run(bool useConcurrent = false, int maxConcurrency = 4);
    bool runTask(std::string const& taskName, bool useConcurrent = false, int maxConcurrency = 4);

private:
    std::string yamlPath_;
    WorkflowInputs inputValues_;
    std::unordered_map<std::string, std::string> baseEnvironment_;
    std::filesystem::path base_directory_; // New: store the base directory for the workflow
    size_t max_trigger_depth_ = Praktor::Execution::kMaxTriggerChainDepth;
};

