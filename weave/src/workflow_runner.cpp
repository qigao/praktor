#include "workflow_runner.hpp"
#include "util/logger.hpp"
#include "util/file_utils.hpp"
#include "yml/task_parser.hpp"
#include "dag/workflow_executor.hpp"

#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <ctime> // For std::time
#include <functional> // For std::function
#include <chrono> // For std::chrono

// Helper function to apply defaults to a task
Task applyDefaults(const Task& task, const Task& defaults) {
    Task result = task;  // Start with the original task

    // Apply retries defaults if task doesn't specify retries
    if (result.retries.count == 0 && defaults.retries.count > 0) {
        result.retries = defaults.retries;
        LOG_INFO("Applied default retries to task '" + task.name + "': count=" +
                std::to_string(defaults.retries.count) + ", delay=" + defaults.retries.delay);
    }

    // Apply timeout default if task doesn't specify timeout
    if (result.timeout.empty() && !defaults.timeout.empty()) {
        result.timeout = defaults.timeout;
        LOG_INFO("Applied default timeout to task '" + task.name + "': " + defaults.timeout);
    }

    // Apply vars defaults (merge, task vars take precedence)
    for (const auto& [key, value] : defaults.vars) {
        if (result.vars.find(key) == result.vars.end()) {
            result.vars[key] = value;
            LOG_INFO("Applied default variable to task '" + task.name + "': " + key + "=" + value);
        }
    }

    return result;
}

// Constructor
WorkflowRunner::WorkflowRunner(std::string const& yamlPath, std::unordered_map<std::string, std::string> inputValues)
    : yamlPath_(yamlPath), inputValues_(std::move(inputValues)), base_directory_(std::filesystem::path(yamlPath).parent_path()) {}

// Destructor
WorkflowRunner::~WorkflowRunner() = default;

// Main run method
// Helper function to expand task templates
std::vector<Task> expandTaskTemplates(const Workflow& workflow, const std::unordered_map<std::string, std::string>& inputValues) {
    std::vector<Task> expanded_tasks;

    // Create a map for quick lookup of task templates
    std::unordered_map<std::string, TaskTemplate> template_map;
    for (const auto& tmpl : workflow.task_templates) {
        template_map[tmpl.name] = tmpl;
    }

    for (const auto& task : workflow.tasks) {
        auto it = template_map.find(task.type);
        if (it != template_map.end()) {
            // This task is a custom template type, expand it
            const TaskTemplate& tmpl = it->second;
            LOG_INFO("Expanding custom task template: " + tmpl.name + " for task: " + task.name);

            // Validate and collect parameters for substitution
            std::unordered_map<std::string, std::string> params_for_substitution;
            for (const auto& param_def : tmpl.parameters) {
                auto param_val_it = task.vars.find(param_def.name);
                if (param_val_it != task.vars.end()) {
                    params_for_substitution[param_def.name] = param_val_it->second;
                } else if (!param_def.default_value.empty()) {
                    params_for_substitution[param_def.name] = param_def.default_value;
                } else {
                    throw std::runtime_error("Missing required parameter '" + param_def.name + "' for template '" + tmpl.name + "'");
                }
            }

            // Perform substitution on template tasks
            for (const auto& tmpl_task : tmpl.tasks) {
                Task expanded_tmpl_task = tmpl_task; // Copy
                expanded_tmpl_task.name = task.name + "_" + expanded_tmpl_task.name; // Prefix with parent task name

                // Substitute parameters in command, working_directory, environment, when, etc.
                // This is a simplified substitution, a more robust solution would use a dedicated templating engine
                // For now, we only substitute in string fields that are likely to contain parameters

                // Substitute in task name (for depends_on references)
                for (auto& dep : expanded_tmpl_task.depends_on) {
                    for (const auto& [param_key, param_val] : params_for_substitution) {
                        size_t pos = dep.find("{{" + param_key + "}}");
                        if (pos != std::string::npos) {
                            dep.replace(pos, param_key.length() + 4, param_val);
                        }
                    }
                }

                // Substitute in when condition
                for (const auto& [param_key, param_val] : params_for_substitution) {
                    size_t pos = expanded_tmpl_task.when.find("{{" + param_key + "}}");
                    if (pos != std::string::npos) {
                        expanded_tmpl_task.when.replace(pos, param_key.length() + 4, param_val);
                    }
                }

                // Substitute in task-specific parameters (RunCommandParams for now)
                if (expanded_tmpl_task.specifics.index() == 0) { // RunCommandParams
                    RunCommandParams& rc_params = std::get<RunCommandParams>(expanded_tmpl_task.specifics);
                    if (std::holds_alternative<std::string>(rc_params.command)) {
                        std::string& cmd = std::get<std::string>(rc_params.command);
                        for (const auto& [param_key, param_val] : params_for_substitution) {
                            size_t pos = cmd.find("{{" + param_key + "}}");
                            if (pos != std::string::npos) {
                                cmd.replace(pos, param_key.length() + 4, param_val);
                            }
                        }
                    } // TODO: handle StrList command

                    for (auto& [env_key, env_val] : rc_params.environment) {
                        for (const auto& [param_key, param_val] : params_for_substitution) {
                            size_t pos = env_val.find("{{" + param_key + "}}");
                            if (pos != std::string::npos) {
                                env_val.replace(pos, param_key.length() + 4, param_val);
                            }
                        }
                    }
                }
                // TODO: Handle other task types and their parameters

                expanded_tasks.push_back(expanded_tmpl_task);
            }
        } else {
            // Not a custom template, add as is
            expanded_tasks.push_back(task);
        }
    }
    return expanded_tasks;
}

