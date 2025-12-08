#include "dag/workflow_executor.hpp"

#include "dag/scoped_variables.hpp"
#include "dag/trigger_executor.hpp"
#include "executors/command_executor.hpp"
#include "executors/script_executor.hpp"
#include "executors/uses_executor.hpp"
#include "expressions/expression_evaluator.hpp"
#include "util/logger.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_parser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

std::string trim(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

EnvMap parseDotEnvFile(const std::filesystem::path& file_path)
{
    EnvMap result;
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open .env file: " + file_path.string());
    }

    std::string line;
    while (std::getline(input, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        if (trimmed.rfind("export ", 0) == 0) {
            trimmed = trim(trimmed.substr(7));
        }

        size_t equals_pos = trimmed.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(trimmed.substr(0, equals_pos));
        std::string value = trim(trimmed.substr(equals_pos + 1));
        if (!value.empty() && value.size() >= 2) {
            bool quoted = (value.front() == '"' && value.back() == '"')
                       || (value.front() == '\'' && value.back() == '\'');
            if (quoted) {
                value = value.substr(1, value.size() - 2);
            }
        }

        if (!key.empty()) {
            result[key] = value;
        }
    }

    return result;
}

std::filesystem::path resolveRelativePath(const std::string& base, const std::string& child)
{
    std::filesystem::path base_path = base.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(base).parent_path();
    std::filesystem::path relative(child);
    if (relative.is_absolute()) {
        return relative.lexically_normal();
    }
    return (base_path / relative).lexically_normal();
}

void sleepWithDelay(const std::string& delay)
{
    if (delay.empty()) {
        return;
    }
    try {
        using namespace std::chrono_literals;
        if (delay.back() == 's') {
            int seconds = std::stoi(delay.substr(0, delay.size() - 1));
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
        } else if (delay.back() == 'm') {
            int minutes = std::stoi(delay.substr(0, delay.size() - 1));
            std::this_thread::sleep_for(std::chrono::minutes(minutes));
        } else if (delay.back() == 'h') {
            int hours = std::stoi(delay.substr(0, delay.size() - 1));
            std::this_thread::sleep_for(std::chrono::hours(hours));
        }
    } catch (...) {
        // Ignore malformed delays
    }
}

} // namespace

WorkflowExecutor::WorkflowExecutor(DependencyGraph<Task>& graph,
                                   std::unordered_map<std::string, std::string> base_environment,
                                   size_t num_threads)
    : graph_(graph)
    , base_environment_(std::move(base_environment))
    , max_concurrency_(num_threads)
{
    (void)max_concurrency_; // Sequential executor for now
    
    // Initialize executor registry - no switch/case needed!
    executors_[TaskAction::RunCommand] = Weave::Execution::createCommandExecutor();
    executors_[TaskAction::Script] = Weave::Execution::createScriptExecutor();
    executors_[TaskAction::Uses] = Weave::Execution::createUsesExecutor(base_environment_, num_threads);
}
void WorkflowExecutor::execute(WorkflowContext& context, std::optional<std::string> alias)
{
    auto nodes = graph_.getNodes();
    std::unordered_map<Task, int> in_degree;
    std::unordered_map<std::string, Task> task_lookup;

    for (const auto& task : nodes) {
        in_degree[task] = graph_.getInDegree(task);
        task_lookup[task.name] = task;
    }

    std::queue<Task> ready;
    for (const auto& task : nodes) {
        if (in_degree[task] == 0) {
            ready.push(task);
        }
    }

    size_t completed = 0;
    while (!ready.empty()) {
        Task current = ready.front();
        ready.pop();

        bool success = executeTask(current, context, alias);
        if (!success) {
            context.setValue("workflow_status", "failed");
            return;
        }

        completed++;

        for (const auto& neighbor : graph_.getEdges(current)) {
            auto it = in_degree.find(neighbor);
            if (it == in_degree.end()) {
                continue;
            }
            it->second -= 1;
            if (it->second == 0) {
                ready.push(neighbor);
            }
        }
    }

    if (completed != nodes.size()) {
        throw std::runtime_error("Workflow graph contains cycles or unreachable tasks");
    }

    context.setValue("workflow_status", "success");
}
bool WorkflowExecutor::executeTask(const Task& task,
                                   WorkflowContext& context,
                                   std::optional<std::string> alias)
{
    context.pushTaskScope(task.name, alias);

    // RAII: Variables automatically restored when scope exits
    ScopedVariables scoped_vars(context, task.vars);

    auto retries = task.retries.value_or(RetryPolicy{});
    int attempts = std::max(1, retries.count + 1);

    bool success = false;
    std::string status = "skipped";

    try {
        if (!evaluateWhen(task, context)) {
            success = true;
            status = "skipped";
        } else {
            // Look up executor from registry - no switch/case!
            auto it = executors_.find(task.action);
            if (it == executors_.end()) {
                throw std::runtime_error("No executor registered for task action");
            }
            
            TaskExecutor* executor = it->second.get();
            
            for (int attempt = 0; attempt < attempts; ++attempt) {
                if (attempt > 0) {
                    LOG_WARNING("Retrying task '" + task.name + "' (" + std::to_string(attempt) + "/" + std::to_string(attempts - 1) + ")");
                    sleepWithDelay(retries.delay);
                }

                TaskResult result = executor->execute(task, context);

                if (result.success) {
                    success = true;
                    status = "success";
                    break;
                }
            }

            if (!success) {
                status = "failed";
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Task '" + task.name + "' failed: " + e.what());
        success = false;
        status = "failed";
    }

    context.setTaskStatus(task.name, status);
    if (alias && alias.value() != task.name) {
        context.setTaskStatus(alias.value(), status);
    }

    executeTriggers(task, success, context);

    context.popTaskScope();

    return success;
}
bool WorkflowExecutor::evaluateWhen(const Task& task,
                                    WorkflowContext& context) const
{
    if (!task.when || task.when->empty()) {
        return true;
    }

    try {
        return Weave::Expressions::ExpressionEvaluator{}.evaluateAsBool(*task.when, context);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to evaluate 'when' expression for task '" + task.name + "': " + e.what());
        return false;
    }
}

std::unordered_map<std::string, std::string> WorkflowExecutor::buildTaskEnvironment(
    const Task& task,
    const std::unordered_map<std::string, std::string>& inherited_env,
    WorkflowContext& context) const
{
    EnvMap env = inherited_env;

    for (const auto& file : task.dot_env) {
        std::filesystem::path env_path = resolveRelativePath(task.source_path, file);
        try {
            auto parsed = parseDotEnvFile(env_path);
            for (const auto& [key, value] : parsed) {
                env[key] = substituteVariables(value, context);
            }
        } catch (const std::exception& e) {
            LOG_WARNING("Failed to load task dotEnv file '" + env_path.string() + "': " + e.what());
        }
    }

    for (const auto& [key, value] : task.env) {
        env[key] = substituteVariables(value, context);
    }

    return env;
}

void WorkflowExecutor::executeTriggers(const Task& task, bool success, WorkflowContext& context)
{
    if (!task.triggers || task.triggers->empty()) {
        return;
    }

    LOG_DEBUG("Executing triggers for task '" + task.name + "' (success=" + (success ? "true" : "false") + ")");

    try {
        trigger_executor_.executeTriggers(task, success, context, base_environment_);
    } catch (const std::exception& e) {
        LOG_WARNING("Trigger execution encountered an error: " + std::string(e.what()));
        // Don't fail the task due to trigger failures
    }
}

