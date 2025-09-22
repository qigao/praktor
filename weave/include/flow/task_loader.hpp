#ifndef __TASK_LOADER_HPP__
#define __TASK_LOADER_HPP__

// Include necessary headers
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

// This file is a compatibility layer for tests that expect task_loader.hpp
// It defines the TaskTree class directly

class TaskTree {
public:
    /**
     * @brief Default constructor
     */
    TaskTree() = default;

    /**
     * @brief Constructor with parser function
     * @param parse_func Function to parse FlowFile from a file path
     */
    explicit TaskTree(std::function<FlowFile(std::string)> parse_func) : parser_func(parse_func) {}

    /**
     * @brief Build a task tree from a file path using the stored parser function
     * @param filePath Path to the file to parse
     */
    void build(std::string const& filePath) {
        if (!parser_func) { throw std::runtime_error("No parser function provided"); }
        build(filePath, parser_func);
    }

    /**
     * @brief Get access to the task tree
     * @return Reference to the task flows
     */
    TaskFlows const& getTaskTree() const { return task_flow; }

    /**
     * @brief Access a flow file by name
     * @param name Name of the flow file
     * @return Reference to the flow file
     */
    FlowFile& operator[](std::string const& name) { return task_flow[name]; }

    /**
     * @brief build a task tree from a FlowFile,only one task and it's file from a flowfile can be
     * defined with the same name .
     *
     * @param filePath
     * @param parse_func
     */
    void build(std::string const& filePath, std::function<FlowFile(std::string)> parse_func) {
        FlowFile task_file = parse_func(filePath);

        // Ensure the weave name is not empty
        if (task_file.name.empty()) { task_file.name = "default"; }

        // Check for duplicate aliases and file paths
        for (auto const& keyval : task_file.imports) {
            if (aliasSet.contains(keyval.first)) {
                throw std::runtime_error("Duplicate flow file alias: " + keyval.first);
            }
            if (filesSet.contains(keyval.second)) {
                throw std::runtime_error("Duplicate flow file path: " + keyval.second);
            }
            aliasSet.insert(keyval.first);
            filesSet.insert(keyval.second);
        }

        // Check and update tasks within the file
        for (auto const& [taskName, task] : task_file.tasks) {
            // Add the current taskFile tasks in task_flow
            task_flow[task_file.name].tasks[taskName] = task;

            // Check for duplicate task names
            if (taskSet.contains(taskName)) { throw std::runtime_error("Duplicate task name: " + taskName); }
            taskSet.insert(taskName);
        }

        // Check if a key exists or not.
        if (!task_flow.contains(task_file.name)) { task_flow[task_file.name] = task_file; }

        // Recursively load included files
        for (auto const& kv : task_file.imports) { build(kv.second, parse_func); }

        aliasSet.clear();
        filesSet.clear();
        taskSet.clear();
    }

private:
    TaskFlows task_flow;
    std::function<FlowFile(std::string)> parser_func;

    StrSet aliasSet;
    StrSet filesSet;
    StrSet taskSet;
};

#endif   // __TASK_LOADER_HPP__
