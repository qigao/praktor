#include "dag/workflow_executor.hpp"

#include "dag/scoped_variables.hpp"
#include "dag/trigger_executor.hpp"
#include "executors/command_executor.hpp"
#include "executors/dynamic_tasks_executor.hpp"
#include "executors/script_executor.hpp"
#include "executors/uses_executor.hpp"
#include "expressions/expression_evaluator.hpp"
#include "util/env_parser.hpp"
#include "util/file_hash.hpp"
#include "util/string_utils.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_parser.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fmtlog.h>
#include <fstream>
#include <jsoncons/json.hpp>
#include <optional>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

std::filesystem::path resolveRelativePath(const std::string &base, const std::string &child) {
  std::filesystem::path base_path =
      base.empty() ? std::filesystem::current_path() : std::filesystem::path(base).parent_path();
  std::filesystem::path relative(child);
  if (relative.is_absolute()) {
    return relative.lexically_normal();
  }
  return (base_path / relative).lexically_normal();
}

void sleepWithDelay(const std::string &delay) {
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

std::vector<jsoncons::json> generateMatrixCombinations(const std::unordered_map<std::string, StrList> &matrix) {
  if (matrix.empty())
    return {};

  std::vector<std::string> keys;
  std::vector<StrList> values;
  for (const auto &[key, list] : matrix) {
    keys.push_back(key);
    values.push_back(list);
  }

  std::vector<jsoncons::json> results;
  std::vector<size_t> indices(keys.size(), 0);
  bool done = false;
  while (!done) {
    jsoncons::json combination = jsoncons::json::object();
    for (size_t i = 0; i < keys.size(); ++i) {
      combination[keys[i]] = values[i][indices[i]];
    }
    results.push_back(std::move(combination));

    // Advance indices
    for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) {
      indices[i]++;
      if (indices[i] < values[i].size()) {
        break;
      }
      indices[i] = 0;
      if (i == 0) {
        done = true;
      }
    }
  }
  return results;
}

std::vector<jsoncons::json> generateEachCombinations(const Each &each) {
  if (each.hasItems()) {
    std::vector<jsoncons::json> results;
    for (const auto &item : each.items) {
      results.push_back(jsoncons::json(item));
    }
    return results;
  }
  if (each.hasMatrix()) {
    return generateMatrixCombinations(each.matrix);
  }
  return {};
}

} // namespace

WorkflowExecutor::WorkflowExecutor(DependencyGraph<Task> &graph,
                                   std::unordered_map<std::string, std::string> base_environment,
                                   size_t num_threads)
    : graph_(graph), base_environment_(std::move(base_environment)), max_concurrency_(num_threads),
      thread_pool_(num_threads > 1 ? std::make_unique<pubcxx::ThreadPool>(num_threads) : nullptr) {

  // Initialize executor registry
  executors_[TaskAction::RunCommand] = Praktor::Execution::createCommandExecutor();
  executors_[TaskAction::Script] = Praktor::Execution::createScriptExecutor();
  executors_[TaskAction::Uses] =
      Praktor::Execution::createUsesExecutor(base_environment_, num_threads);

  // DynamicTasks executor needs a callback to execute generated subtasks
  auto dynamic_executor = Praktor::Execution::createDynamicTasksExecutor();
  auto *dt_ptr = static_cast<Praktor::Execution::DynamicTasksExecutor *>(dynamic_executor.get());
  dt_ptr->setSubTaskCallback([this](const Task &task, WorkflowContext &ctx) {
    return this->executeTask(task, ctx, std::nullopt);
  });
  executors_[TaskAction::DynamicTasks] = std::move(dynamic_executor);
}

