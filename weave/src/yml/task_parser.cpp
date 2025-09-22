#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp" // This is crucial for the parse_workflow() to work


#include "util/file_utils.hpp"
#include "util/logger.hpp" // Added for LOG_DEBUG

#include <ryml/ryml_std.hpp>
#include <ryml/ryml.hpp>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace TaskParser {

    // Helper function to read file content
    std::string readFileToString(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filePath);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    Workflow parseFile(const std::string& filePath) {
        if (!fs::exists(filePath)) {
            throw std::runtime_error("YAML file not found: " + filePath);
        }
        try {
            // Read file content
            std::string content = readFileToString(filePath);

            // Parse with ryml
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            auto root = tree.rootref();

            return parse_workflow(root);
        } catch (const std::exception& e) {
            std::cerr << "YAML parsing error in file: " << filePath << "\n";
            std::cerr << e.what() << "\n";
            throw;
        }
    }

    enum class NodeState { UNVISITED, VISITING, VISITED };

    bool hasCycleDFS(const Task& task,
                     const EnhancedGraph<Task>& graph,
                     std::unordered_map<Task, NodeState>& states) {
        states[task] = NodeState::VISITING;

        for (const auto& neighbor : graph.getEdges(task)) {
            if (states[neighbor] == NodeState::VISITING) {
                return true; // Cycle detected (back edge)
            }
            if (states[neighbor] == NodeState::UNVISITED) {
                if (hasCycleDFS(neighbor, graph, states)) {
                    return true;
                }
            }
        }
        states[task] = NodeState::VISITED;
        return false;
    }

    EnhancedGraph<Task> buildGraph(const Workflow& workflow) {
        EnhancedGraph<Task> graph(workflow.name.empty() ? "Workflow" : workflow.name);

        std::unordered_map<std::string, Task> task_map;
        for (const auto& task : workflow.tasks) {
            graph.addNode(task);
            task_map[task.name] = task;
            // LOG_DEBUG("TaskParser: Added node to graph: " + task.name); // Temporarily removed due to compilation issue
        }

        // Add dependencies as edges
        for (const auto& task : workflow.tasks) {
            // Handle explicit dependencies
            for (const auto& dep_name : task.depends_on) {
                auto it = task_map.find(dep_name);
                if (it != task_map.end()) {
                    graph.addEdge(it->second, task);
                } else {
                    throw std::runtime_error("Task '" + task.name + "' has an unknown dependency: '" + dep_name + "'");
                }
            }

            // Handle implicit dependencies for parallel tasks
            if (task.specifics.index() == 2) { // ParallelParams
                const ParallelParams& parallel_params = std::get<ParallelParams>(task.specifics);
                for (const auto& sub_task : parallel_params.tasks) {
                    auto sub_task_it = task_map.find(sub_task.name);
                    if (sub_task_it != task_map.end()) {
                        // The parallel task depends on all its sub-tasks
                        // So, add an edge from sub_task to task (parent parallel task)
                        graph.addEdge(sub_task_it->second, task);
                    } else {
                        // This case should ideally not happen if parse_parallel_params correctly adds tasks
                        throw std::runtime_error("Parallel task '" + task.name + "' contains unknown sub-task: '" + sub_task.name + "'");
                    }
                }
            }
            // Handle implicit dependencies for group tasks
            if (task.specifics.index() == 5) { // GroupParams
                const GroupParams& group_params = std::get<GroupParams>(task.specifics);
                for (const auto& sub_task : group_params.tasks) {
                    auto sub_task_it = task_map.find(sub_task.name);
                    if (sub_task_it != task_map.end()) {
                        graph.addEdge(sub_task_it->second, task);
                    } else {
                        throw std::runtime_error("Group task '" + task.name + "' contains unknown sub-task: '" + sub_task.name + "'");
                    }
                }
            }
            // Handle implicit dependencies for choose tasks
            if (task.specifics.index() == 6) { // ChooseParams
                const ChooseParams& choose_params = std::get<ChooseParams>(task.specifics);
                for (const auto& branch : choose_params.branches) {
                    for (const auto& sub_task : branch.tasks) {
                        auto sub_task_it = task_map.find(sub_task.name);
                        if (sub_task_it != task_map.end()) {
                            graph.addEdge(sub_task_it->second, task);
                        } else {
                            throw std::runtime_error("Choose task '" + task.name + "' branch contains unknown sub-task: '" + sub_task.name + "'");
                        }
                    }
                }
                // Also handle default tasks if they are full Task objects
                // Currently, default_task_names is std::vector<std::string>
                // If it were std::vector<Task>, similar logic would apply.
            }
        }

        // Perform cycle detection
        std::unordered_map<Task, NodeState> states;
        for (const auto& task : graph.getNodes()) {
            states[task] = NodeState::UNVISITED;
        }

        for (const auto& task : graph.getNodes()) {
            if (states[task] == NodeState::UNVISITED) {
                if (hasCycleDFS(task, graph, states)) {
                    throw std::runtime_error("Cycle detected in workflow graph!");
                }
            }
        }

        return graph;
    }

    Workflow parseFileWithImports(const std::string& filePath, const std::string& basePath) {
        // Determine the base directory for imports
        std::string actualBasePath = basePath;
        if (actualBasePath.empty()) {
            actualBasePath = fs::path(filePath).parent_path().string();
            if (actualBasePath.empty()) {
                actualBasePath = ".";
            }
        }

        // Parse the main workflow file
        Workflow mainWorkflow = parseFile(filePath);

        // If no imports, return the main workflow as-is
        if (mainWorkflow.imports.empty()) {
            return mainWorkflow;
        }

        std::cerr << "Processing imports for workflow: " << filePath << "\n";

        // Recursively load and merge imported workflows
        for (const auto& importPath : mainWorkflow.imports) {
            // Resolve the import path relative to base directory
            fs::path fullImportPath = fs::path(actualBasePath) / importPath;
            std::string resolvedPath = fullImportPath.string();

            std::cerr << "Loading imported workflow: " << resolvedPath << "\n";

            if (!fs::exists(resolvedPath)) {
                throw std::runtime_error("Imported workflow file not found: " + resolvedPath);
            }

            // Recursively parse imported workflow (with its own imports)
            Workflow importedWorkflow = parseFileWithImports(resolvedPath, actualBasePath);

            // Merge imported tasks into main workflow
            // Check for task name conflicts
            for (const auto& importedTask : importedWorkflow.tasks) {
                bool conflict = false;
                for (const auto& mainTask : mainWorkflow.tasks) {
                    if (mainTask.name == importedTask.name) {
                        std::cerr << "Warning: Task name conflict '" << importedTask.name
                                 << "' found in import " << importPath
                                 << ". Main workflow task takes precedence.\n";
                        conflict = true;
                        break;
                    }
                }

                // Add imported task if no conflict
                if (!conflict) {
                    mainWorkflow.tasks.push_back(importedTask);
                }
            }

            // Merge variables (main workflow variables take precedence)
            for (const auto& [key, value] : importedWorkflow.variables) {
                if (mainWorkflow.variables.find(key) == mainWorkflow.variables.end()) {
                    mainWorkflow.variables[key] = value;
                }
            }

            // Merge inputs (no conflict resolution needed - they're additive)
            mainWorkflow.inputs.insert(mainWorkflow.inputs.end(),
                                       importedWorkflow.inputs.begin(),
                                       importedWorkflow.inputs.end());
        }

        std::cerr << "Successfully merged " << mainWorkflow.imports.size()
                 << " imports. Total tasks: " << mainWorkflow.tasks.size() << "\n";

        return mainWorkflow;
    }

    // NOTE: registerTasks and the execution logic will need a major rewrite
    // to handle the new Task structure (variants, each, when, etc.).
    // This function is deprecated - task registration is now handled by TaskProvider
    /*
    void registerTasks(const Workflow& workflow, TaskRegistry<std::string>& registry, const std::string& basePath) {
        for (const auto& task : workflow.tasks) {
            registry.registerTask(task.name, [=](WorkflowContext& context) {
                std::cout << "Executing task '" << task.name << "' (logic not implemented yet).\n";
                // Real implementation will go into a new/refactored WorkflowExecutor
                // It will need to handle:
                // 1. 'when' condition
                // 2. 'each' loop
                // 3. 'retries' policy
                // 4. std::visit on 'specifics' to call the correct command
                // 5. 'on_success' and 'on_failure' hooks
            });
        }
    }
    */

} // namespace TaskParser
