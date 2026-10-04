#pragma once

#include "dag/dependency_graph.hpp"
#include "task.hpp"

#include <memory>
#include <string>

/**
 * @namespace TaskParser
 * @brief A collection of functions for parsing and processing Praktor workflow files.
 */
namespace TaskParser {
    /**
     * @brief Parses a YAML file and returns a Workflow object.
     * @param filePath The path to the YAML file.
     * @return A Workflow object representing the parsed configuration.
     */
    Workflow parseFile(const std::string& filePath);

    /**
     * @brief Parses one in-memory YAML workflow without filesystem includes.
     * @param source UTF-8 YAML source bytes.
     * @param sourceId Logical source identity used in diagnostics/task metadata.
     * @return A normalized Workflow object.
     *
     * Inline parsing deliberately rejects top-level includes. Higher layers may
     * further restrict task forms before admitting an executable plan.
     */
    Workflow parseText(const std::string& source, const std::string& sourceId);

    /**
     * @brief Builds a task graph from a Workflow object.
     * @param workflow The Workflow object to build the graph from.
     * @return A DependencyGraph representing the task dependencies.
     */
    DependencyGraph<Task> buildGraph(const Workflow& workflow);

    /**
     * @brief Parses a workflow file with includes, recursively loading all dependencies.
     * @param filePath The path to the main YAML file.
     * @param basePath The base directory for resolving relative includes.
     * @return A merged Workflow object containing all included tasks.
     */
    Workflow parseFileWithIncludes(const std::string& filePath, const std::string& basePath = "");

} // namespace TaskParser