void WorkflowExecutor::execute(WorkflowContext &context, std::optional<std::string> alias) {
  auto nodes = graph_.getNodes();
  if (nodes.empty())
    return;

  loadCache(nodes.front().source_path);

  struct SharedState {
    std::unordered_map<Task, int> in_degree;
    std::unordered_set<std::string> scheduled;
    size_t completed = 0;
    size_t active = 0;
    bool workflow_failed = false;
    bool should_stop = false;
  } state;

  for (const auto &task : nodes) {
    state.in_degree[task] = graph_.getInDegree(task);
  }

  std::function<void(const Task &)> pushTask;
  pushTask = [&](const Task &task) {
    auto runTask = [this, task, &context, &state, alias, &pushTask]() {
      std::unique_ptr<WorkflowContext> task_context;
      WorkflowContext *ctx_ptr = &context;

      if (this->thread_pool_) {
        task_context = context.fork();
        ctx_ptr = task_context.get();
      }

      bool success = executeTask(task, *ctx_ptr, alias);

      std::vector<Task> ready_neighbors;
      {
        std::lock_guard<std::mutex> lock(this->execution_mutex_);
        state.active--;
        state.completed++;

        if (!success) {
          state.workflow_failed = true;
          if (!task.continue_on_error) {
            state.should_stop = true;
          }
        }

        if (!state.should_stop) {
          for (const auto &neighbor : graph_.getEdges(task)) {
            auto it = state.in_degree.find(neighbor);
            if (it != state.in_degree.end()) {
              it->second -= 1;
              if (it->second == 0 && state.scheduled.find(neighbor.name) == state.scheduled.end()) {
                state.scheduled.insert(neighbor.name);
                state.active++;
                ready_neighbors.push_back(neighbor);
              }
            }
          }
        }
        this->execution_cv_.notify_all();
      }

      for (const auto &neighbor : ready_neighbors) {
        pushTask(neighbor);
      }
    };

    if (thread_pool_) {
      thread_pool_->enqueue(std::move(runTask));
    } else {
      runTask();
    }
  };

  // Initial scheduling
  std::vector<Task> initial_ready;
  {
    std::lock_guard<std::mutex> lock(execution_mutex_);
    for (const auto &task : nodes) {
      if (state.in_degree[task] == 0) {
        state.scheduled.insert(task.name);
        state.active++;
        initial_ready.push_back(task);
      }
    }
  }

  for (const auto &task : initial_ready) {
    pushTask(task);
  }

  // Wait for completion or failure
  std::unique_lock<std::mutex> lock(execution_mutex_);
  execution_cv_.wait(lock, [&]() {
    return (state.should_stop && state.active == 0) || (state.completed == nodes.size());
  });

  // Wait for all active tasks if we stopped early due to failure
  if (state.active > 0) {
    execution_cv_.wait(lock, [&]() { return state.active == 0; });
  }

  if (state.completed != nodes.size() && !state.should_stop) {
    throw std::runtime_error("Workflow graph contains cycles or unreachable tasks");
  }

  context.setValue("workflow_status",
                   (state.workflow_failed && !state.should_stop) || state.should_stop ? "failed"
                                                                                      : "success");

  saveCache();
}

void WorkflowExecutor::loadCache(const std::string &workflow_path) {
  if (workflow_path.empty())
    return;
  auto path = std::filesystem::path(workflow_path).parent_path() / ".praktor_cache";
  cache_file_ = path.string();

  if (!std::filesystem::exists(path))
    return;

  try {
    std::ifstream is(path);
    jsoncons::json j = jsoncons::json::parse(is);
    for (auto const &item : j.array_range()) {
      std::string task_name = item["task"].as_string();
      TaskCacheState state;
      state.action_hash = item["action_hash"].as_string();
      if (item.contains("sources")) {
        for (auto const &src : item["sources"].object_range()) {
          state.source_hashes[src.key()] = src.value().as_string();
        }
      }
      cache_[task_name] = std::move(state);
    }
  } catch (...) {
    logw("Failed to load cache from {}", cache_file_);
  }
}

void WorkflowExecutor::saveCache() {
  if (cache_file_.empty() || cache_.empty())
    return;

  try {
    jsoncons::json j = jsoncons::json::array();
    for (auto const &[name, state] : cache_) {
      jsoncons::json item;
      item["task"] = name;
      item["action_hash"] = state.action_hash;
      jsoncons::json sources = jsoncons::json::object();
      for (auto const &[path, hash] : state.source_hashes) {
        sources[path] = hash;
      }
      item["sources"] = std::move(sources);
      j.push_back(std::move(item));
    }

    std::ofstream os(cache_file_);
    os << jsoncons::pretty_print(j);
  } catch (...) {
    logw("Failed to save cache to {}", cache_file_);
  }
}

bool WorkflowExecutor::checkSkipTask(const Task &task, WorkflowContext &context) {
  if (!use_cache_ || (task.sources.empty() && task.generates.empty())) {
    return false;
  }

  auto it = cache_.find(task.name);
  if (it == cache_.end())
    return false;

  // Check if generates exist
  for (const auto &gen : task.generates) {
    auto path = resolveRelativePath(task.source_path, gen);
    if (!std::filesystem::exists(path))
      return false;
  }

  // Check if sources changed
  for (const auto &src : task.sources) {
    auto path = resolveRelativePath(task.source_path, src);
    auto current_hash = Praktor::Util::computeFileHash(path);
    auto cached_it = it->second.source_hashes.find(path.string());
    if (cached_it == it->second.source_hashes.end() || cached_it->second != current_hash) {
      return false;
    }
  }

  logi("Skipping task '{}' (already up to date)", task.name);
  return true;
}

void WorkflowExecutor::updateTaskCache(const Task &task, WorkflowContext &context) {
  if (!use_cache_ || (task.sources.empty() && task.generates.empty())) {
    return;
  }

  TaskCacheState state;
  for (const auto &src : task.sources) {
    auto path = resolveRelativePath(task.source_path, src);
    state.source_hashes[path.string()] = Praktor::Util::computeFileHash(path);
  }

  std::lock_guard<std::mutex> lock(execution_mutex_);
  cache_[task.name] = std::move(state);
}

