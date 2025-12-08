#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

#include "util/logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace TaskParser {

namespace {

std::string readFileToString(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filePath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

enum class NodeState {
    Unvisited,
    Visiting,
    Visited
};

bool hasCycleDFS(const Task& task,
                 const DependencyGraph<Task>& graph,
                 std::unordered_map<Task, NodeState>& states) {
    states[task] = NodeState::Visiting;

    for (const auto& neighbor : graph.getEdges(task)) {
        auto state = states[neighbor];
        if (state == NodeState::Visiting) {
            return true;
        }
        if (state == NodeState::Unvisited && hasCycleDFS(neighbor, graph, states)) {
            return true;
        }
    }

    states[task] = NodeState::Visited;
    return false;
}

void validateNoCycles(const Workflow& workflow, const DependencyGraph<Task>& graph) {
    std::unordered_map<Task, NodeState> states;
    for (const auto& task : workflow.tasks) {
        states.emplace(task, NodeState::Unvisited);
    }

    for (const auto& task : workflow.tasks) {
        if (states[task] == NodeState::Unvisited) {
            if (hasCycleDFS(task, graph, states)) {
                throw std::runtime_error("Cycle detected in workflow dependencies");
            }
        }
    }
}

} // namespace

Workflow parseFile(const std::string& filePath) {
    if (!fs::exists(filePath)) {
        throw std::runtime_error("YAML file not found: " + filePath);
    }

    try {
        std::string content = readFileToString(filePath);
        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
        auto root = tree.rootref();
        return parse_workflow(root, filePath);
    } catch (const std::exception& e) {
        LOG_ERROR("YAML parsing error in file: " + filePath + " - " + e.what());
        throw;
    }
}

DependencyGraph<Task> buildGraph(const Workflow& workflow) {
    DependencyGraph<Task> graph(workflow.name.empty() ? "Workflow" : workflow.name);

    std::unordered_map<std::string, Task> task_lookup;
    for (const auto& task : workflow.tasks) {
        graph.addNode(task);
        task_lookup[task.name] = task;
    }

    for (const auto& task : workflow.tasks) {
        for (const auto& dependency_name : task.depends_on) {
            auto it = task_lookup.find(dependency_name);
            if (it == task_lookup.end()) {
                throw std::runtime_error("Task '" + task.name + "' depends on unknown task '" + dependency_name + "'");
            }
            graph.addEdge(it->second, task);
        }
    }

    validateNoCycles(workflow, graph);
    return graph;
}

Workflow parseFileWithImports(const std::string& filePath, const std::string& /*basePath*/) {
    Workflow workflow = parseFile(filePath);

    auto workflow_dir = fs::path(filePath).parent_path();
    if (workflow_dir.empty()) {
        workflow_dir = fs::current_path();
    }

    for (auto& env_path : workflow.dot_env) {
        fs::path resolved(env_path);
        if (resolved.is_relative()) {
            resolved = workflow_dir / resolved;
        }
        env_path = resolved.lexically_normal().string();
    }

    return workflow;
}

} // namespace TaskParser
