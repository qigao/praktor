#ifndef __TASK_PARSER_HPP__
#define __TASK_PARSER_HPP__

#include "dag/enhanced_graph.hpp"
#include "task.hpp"

#include <memory>
#include <string>

/**
 * @namespace TaskParser
 * @brief A collection of functions for parsing and processing Weave workflow files.
 */
namespace TaskParser {
    /**
     * @brief Parses a YAML file and returns a Workflow object.
     * @param filePath The path to the YAML file.
     * @return A Workflow object representing the parsed configuration.
     */
    Workflow parseFile(const std::string& filePath);

    /**
     * @brief Builds a task graph from a Workflow object.
     * @param workflow The Workflow object to build the graph from.
     * @return An EnhancedGraph representing the task dependencies.
     */
    EnhancedGraph<Task> buildGraph(const Workflow& workflow);

    /**
     * @brief Parses a workflow file with imports, recursively loading all dependencies.
     * @param filePath The path to the main YAML file.
     * @param basePath The base directory for resolving relative imports.
     * @return A merged Workflow object containing all imported tasks.
     */
    Workflow parseFileWithImports(const std::string& filePath, const std::string& basePath = "");

} // namespace TaskParser

#endif // __TASK_PARSER_HPP__