bool WorkflowRunner::run(bool useConcurrent, int maxConcurrency) {
    try {
        LOG_INFO("Loading workflow from: " + yamlPath_);
        // Pass the base_directory_ for import resolution
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        // Expand custom task templates
        workflow.tasks = expandTaskTemplates(workflow, inputValues_);

        // Apply defaults to all tasks and set default working directory
        for (auto& task : workflow.tasks) {
            task = applyDefaults(task, workflow.defaults);
            // Set default working directory for run_command and create_directory tasks
            if (task.specifics.index() == 0) { // RunCommandParams
                RunCommandParams& rc_params = std::get<RunCommandParams>(task.specifics);
                if (rc_params.working_directory.empty()) {
                    rc_params.working_directory = base_directory_.string();
                }
            } else if (task.specifics.index() == 1) { // CreateDirectoryParams
                CreateDirectoryParams& cd_params = std::get<CreateDirectoryParams>(task.specifics);
                std::filesystem::path p(cd_params.path);
                if (p.is_relative()) {
                    cd_params.path = (base_directory_ / p).string();
                }
            }
        }

        WorkflowContext context;

        // Initialize context with variables from workflow
        LOG_INFO("Initializing workflow context...");
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
            LOG_INFO("Set variable: " + key + " = " + value);
        }

        // Process inputs: apply defaults and user-provided values
        LOG_INFO("Processing workflow inputs...");
        for (const auto& input : workflow.inputs) {
            WorkflowValue value;

            // Check if user provided a value for this input
            auto userValueIt = inputValues_.find(input.name);
            if (userValueIt != inputValues_.end()) {
                // Attempt to parse as JSON first, then as bool/int, then string
                try {
                    value = jsoncons::json::parse(userValueIt->second);
                } catch (const jsoncons::json_exception&) {
                    if (userValueIt->second == "true") value = true;
                    else if (userValueIt->second == "false") value = false;
                    else if (std::all_of(userValueIt->second.begin(), userValueIt->second.end(), ::isdigit)) value = std::stoll(userValueIt->second);
                    else value = userValueIt->second;
                }
                LOG_INFO("Using user-provided input: " + input.name + " = " + userValueIt->second);
            } else if (!input.default_value.empty()) {
                // Attempt to parse as JSON first, then as bool/int, then string
                try {
                    value = jsoncons::json::parse(input.default_value);
                } catch (const jsoncons::json_exception&) {
                    if (input.default_value == "true") value = true;
                    else if (input.default_value == "false") value = false;
                    else if (std::all_of(input.default_value.begin(), input.default_value.end(), ::isdigit)) value = std::stoll(input.default_value);
                    else value = input.default_value;
                }
                LOG_INFO("Using default input: " + input.name + " = " + input.default_value);
            } else {
                // Required input not provided
                throw std::runtime_error("Required input '" + input.name + "' not provided and has no default value");
            }

            // TODO: Add type validation based on input.type

            // Add input as a variable in context
            context.setValue(input.name, value);
        }

        LOG_INFO("Building task graph...");
        EnhancedGraph<Task> graph = TaskParser::buildGraph(workflow);

        LOG_INFO("Starting workflow execution...");
        WorkflowExecutor executor(graph, useConcurrent ? maxConcurrency : 1);
        executor.execute(context);

        // Check final status
        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        if (status == "failed") {
            LOG_ERROR("Workflow execution failed.");
            return false;
        }

        LOG_INFO("Workflow finished successfully.");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("An error occurred during workflow execution: " + std::string(e.what()));
        return false;
    }
}

