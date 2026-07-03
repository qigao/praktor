#pragma once

#include "dag/dependency_graph.hpp"
#include "dag/workflow_executor.hpp"
#include "yml/task_parser.hpp"

#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

class WorkflowRunner {
public:
    explicit WorkflowRunner(std::string const& yamlPath,
                            std::unordered_map<std::string, std::string> inputValues = {},
                            std::unordered_map<std::string, std::string> baseEnvironment = {});
    ~WorkflowRunner(); // Declared destructor

    bool run(bool useConcurrent = false, int maxConcurrency = 4);
    bool runTask(std::string const& taskName, bool useConcurrent = false, int maxConcurrency = 4);

private:
    std::string yamlPath_;
    std::unordered_map<std::string, std::string> inputValues_;  // New: store input values
    std::unordered_map<std::string, std::string> baseEnvironment_;
    std::filesystem::path base_directory_; // New: store the base directory for the workflow
};

