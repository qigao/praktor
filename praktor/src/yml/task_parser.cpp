#include "yml/task_parser.hpp"
#include "task_yaml_internal.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace TaskParser {

namespace {
struct ParseContext {
    std::vector<std::string> include_stack;
    std::unordered_map<std::string, Workflow> cache;

    bool is_circular(const std::string& path) const {
        return std::find(include_stack.begin(), include_stack.end(), path) != include_stack.end();
    }

    void push_include(const std::string& path) {
        include_stack.push_back(path);
    }

    void pop_include() {
        if (!include_stack.empty()) {
            include_stack.pop_back();
        }
    }
};

class IncludeScope {
public:
    IncludeScope(ParseContext& context, const std::string& path)
        : context_(context) {
        context_.push_include(path);
    }

    ~IncludeScope() {
        context_.pop_include();
    }

    IncludeScope(const IncludeScope&) = delete;
    IncludeScope& operator=(const IncludeScope&) = delete;

private:
    ParseContext& context_;
};

void applyTaskDefaults(Task& task, const TaskDefaults& defaults) {
    if (!task.timeout && defaults.timeout) {
        task.timeout = defaults.timeout;
    }
}

void normalizeWorkflow(Workflow& workflow) {
    for (auto& task : workflow.tasks) {
        applyTaskDefaults(task, workflow.defaults);
    }
}

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
                 std::unordered_map<std::string, NodeState>& states) {
    states[task.name] = NodeState::Visiting;

    for (const auto& neighbor : graph.getEdges(task)) {
        auto state = states[neighbor.name];
        if (state == NodeState::Visiting) {
            return true;
        }
        if (state == NodeState::Unvisited && hasCycleDFS(neighbor, graph, states)) {
            return true;
        }
    }

    states[task.name] = NodeState::Visited;
    return false;
}

void validateNoCycles(const Workflow& workflow, const DependencyGraph<Task>& graph) {
    std::unordered_map<std::string, NodeState> states;
    for (const auto& task : workflow.tasks) {
        states.emplace(task.name, NodeState::Unvisited);
    }

    for (const auto& task : workflow.tasks) {
        if (states[task.name] == NodeState::Unvisited) {
            if (hasCycleDFS(task, graph, states)) {
                throw std::runtime_error("Cycle detected in workflow dependencies");
            }
        }
    }
}

void validateUniqueTaskNames(const Workflow& workflow) {
    std::unordered_set<std::string> names;
    for (const auto& task : workflow.tasks) {
        if (task.name.empty()) {
            continue;
        }
        if (!names.insert(task.name).second) {
            throw std::runtime_error("Duplicate task name: '" + task.name + "'");
        }
    }
}

bool isDynamicTriggerReference(const std::string& action) {
    return action.find("{{") != std::string::npos || action.find("}}") != std::string::npos;
}

void validateTriggerReferences(const Workflow& workflow,
                               const std::unordered_map<std::string, Task>& task_lookup) {
    for (const auto& task : workflow.tasks) {
        if (!task.triggers) {
            continue;
        }

        auto validate_actions = [&](const std::vector<TriggerAction>& actions,
                                    const std::string& event_name) {
            for (const auto& action : actions) {
                if (action.empty() || isDynamicTriggerReference(action)) {
                    continue;
                }
                if (task_lookup.find(action) == task_lookup.end()) {
                    throw std::runtime_error(
                        "Task '" + task.name + "' trigger '" + event_name +
                        "' references unknown task '" + action + "'");
                }
            }
        };

        validate_actions(task.triggers->on_success, "on_success");
        validate_actions(task.triggers->on_failure, "on_failure");
        validate_actions(task.triggers->on_complete, "on_complete");
    }
}

Workflow parseInternal(const std::string& filePath, ParseContext& ctx) {
    std::string absolutePath = fs::absolute(filePath).lexically_normal().string();

    if (ctx.cache.count(absolutePath)) {
        return ctx.cache[absolutePath];
    }

    if (ctx.is_circular(absolutePath)) {
        std::string stack_str;
        for (const auto& s : ctx.include_stack) {
            stack_str += s + " -> ";
        }
        throw std::runtime_error("Circular include detected: " + stack_str + absolutePath);
    }

    IncludeScope include_scope(ctx, absolutePath);

    if (!fs::exists(absolutePath)) {
        throw std::runtime_error("YAML file not found: " + absolutePath);
    }

    try {
        std::string content = readFileToString(absolutePath);
        TaskYamlDetail::YamlDocument document(content);
        auto root = document.root();
        Workflow workflow = parse_workflow(root, absolutePath);
        workflow.source_path = absolutePath;

        if (root.has_child("includes")) {
            const auto& includes_node = root["includes"];
            if (!includes_node.is_map()) {
                TaskYamlDetail::throw_parse_error(includes_node, "'includes' must be a map");
            }
            for (const auto& child : includes_node) {
                std::string include_path_str = TaskYamlDetail::read_scalar_or_throw(
                    child, "include path must be a scalar");
                if (include_path_str.empty()) {
                    TaskYamlDetail::throw_parse_error(child, "include path cannot be empty");
                }

                fs::path include_path = fs::path(absolutePath).parent_path() / include_path_str;
                Workflow included = parseInternal(include_path.string(), ctx);

                for (auto& t : included.tasks) {
                    workflow.tasks.push_back(std::move(t));
                }
                // Included values act as defaults for the including workflow.
                for (const auto& [key, val] : included.variables) {
                    if (workflow.variables.find(key) == workflow.variables.end()) {
                        workflow.variables[key] = val;
                    }
                }
                for (const auto& [key, val] : included.env) {
                    if (workflow.env.find(key) == workflow.env.end()) {
                        workflow.env[key] = val;
                    }
                }
            }
        }

        normalizeWorkflow(workflow);

        ctx.cache[absolutePath] = workflow;
        return workflow;
    } catch (const std::exception& e) {
        std::string error_msg = absolutePath + ": " + e.what();
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

    validateUniqueTaskNames(workflow);

    std::unordered_map<std::string, Task> task_lookup;
    for (const auto& task : workflow.tasks) {
        graph.addNode(task);
        task_lookup[task.name] = task;
    }

    validateTriggerReferences(workflow, task_lookup);

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

Workflow parseFileWithIncludes(const std::string& filePath, const std::string& basePath) {
    ParseContext ctx;
    fs::path resolved_path(filePath);
    if (resolved_path.is_relative() && !basePath.empty()) {
        resolved_path = fs::path(basePath) / resolved_path;
    }
    return parseInternal(resolved_path.string(), ctx);
}

} // namespace TaskParser