bool WorkflowExecutor::executeTask(const Task &task, WorkflowContext &context,
                                   std::optional<std::string> alias) {
  // Mark as running in registry to allow setOutput calls
  context.setTaskStatus(task.name, "running");
  if (alias && alias.value() != task.name) {
    context.setTaskStatus(alias.value(), "running");
  }

  bool overall_success = false;
  std::string final_status = "failed";

  if (task.each && task.each->enabled()) {
    auto combinations = generateEachCombinations(*task.each);
    logi("Executing task '{}' for {} combinations", task.name, combinations.size());

    bool all_success = true;
    bool any_executed = false;
    for (size_t i = 0; i < combinations.size(); ++i) {
      auto child_context = context.fork();
      child_context->setValue(task.each->as, combinations[i]);
      if (!task.each->index_variable.empty()) {
        child_context->setValue(task.each->index_variable, std::to_string(i));
      }

      auto [success, status] = executeTaskInternal(task, *child_context, alias);
      if (!success) {
        all_success = false;
      }
      if (status != "skipped") {
        any_executed = true;
      }
    }
    overall_success = all_success;
    if (!any_executed && overall_success) {
      final_status = "skipped";
    } else {
      final_status = overall_success ? "success" : "failed";
    }
  } else {
    auto [success, status] = executeTaskInternal(task, context, alias);
    overall_success = success;
    final_status = status;
  }

  // Finalize in registry
  context.setTaskStatus(task.name, final_status);
  if (alias && alias.value() != task.name) {
    context.setTaskStatus(alias.value(), final_status);
  }

  return overall_success;
}

std::pair<bool, std::string> WorkflowExecutor::executeTaskInternal(const Task &task, WorkflowContext &context,
                                           std::optional<std::string> alias) {
  context.pushTaskScope(task.name, alias);
  ScopedVariables scoped_vars(context, task.vars);

  auto retries = task.retries.value_or(RetryPolicy{});
  int attempts = std::max(1, retries.count + 1);

  bool success = false;
  std::string status = "skipped";

  try {
    if (!evaluateWhen(task, context)) {
      success = true;
      status = "skipped";
    } else if (checkSkipTask(task, context)) {
      success = true;
      status = "skipped";
    } else {
      auto it = executors_.find(task.action);
      if (it == executors_.end()) {
        throw std::runtime_error("No executor registered for task action");
      }

      TaskExecutor *executor = it->second.get();

      for (int attempt = 0; attempt < attempts; ++attempt) {
        if (attempt > 0) {
          logw("Retrying task '{}' ({}/{})", task.name, attempt, attempts - 1);
          sleepWithDelay(retries.delay);
        }

        TaskResult result = executor->execute(task, context);

        if (result.success) {
          success = true;
          status = "success";
          updateTaskCache(task, context);
          break;
        }
      }

      if (!success) {
        status = "failed";
      }
    }
  } catch (const std::exception &e) {
    loge("Task '{}' failed: {}", task.name, e.what());
    success = false;
    status = "failed";
  }

  executeTriggers(task, success, context);
  context.popTaskScope();

  return {success, status};
}

bool WorkflowExecutor::evaluateWhen(const Task &task, WorkflowContext &context) const {
  if (!task.when || task.when->empty()) {
    return true;
  }

  try {
    return Praktor::Expressions::ExpressionEvaluator{}.evaluateAsBool(*task.when, context);
  } catch (const std::exception &e) {
    loge("Failed to evaluate 'when' expression for task '{}': {}", task.name, e.what());
    return false;
  }
}

std::unordered_map<std::string, std::string> WorkflowExecutor::buildTaskEnvironment(
    const Task &task, const std::unordered_map<std::string, std::string> &inherited_env,
    WorkflowContext &context) const {
  EnvMap env = inherited_env;

  for (const auto &file : task.dot_env) {
    std::filesystem::path env_path = resolveRelativePath(task.source_path, file);
    try {
      auto parsed = Praktor::util::parseDotEnvFile(env_path);
      for (const auto &[key, value] : parsed) {
        env[key] = substituteVariables(value, context);
      }
    } catch (const std::exception &e) {
      logw("Failed to load task dotEnv file '{}': {}", env_path.string(), e.what());
    }
  }

  for (const auto &[key, value] : task.env) {
    env[key] = substituteVariables(value, context);
  }

  return env;
}

void WorkflowExecutor::executeTriggers(const Task &task, bool success, WorkflowContext &context) {
  if (!task.triggers || task.triggers->empty()) {
    return;
  }

  logd("Executing triggers for task '{}' (success={})", task.name, success);

  try {
    trigger_executor_.executeTriggers(task, success, context, base_environment_);
  } catch (const std::exception &e) {
    logw("Trigger execution encountered an error: {}", e.what());
  }
}
