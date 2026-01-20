#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

#include "util/logging.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace TaskParser {

namespace {
struct ParseContext {
    std::vector<std::string> import_stack;
    std::unordered_map<std::string, Workflow> cache;

    bool is_circular(const std::string& path) const {
        return std::find(import_stack.begin(), import_stack.end(), path) != import_stack.end();
    }
};

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

Workflow parseInternal(const std::string& filePath, ParseContext& ctx) {
    std::string absolutePath = fs::absolute(filePath).lexically_normal().string();

    if (ctx.cache.count(absolutePath)) {
        return ctx.cache[absolutePath];
    }

    if (ctx.is_circular(absolutePath)) {
        std::string stack_str;
        for (const auto& s : ctx.import_stack) {
            stack_str += s + " -> ";
        }
        throw std::runtime_error("Circular import detected: " + stack_str + absolutePath);
    }

    ctx.import_stack.push_back(absolutePath);

    if (!fs::exists(absolutePath)) {
        throw std::runtime_error("YAML file not found: " + absolutePath);
    }

    try {
        std::string content = readFileToString(absolutePath);
        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
        auto root = tree.rootref();
        Workflow workflow = parse_workflow(root, absolutePath);
        workflow.source_path = absolutePath;

        if (root.has_child("includes")) {
            const auto& includes_node = root["includes"];
            if (includes_node.is_map()) {
                for (const auto& child : includes_node) {
                    std::string include_path_str;
                    child >> include_path_str;

                    fs::path include_path = fs::path(absolutePath).parent_path() / include_path_str;
                    Workflow included = parseInternal(include_path.string(), ctx);

                    // Merge tasks
                    for (auto& t : included.tasks) {
                        workflow.tasks.push_back(std::move(t));
                    }
                    // Merge variables (included files act as defaults)
                    for (const auto& [key, val] : included.variables) {
                        if (workflow.variables.find(key) == workflow.variables.end()) {
                            workflow.variables[key] = val;
                        }
                    }
                    // Merge environment
                    for (const auto& [key, val] : included.env) {
                        if (workflow.env.find(key) == workflow.env.end()) {
                            workflow.env[key] = val;
                        }
                    }
                }
            }
        }

        ctx.cache[absolutePath] = workflow;
        ctx.import_stack.pop_back();
        return workflow;
    } catch (const std::exception& e) {
        std::string error_msg = absolutePath + ": " + e.what();
        TLOG_ERROR("YAML parsing error: {}", error_msg);
        ctx.import_stack.pop_back();
        throw std::runtime_error(error_msg);
    }
}

} // namespace

Workflow parseFile(const std::string& filePath) {
    ParseContext ctx;
    return parseInternal(filePath, ctx);
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
    ParseContext ctx;
    return parseInternal(filePath, ctx);
}

} // namespace TaskParser