// runTask is not implemented in this refactoring, as it requires more complex graph slicing logic
bool WorkflowRunner::runTask(std::string const& taskName, bool useConcurrent, int maxConcurrency) {
    try {
        LOG_INFO("Loading workflow to run single task: " + taskName);
        // Pass the base_directory_ for import resolution
        Workflow workflow = TaskParser::parseFileWithImports(yamlPath_, base_directory_.string());

        // Expand custom task templates
        std::vector<Task> expanded_tasks = expandTaskTemplates(workflow, inputValues_);
        workflow.tasks = expanded_tasks; // Update workflow with expanded tasks

        // Find the target task in the expanded list
        auto targetTaskIt = std::find_if(workflow.tasks.begin(), workflow.tasks.end(),
            [&](const Task& task) { return task.name == taskName; });

        if (targetTaskIt == workflow.tasks.end()) {
            throw std::runtime_error("Target task '" + taskName + "' not found in workflow.");
        }

        // Apply defaults to all tasks before building graph
        for (auto& task : workflow.tasks) {
            task = applyDefaults(task, workflow.defaults);
        }

        // Build the full graph first
        EnhancedGraph<Task> fullGraph = TaskParser::buildGraph(workflow);

        // Create a subgraph containing only the target task and its dependencies
        LOG_INFO("Creating subgraph for task: " + taskName);
        EnhancedGraph<Task> subgraph = fullGraph.createSubgraphFor(*targetTaskIt);

        // Setup context (similar to full run)
        WorkflowContext context;
        for (const auto& [key, value] : workflow.variables) {
            context.setValue(key, value);
        }
        for (const auto& input : workflow.inputs) {
            std::string value;
            auto userValueIt = inputValues_.find(input.name);
            if (userValueIt != inputValues_.end()) {
                value = userValueIt->second;
            } else if (!input.default_value.empty()) {
                value = input.default_value;
            } else {
                throw std::runtime_error("Required input '" + input.name + "' not provided.");
            }
            context.setValue(input.name, value);
        }

        LOG_INFO("Executing subgraph for task: " + taskName);
        WorkflowExecutor executor(subgraph, useConcurrent ? maxConcurrency : 1);
        executor.execute(context);

        // Check final status
        std::string status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
        return status != "failed";

    } catch (const std::exception& e) {
        LOG_ERROR("An error occurred during single task execution: " + std::string(e.what()));
        return false;
    }
}
