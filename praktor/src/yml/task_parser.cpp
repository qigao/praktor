#include "yml/task_parser.hpp"
#include "yml/task_yaml.hpp"

#include "util/logging.hpp"

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
    std::vector<std::string> import_stack;
    std::unordered_set<std::string> import_set;  // mirrors import_stack for O(1) lookup
    std::unordered_map<std::string, Workflow> cache;

    bool is_circular(const std::string& path) const {
        return import_set.count(path) > 0;
    }

    void push_import(const std::string& path) {
        import_stack.push_back(path);
        import_set.insert(path);
    }

    void pop_import() {
        if (!import_stack.empty()) {
            import_set.erase(import_stack.back());
            import_stack.pop_back();
        }
    }
};

void applyTaskDefaults(Task& task, const TaskDefaults& defaults) {
    if (!task.retries && defaults.retries) {
        task.retries = defaults.retries;
    }
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

// Load modules from imported .yml or .js files
void loadImportedModules(Workflow& workflow, const fs::path& base_dir, ParseContext& ctx) {
    for (const auto& import_file : workflow.imports.files) {
        fs::path import_path = base_dir / import_file;
        std::string abs_path = fs::absolute(import_path).lexically_normal().string();

        if (!fs::exists(abs_path)) {
            throw std::runtime_error("Import file not found: " + abs_path);
        }

        std::string ext = import_path.extension().string();

        if (ext == ".yml" || ext == ".yaml") {
            // Import embedded modules from YAML file
            std::string content = readFileToString(abs_path);
            ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(content));
            auto root = tree.rootref();

            if (root.has_child("embedded")) {
                const auto& embedded_node = root["embedded"];
                if (embedded_node.is_map()) {
                    for (const auto& module_node : embedded_node) {
                        std::string module_name(module_node.key().str, module_node.key().len);

                        // Don't override existing modules
                        if (workflow.embedded.find(module_name) != workflow.embedded.end()) {
                            continue;
                        }

                        if (!module_node.is_map()) continue;

                        EmbeddedModule module;
                        if (module_node.has_child("language")) {
                            module_node["language"] >> module.language;
                        }
                        if (module_node.has_child("source")) {
                            module_node["source"] >> module.source;
                        }
                        if (module_node.has_child("path")) {
                            module_node["path"] >> module.path;
                            // Resolve path relative to the imported YAML file
                            if (!module.path.empty() && !fs::path(module.path).is_absolute()) {
                                module.path = (import_path.parent_path() / module.path).lexically_normal().string();
                            }
                        }

                        if (!module.source.empty() || !module.path.empty()) {
                            workflow.embedded[module_name] = std::move(module);
                        }
                    }
                }
            }
        } else if (ext == ".js" || ext == ".mjs") {
            // Import .js file as a module (use filename without extension as module name)
            std::string module_name = import_path.stem().string();

            // Don't override existing modules
            if (workflow.embedded.find(module_name) != workflow.embedded.end()) {
                continue;
            }

            EmbeddedModule module;
            module.language = "javascript";
            module.path = abs_path;
            workflow.embedded[module_name] = std::move(module);
        }
    }
}

// Load source content for modules that have 'path' instead of inline 'source'
void loadExternalModuleSources(Workflow& workflow, const fs::path& base_dir) {
    for (auto& [name, module] : workflow.embedded) {
        if (module.isExternal() && module.source.empty()) {
            fs::path module_path = module.path;
            if (!module_path.is_absolute()) {
                module_path = base_dir / module_path;
            }

            if (!fs::exists(module_path)) {
                throw std::runtime_error("Module file not found: " + module_path.string() + " (module: " + name + ")");
            }

            module.source = readFileToString(module_path.string());
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
        for (const auto& s : ctx.import_stack) {
            stack_str += s + " -> ";
        }
        throw std::runtime_error("Circular import detected: " + stack_str + absolutePath);
    }

    ctx.push_import(absolutePath);

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

        normalizeWorkflow(workflow);

        // Process module imports
        if (!workflow.imports.empty()) {
            loadImportedModules(workflow, fs::path(absolutePath).parent_path(), ctx);
        }

        // Load external module files (modules with 'path' instead of 'source')
        loadExternalModuleSources(workflow, fs::path(absolutePath).parent_path());

        ctx.cache[absolutePath] = workflow;
        ctx.pop_import();
        return workflow;
    } catch (const std::exception& e) {
        std::string error_msg = absolutePath + ": " + e.what();
        TLOG_ERROR("YAML parsing error: {}", error_msg);
        ctx.pop_import();
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

Workflow parseFileWithImports(const std::string& filePath, const std::string& /*basePath*/) {
    ParseContext ctx;
    return parseInternal(filePath, ctx);
}

} // namespace TaskParser
