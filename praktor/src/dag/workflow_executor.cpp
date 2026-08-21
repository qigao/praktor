#include "dag/workflow_executor.hpp"
#include "workflow_executor_internal.hpp"

#include "dag/scoped_variables.hpp"
#include "dag/trigger_executor.hpp"
#include "script/script_engine.hpp"
// CommandExecutor removed: command: tasks are desugared to BT at parse time
#include "executors/dynamic_tasks_executor.hpp"
#include "executors/uses_executor.hpp"
#include "executors/declarative_tree_executor.hpp"
#include "executors/download_executor.hpp"
#include "executors/managed_process_executor.hpp"
#include "executors/program_executor.hpp"
#include "executors/service_executor.hpp"
#include "expressions/expression_evaluator.hpp"
#include "util/env_parser.hpp"
#include "util/file_hash.hpp"
#include "util/path_utils.hpp"
#include "util/shared_thread_pool.hpp"
#include "util/string_utils.hpp"
#include "util/variable_substitution.hpp"
#include "yml/task_parser.hpp"

#include "util/logging.hpp"
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
#include <variant>

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;
constexpr size_t kMaxLoggedTaskOutputBytes = 4096;

std::vector<WorkflowValue>
generateMatrixCombinations(const std::unordered_map<std::string, StrList> &matrix) {
  if (matrix.empty())
    return {};

  std::vector<std::string> keys;
  std::vector<StrList> values;
  for (const auto &[key, list] : matrix) {
    keys.push_back(key);
    values.push_back(list);
  }

  std::vector<WorkflowValue> results;
  std::vector<size_t> indices(keys.size(), 0);
  bool done = false;
  while (!done) {
    WorkflowValue combination = WorkflowValue::object();
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

std::vector<WorkflowValue> generateEachCombinations(const Each &each) {
  if (each.hasItems()) {
    std::vector<WorkflowValue> results;
    for (const auto &item : each.items) {
      results.push_back(WorkflowValue(item));
    }
    return results;
  }
  if (each.hasMatrix()) {
    return generateMatrixCombinations(each.matrix);
  }
  return {};
}

std::string taskTypeName(const Task& task,
                         const std::unordered_map<TaskAction, std::unique_ptr<TaskExecutor>>& executors) {
  if (!task.declared_runner.empty()) {
    return task.declared_runner;
  }

  if (task.script && task.action == TaskAction::None) {
    return "script";
  }

  auto it = executors.find(task.action);
  if (it != executors.end() && it->second) {
    return it->second->getTaskType();
  }

  return "unknown";
}

TaskFailureContext buildFailureContext(const Task& task,
                                       const TaskResult& result,
                                       const WorkflowContext& context,
                                       const std::unordered_map<TaskAction, std::unique_ptr<TaskExecutor>>& executors) {
  TaskFailureContext failure;
  failure.task_name = task.name;
  failure.task_type = taskTypeName(task, executors);
  failure.exit_code = result.exit_code;
  failure.stdout_data = result.stdout_data;
  failure.stderr_data = result.stderr_data;
  failure.error_message = result.error_message;
  failure.error_code = result.error_code;
  failure.error_phase = result.error_phase;
  failure.error_details = result.error_details;

  WorkflowValue outputs = context.getValueByPath("tasks." + task.name + ".outputs");
  if (outputs.is_object()) {
    for (const auto& item : outputs.object_range()) {
      failure.captured_outputs[item.key()] = item.value();
    }
  }

  if (result.nested_failure_context.has_value()) {
    const TaskFailureContext& nested = *result.nested_failure_context;
    failure.inner_failure = std::make_shared<TaskFailureContext>(nested);
  }

  return failure;
}

std::string summarizeTaskOutputForLog(const std::string& output) {
  if (output.empty()) {
    return {};
  }

  if (output.size() <= kMaxLoggedTaskOutputBytes) {
    return output;
  }

  std::ostringstream stream;
  stream << output.substr(0, kMaxLoggedTaskOutputBytes)
         << "\n...[truncated " << (output.size() - kMaxLoggedTaskOutputBytes) << " bytes]";
  return stream.str();
}

void appendField(std::ostringstream& stream, const std::string& key, const std::string& value) {
  stream << key << ':' << value.size() << ':' << value << ';';
}

void appendStringList(std::ostringstream& stream, const std::string& key, const StrList& values) {
  stream << key << '[' << values.size() << ']';
  for (const auto& value : values) {
    appendField(stream, "item", value);
  }
}

void appendStringMap(std::ostringstream& stream, const std::string& key, const Vars& values) {
  std::vector<std::pair<std::string, std::string>> sorted(values.begin(), values.end());
  std::sort(sorted.begin(), sorted.end());
  stream << key << '{' << sorted.size() << '}';
  for (const auto& [map_key, value] : sorted) {
    appendField(stream, "key", map_key);
    appendField(stream, "value", value);
  }
}

void appendOptionalString(std::ostringstream& stream,
                          const std::string& key,
                          const std::optional<std::string>& value) {
  stream << key << (value.has_value() ? "=1;" : "=0;");
  if (value) {
    appendField(stream, key + ".value", *value);
  }
}

void appendCommand(std::ostringstream& stream,
                   const std::string& key,
                   const std::variant<std::string, StrList>& command) {
  if (std::holds_alternative<std::string>(command)) {
    appendField(stream, key + ".scalar", std::get<std::string>(command));
  } else {
    appendStringList(stream, key + ".list", std::get<StrList>(command));
  }
}

void appendOrchNode(std::ostringstream& stream, const OrchNode& node) {
  appendField(stream, "node.type", node.type);
  appendStringMap(stream, "node.params", node.params);
  stream << "node.children[" << node.children.size() << ']';
  for (const auto& child : node.children) {
    appendOrchNode(stream, child);
  }
}

std::string computeTaskActionHashImpl(const Task& task) {
  std::ostringstream stream;
  appendField(stream, "name", task.name);
  appendField(stream, "runner", task.declared_runner);
  appendField(stream, "action", std::to_string(static_cast<int>(task.action)));
  appendStringList(stream, "depends_on", task.depends_on);
  appendStringMap(stream, "vars", task.vars);
  appendStringMap(stream, "env", task.env);
  appendStringList(stream, "dot_env", task.dot_env);
  appendOptionalString(stream, "when", task.when);
  if (task.each) {
    stream << "each=1;";
    appendStringList(stream, "each.items", task.each->items);
    std::vector<std::pair<std::string, StrList>> matrix(task.each->matrix.begin(), task.each->matrix.end());
    std::sort(matrix.begin(), matrix.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    stream << "each.matrix{" << matrix.size() << '}';
    for (const auto& [key, values] : matrix) {
      appendField(stream, "each.matrix.key", key);
      appendStringList(stream, "each.matrix.values", values);
    }
    appendField(stream, "each.as", task.each->as);
    appendField(stream, "each.index_variable", task.each->index_variable);
  } else {
    stream << "each=0;";
  }
  appendOptionalString(stream, "timeout", task.timeout);
  if (task.triggers) {
    stream << "triggers=1;";
    appendStringList(stream, "triggers.on_success", task.triggers->on_success);
    appendStringList(stream, "triggers.on_failure", task.triggers->on_failure);
    appendStringList(stream, "triggers.on_complete", task.triggers->on_complete);
  } else {
    stream << "triggers=0;";
  }
  appendOptionalString(stream, "working_dir", task.working_dir);
  appendField(stream, "silent", task.silent ? "1" : "0");
  appendStringList(stream, "sources", task.sources);
  appendStringList(stream, "generates", task.generates);
  appendOptionalString(stream, "script", task.script);

  if (std::holds_alternative<UsesParams>(task.specifics)) {
    const auto& params = std::get<UsesParams>(task.specifics);
    appendField(stream, "specifics", "uses");
    appendField(stream, "uses.path", params.path);
  } else if (std::holds_alternative<DynamicTasksParams>(task.specifics)) {
    const auto& params = std::get<DynamicTasksParams>(task.specifics);
    appendField(stream, "specifics", "dynamic_tasks");
    appendField(stream, "dynamic.items_variable", params.items_variable);
    appendField(stream, "dynamic.template.name", params.task_template.name);
    appendCommand(stream, "dynamic.template.command", params.task_template.command);
    appendOptionalString(stream, "dynamic.template.timeout", params.task_template.timeout);
    appendOptionalString(stream, "dynamic.template.when", params.task_template.when);
    appendStringList(stream, "dynamic.template.depends_on", params.task_template.depends_on);
    appendStringMap(stream, "dynamic.template.env", params.task_template.env);
  } else if (std::holds_alternative<OrchParams>(task.specifics)) {
    const auto& params = std::get<OrchParams>(task.specifics);
    appendField(stream, "specifics", "actions");
    appendOrchNode(stream, params.root);
  } else if (std::holds_alternative<ProgramParams>(task.specifics)) {
    const auto& params = std::get<ProgramParams>(task.specifics);
    appendField(stream, "specifics", "program");
    appendField(stream, "program.program", params.program);
    appendStringList(stream, "program.args", params.args);
    appendField(stream, "program.input", params.input);
    appendField(stream, "program.output_format",
                params.output_format == CommandOutputFormat::Json ? "json" : "text");
  } else if (std::holds_alternative<ServiceParams>(task.specifics)) {
    const auto& params = std::get<ServiceParams>(task.specifics);
    appendField(stream, "specifics", "service");
    appendField(stream, "service.operation", std::to_string(static_cast<int>(params.operation)));
    appendField(stream, "service.name", params.name);
    appendField(stream, "service.profile", params.profile);
    appendStringList(stream, "service.arguments", params.arguments);
    appendField(stream, "service.timeout_ms", std::to_string(params.timeout_ms));
    appendField(stream, "service.poll_interval_ms", std::to_string(params.poll_interval_ms));
  } else if (std::holds_alternative<ManagedProcessParams>(task.specifics)) {
    const auto& params = std::get<ManagedProcessParams>(task.specifics);
    appendField(stream, "specifics", "managed_process");
    appendField(stream, "managed_process.operation",
                std::to_string(static_cast<int>(params.operation)));
    appendField(stream, "managed_process.executable", params.executable);
    appendStringList(stream, "managed_process.arguments", params.arguments);
    appendField(stream, "managed_process.working_directory", params.working_directory);
    appendField(stream, "managed_process.identity.image_name", params.identity.image_name);
    appendField(stream, "managed_process.startup_timeout_ms",
                std::to_string(params.startup_timeout_ms));
    appendField(stream, "managed_process.stop_timeout_ms", std::to_string(params.stop_timeout_ms));
    appendField(stream, "managed_process.force_terminate", params.force_terminate ? "1" : "0");
  } else {
    appendField(stream, "specifics", "none");
  }

  return Praktor::Util::computeStringHash(stream.str());
}

void logTaskOutputs(const Task& task, const TaskResult& result) {
  if (task.silent || result.output_streamed_live) {
    return;
  }

  if (!result.stdout_data.empty()) {
    logd("Task '{}' stdout:\n{}", task.name, summarizeTaskOutputForLog(result.stdout_data));
  }

  if (!result.stderr_data.empty()) {
    const std::string summarized = summarizeTaskOutputForLog(result.stderr_data);
    if (result.success) {
      logw("Task '{}' stderr:\n{}", task.name, summarized);
    } else {
      loge("Task '{}' stderr:\n{}", task.name, summarized);
    }
  }
}

void printTaskTerminalStatus(const Task& task, std::string_view status) {
  if (Praktor::Logging::isVerboseEnabled()) {
    return;
  }
  if (status == "success") {
    Praktor::Logging::printTaskStatus(task.name, "SUCCESS");
  } else if (status == "failed") {
    Praktor::Logging::printTaskStatus(task.name, "FAILED");
  } else {
    Praktor::Logging::printTaskStatus(task.name, "SKIPPED");
  }
}

std::unordered_set<std::string> collectTriggerTargetNames(const std::vector<Task>& tasks) {
  std::unordered_set<std::string> names;
  for (const auto& task : tasks) {
    if (!task.triggers) {
      continue;
    }

    for (const auto& name : task.triggers->on_success) {
      names.insert(name);
    }
    for (const auto& name : task.triggers->on_failure) {
      names.insert(name);
    }
    for (const auto& name : task.triggers->on_complete) {
      names.insert(name);
    }
  }
  return names;
}

} // namespace

namespace Praktor::Execution::Internal {

std::string computeTaskActionHash(const Task& task) {
  return computeTaskActionHashImpl(task);
}

}  // namespace Praktor::Execution::Internal

WorkflowExecutor::WorkflowExecutor(DependencyGraph<Task> &graph,
                                   std::vector<Task> all_tasks,
                                   std::unordered_map<std::string, std::string> base_environment,
                                   size_t num_threads,
                                   bool schedule_trigger_tasks,
                                   size_t max_trigger_depth)
    : graph_(graph), base_environment_(std::move(base_environment)), max_concurrency_(std::max<size_t>(1, num_threads)),
      use_shared_pool_(num_threads > 1), all_tasks_(std::move(all_tasks)),
      schedule_trigger_tasks_(schedule_trigger_tasks) {

  trigger_executor_.setMaxTriggerDepth(max_trigger_depth);

  if (all_tasks_.empty()) {
    all_tasks_ = graph_.getNodes();
  }

  for (const auto& task : all_tasks_) {
    all_task_lookup_[task.name] = task;
  }

  if (!schedule_trigger_tasks_) {
    trigger_only_tasks_ = collectTriggerTargetNames(all_tasks_);
  }

  // Initialize executor registry
  // RunCommand removed: command: tasks are desugared to OrchParams at parse time
  // All command execution now flows through DeclarativeTreeExecutor
  executors_[TaskAction::Uses] =
      Praktor::Execution::createUsesExecutor(base_environment_, num_threads);
  executors_[TaskAction::Orch] =
      Praktor::Execution::createDeclarativeTreeExecutor(base_environment_);
  executors_[TaskAction::Program] =
      Praktor::Execution::createProgramExecutor(base_environment_);
  executors_[TaskAction::Download] =
      Praktor::Execution::createDownloadExecutor();
  executors_[TaskAction::Service] =
      Praktor::Execution::createServiceExecutor();
  executors_[TaskAction::ManagedProcess] =
      Praktor::Execution::createManagedProcessExecutor();

  // DynamicTasks executor needs a callback to execute generated subtasks
  auto dynamic_executor = Praktor::Execution::createDynamicTasksExecutor();
  auto *dt_ptr = static_cast<Praktor::Execution::DynamicTasksExecutor *>(dynamic_executor.get());
  dt_ptr->setSubTaskCallback([this](const Task &task, WorkflowContext &ctx) {
    return this->executeTask(task, ctx, std::nullopt);
  });
  dt_ptr->setTaskNameExistsCallback([this](std::string_view task_name) {
    return this->findTaskByName(task_name) != nullptr;
  });
  executors_[TaskAction::DynamicTasks] = std::move(dynamic_executor);
}

WorkflowExecutor::WorkflowExecutor(DependencyGraph<Task> &graph,
                                   std::unordered_map<std::string, std::string> base_environment,
                                   size_t num_threads,
                                   size_t max_trigger_depth)
    : WorkflowExecutor(graph, graph.getNodes(), std::move(base_environment), num_threads, false,
                       max_trigger_depth) {}

void WorkflowExecutor::execute(WorkflowContext &context, std::optional<std::string> alias) {
  auto nodes = graph_.getNodes();
  if (nodes.empty()) return;

  loadCache(nodes.front().source_path);

  ExecutionState state;
  initializeExecutionState(state, nodes);

  // Schedule initial ready tasks
  for (const auto &task : getReadyTasks(state)) {
    scheduleTask(task, context, state, alias);
  }

  // Wait for completion
  std::unique_lock<std::mutex> lock(execution_mutex_);
  execution_cv_.wait(lock, [&]() { return state.isFinished(); });

  if (state.hasPendingWork()) {
    execution_cv_.wait(lock, [&]() { return !state.hasPendingWork(); });
  }

  if (state.completed != state.total_tasks && !state.should_stop) {
    throw std::runtime_error("Workflow graph contains cycles or unreachable tasks");
  }

  context.setValue("workflow_status", state.workflow_failed ? "failed" : "success");
  saveCache();
}

bool WorkflowExecutor::isRegularlySchedulable(const Task& task) const {
  return schedule_trigger_tasks_ || trigger_only_tasks_.count(task.name) == 0;
}

const Task* WorkflowExecutor::findTaskByName(std::string_view task_name) const {
  auto it = all_task_lookup_.find(std::string(task_name));
  return it == all_task_lookup_.end() ? nullptr : &it->second;
}

void WorkflowExecutor::initializeExecutionState(ExecutionState &state, const std::vector<Task> &nodes) {
  state.total_tasks = nodes.size();
  for (const auto& task : nodes) {
    if (!isRegularlySchedulable(task)) {
      state.total_tasks--;
      continue;
    }

    int incoming = 0;
    for (const auto& candidate : nodes) {
      if (!isRegularlySchedulable(candidate)) {
        continue;
      }
      for (const auto& edge : graph_.getEdges(candidate)) {
        if (edge == task) {
          incoming++;
        }
      }
    }
    state.in_degree[task] = incoming;
  }
}

std::vector<Task> WorkflowExecutor::getReadyTasks(ExecutionState &state) {
  std::vector<Task> ready;
  std::lock_guard<std::mutex> lock(execution_mutex_);
  const size_t capacity =
      max_concurrency_ > state.active ? max_concurrency_ - state.active : 0;
  if (capacity == 0) {
    return ready;
  }

  for (const auto &[task, degree] : state.in_degree) {
    if (degree == 0 && state.scheduled.find(task.name) == state.scheduled.end()) {
      state.scheduled.insert(task.name);
      state.active++;
      ready.push_back(task);
      if (ready.size() >= capacity) {
        break;
      }
    }
  }
  return ready;
}

std::vector<Task> WorkflowExecutor::onTaskCompleted(ExecutionState &state, const Task &task, bool success) {
  std::vector<Task> ready;
  std::lock_guard<std::mutex> lock(execution_mutex_);

  state.active--;
  state.completed++;

  if (!success) {
    state.workflow_failed = true;
    state.should_stop = true;
  }

  if (!state.should_stop) {
    for (const auto &neighbor : graph_.getEdges(task)) {
      auto it = state.in_degree.find(neighbor);
      if (it != state.in_degree.end()) {
        it->second--;
      }
    }

    const size_t capacity =
        max_concurrency_ > state.active ? max_concurrency_ - state.active : 0;
    for (const auto &[candidate, degree] : state.in_degree) {
      if (ready.size() >= capacity) {
        break;
      }
      if (degree == 0 && state.scheduled.find(candidate.name) == state.scheduled.end()) {
        state.scheduled.insert(candidate.name);
        state.active++;
        ready.push_back(candidate);
      }
    }
  }

  execution_cv_.notify_all();
  return ready;
}

void WorkflowExecutor::mergeForkedContext(WorkflowContext& target, const WorkflowContext& child) {
  std::lock_guard<std::mutex> lock(execution_mutex_);
  target.mergeLocalValuesFrom(child);
}

void WorkflowExecutor::scheduleTask(const Task &task, WorkflowContext &context,
                                     ExecutionState &state, std::optional<std::string> alias) {
  auto runTask = [this, task, &context, &state, alias]() {
    WorkflowContext *ctx_ptr = &context;
    std::unique_ptr<WorkflowContext> task_context;

    if (use_shared_pool_) {
      task_context = context.fork();
      ctx_ptr = task_context.get();
    }

    bool success = executeTask(task, *ctx_ptr, alias);
    if (task_context) {
      mergeForkedContext(context, *task_context);
    }
    auto ready = onTaskCompleted(state, task, success);

    for (const auto &next : ready) {
      scheduleTask(next, context, state, alias);
    }
  };

  if (use_shared_pool_) {
    Praktor::SharedThreadPool::instance().enqueue(std::move(runTask));
  } else {
    runTask();
  }
}

void WorkflowExecutor::loadCache(const std::string &workflow_path) {
  if (workflow_path.empty())
    return;
  auto path = std::filesystem::path(workflow_path).parent_path() / ".praktor_cache";
  cache_file_ = path.string();

  if (!std::filesystem::exists(path))
    return;

  std::ifstream is(path);
  const std::string cache_contents((std::istreambuf_iterator<char>(is)),
                                   std::istreambuf_iterator<char>());
  WorkflowValue j = WorkflowValue::parse(cache_contents);
  for (auto const &item : j.array_range()) {
    std::string task_name = item["task"].as_string();
    TaskCacheState state;
    if (item.contains("action_hash")) {
      state.action_hash = item["action_hash"].as_string();
    }
    if (item.contains("sources")) {
      for (auto const &src : item["sources"].object_range()) {
        state.source_hashes[src.key()] = src.value().as_string();
      }
    }
    cache_[task_name] = std::move(state);
  }
}

void WorkflowExecutor::saveCache() {
  if (cache_file_.empty() || cache_.empty())
    return;

  WorkflowValue j = WorkflowValue::array();
  for (auto const &[name, state] : cache_) {
    WorkflowValue item;
    item["task"] = name;
    item["action_hash"] = state.action_hash;
    WorkflowValue sources = WorkflowValue::object();
    for (auto const &[path, hash] : state.source_hashes) {
      sources[path] = hash;
    }
    item["sources"] = std::move(sources);
    j.push_back(std::move(item));
  }

  std::ofstream os(cache_file_);
  os << j.pretty_string();
}

bool WorkflowExecutor::checkSkipTask(const Task &task, WorkflowContext &context) {
  if (!use_cache_ || (task.sources.empty() && task.generates.empty())) {
    return false;
  }

  auto it = cache_.find(task.name);
  if (it == cache_.end())
    return false;

  if (it->second.action_hash !=
      Praktor::Execution::Internal::computeTaskActionHash(task)) {
    return false;
  }

  // Check if generates exist
  for (const auto &gen : task.generates) {
    auto path = Praktor::util::resolveRelativePath(task.source_path, gen);
    if (!std::filesystem::exists(path))
      return false;
  }

  // Check if sources changed
  for (const auto &src : task.sources) {
    auto path = Praktor::util::resolveRelativePath(task.source_path, src);
    auto current_hash = Praktor::Util::computeFileHash(path);
    auto cached_it = it->second.source_hashes.find(path.string());
    if (cached_it == it->second.source_hashes.end() || cached_it->second != current_hash) {
      return false;
    }
  }

  logd("Skipping task '{}' (already up to date)", task.name);
  return true;
}

void WorkflowExecutor::updateTaskCache(const Task &task, WorkflowContext &context) {
  if (!use_cache_ || (task.sources.empty() && task.generates.empty())) {
    return;
  }

  TaskCacheState state;
  state.action_hash = Praktor::Execution::Internal::computeTaskActionHash(task);
  for (const auto &src : task.sources) {
    auto path = Praktor::util::resolveRelativePath(task.source_path, src);
    state.source_hashes[path.string()] = Praktor::Util::computeFileHash(path);
  }

  std::lock_guard<std::mutex> lock(execution_mutex_);
  cache_[task.name] = std::move(state);
}

bool WorkflowExecutor::executeTask(const Task &task, WorkflowContext &context,
                                   std::optional<std::string> alias, bool ignore_when,
                                   size_t trigger_depth) {
  // Mark as running in registry to allow setOutput calls
  setTaskExecutionStatus(task, context, alias, "running");

  bool overall_success = false;
  std::string final_status = "failed";
  std::optional<TaskExecutionOutcome> trigger_outcome;

  if (task.each && task.each->enabled()) {
    auto combinations = generateEachCombinations(*task.each);
    logd("Executing task '{}' for {} combinations", task.name, combinations.size());

    bool all_success = true;
    bool any_executed = false;
    WorkflowValue iteration_results = WorkflowValue::array();
    WorkflowValue last_executed_outputs = WorkflowValue::object();
    for (size_t i = 0; i < combinations.size(); ++i) {
      clearTaskExecutionOutputs(task, context, alias);
      auto child_context = context.fork();
      child_context->setValue(task.each->as, combinations[i]);
      if (!task.each->index_variable.empty()) {
        child_context->setValue(task.each->index_variable, std::to_string(i));
      }

      auto outcome = executeTaskInternal(task, *child_context, alias, ignore_when, false);
      child_context->unsetValue(task.each->as);
      if (!task.each->index_variable.empty()) {
        child_context->unsetValue(task.each->index_variable);
      }
      context.mergeLocalValuesFrom(*child_context);
      if (!outcome.success) {
        all_success = false;
        if (!trigger_outcome.has_value()) {
          trigger_outcome = outcome;
        }
      }
      if (outcome.status != "skipped") {
        any_executed = true;
        last_executed_outputs = getTaskOutputsSnapshot(task, context);
      }

      WorkflowValue iteration = WorkflowValue::object();
      iteration["index"] = static_cast<int64_t>(i);
      iteration["item"] = combinations[i];
      iteration["status"] = outcome.status;
      iteration["outputs"] = getTaskOutputsSnapshot(task, context);
      iteration_results.push_back(std::move(iteration));
    }

    clearTaskExecutionOutputs(task, context, alias);
    WorkflowValue aggregated_outputs = WorkflowValue::object();
    if (last_executed_outputs.is_object()) {
      for (const auto& item : last_executed_outputs.object_range()) {
        aggregated_outputs[item.key()] = item.value();
      }
    }
    aggregated_outputs["iterations"] = std::move(iteration_results);
    mergeTaskExecutionOutputs(task, context, alias, aggregated_outputs);

    overall_success = all_success;
    if (!any_executed && overall_success) {
      final_status = "skipped";
    } else {
      final_status = overall_success ? "success" : "failed";
    }

    // Report one terminal status for the whole each/matrix task instead of
    // one line per iteration.
    printTaskTerminalStatus(task, final_status);
  } else {
    auto outcome = executeTaskInternal(task, context, alias, ignore_when);
    overall_success = outcome.success;
    final_status = outcome.status;
    trigger_outcome = outcome;
  }

  // `each` is one logical task: expose the aggregate status, then fire triggers once.
  const TaskExecutionOutcome failure = trigger_outcome.value_or(TaskExecutionOutcome{});
  setTaskExecutionStatus(task, context, alias, final_status, failure.result.error_message);

  bool has_failure_context = false;
  if (!overall_success) {
    context.setFailureContext(buildFailureContext(task, failure.result, context, executors_));
    has_failure_context = true;
  }

  const bool triggers_success = executeTriggers(task, overall_success, context, trigger_depth);
  if (has_failure_context) {
    context.clearFailureContext();
  }

  return overall_success && triggers_success;
}

void WorkflowExecutor::setTaskExecutionStatus(const Task& task, WorkflowContext& context,
                                              std::optional<std::string> alias,
                                              std::string_view status,
                                              const std::string& error_message) const {
  const auto apply_status = [&](const std::string& task_name) {
    if (status == "success") {
      context.addCompletedTask(task_name);
    } else if (status == "failed") {
      context.addFailedTask(task_name, error_message.empty() ? "Task failed" : error_message);
    } else {
      context.setTaskStatus(task_name, std::string(status));
    }
  };

  apply_status(task.name);
  if (alias && alias.value() != task.name) {
    apply_status(alias.value());
  }
}

void WorkflowExecutor::clearTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                                 std::optional<std::string> alias) const {
  context.clearTaskOutputs(task.name);
  if (alias && alias.value() != task.name) {
    context.clearTaskOutputs(alias.value());
  }
}

void WorkflowExecutor::mergeTaskExecutionOutputs(const Task& task, WorkflowContext& context,
                                                 std::optional<std::string> alias,
                                                 const WorkflowValue& outputs) const {
  if (!outputs.is_object()) {
    return;
  }

  context.mergeTaskOutputs(task.name, outputs);
  if (alias && alias.value() != task.name) {
    context.mergeTaskOutputs(alias.value(), outputs);
  }
}

WorkflowValue WorkflowExecutor::getTaskOutputsSnapshot(const Task& task,
                                                       const WorkflowContext& context) const {
  WorkflowValue outputs = context.getValueByPath("tasks." + task.name + ".outputs");
  return outputs.is_object() ? outputs : WorkflowValue::object();
}

WorkflowExecutor::TaskExecutionOutcome WorkflowExecutor::executeTaskInternal(
    const Task &task, WorkflowContext &context, std::optional<std::string> alias, bool ignore_when,
    bool report_terminal_status) {
  if (!Praktor::Logging::isVerboseEnabled()) {
    // Report RUNNING once per task per run; a task can execute many times
    // (each/matrix iterations, trigger re-entry) and repeating the same
    // RUNNING line floods logs. Terminal statuses are still emitted for
    // every execution.
    const bool report_running = [&] {
      std::lock_guard<std::mutex> lock(status_log_mutex_);
      return running_reported_.insert(task.name).second;
    }();
    if (report_running) {
      Praktor::Logging::printTaskStatus(task.name, "RUNNING");
    }
  }
  logd("Executing task: {} (action={}, ignore_when={})", task.name, static_cast<int>(task.action),
       ignore_when);
  context.pushTaskScope(task.name, alias);
  ScopedVariables scoped_vars(context, task.vars);

  TaskExecutionOutcome outcome;
  outcome.status = "skipped";
  TaskResult last_result;
  bool has_task_result = false;

  try {
    if (!ignore_when && !evaluateWhen(task, context)) {
      outcome.success = true;
      outcome.status = "skipped";
    } else if (checkSkipTask(task, context)) {
      outcome.success = true;
      outcome.status = "skipped";
    } else {
      TaskResult result;
      result.success = true;

      // Run task action (command-desugared BT / explicit BT / uses / dynamic_tasks) if present
      if (task.action != TaskAction::None) {
        auto it = executors_.find(task.action);
        if (it == executors_.end()) {
          throw std::runtime_error("No executor registered for task action");
        }
        result = it->second->execute(task, context);
        has_task_result = true;
      }

      // Run script if present (as action for script-only tasks, or post-processing)
      if (result.success && task.script) {
        auto script_result = Praktor::Script::execute(*task.script, context, task.source_path);
        if (!script_result.success) {
          result.success = false;
          result.error_message = script_result.error_message;
          loge("Script failed for task '{}': {}", task.name, script_result.error_message);
          for (const auto& err : script_result.errors) {
            loge("  line {}: {}", err.line, err.message);
          }
        }
      }

      // Script-only task with no action and no script — nothing to do
      if (task.action == TaskAction::None && !task.script) {
        throw std::runtime_error("Task '" + task.name + "' has no action and no script");
      }

      last_result = result;

      if (result.success) {
        outcome.success = true;
        outcome.status = "success";
        updateTaskCache(task, context);
      }

      if (!outcome.success) {
        outcome.status = "failed";
      }
    }
  } catch (const std::exception &e) {
    loge("Task '{}' failed: {}", task.name, e.what());
    outcome.success = false;
    outcome.status = "failed";
    last_result.success = false;
    last_result.error_message = e.what();
  }

  if (has_task_result) {
    logTaskOutputs(task, last_result);
  }

  if (report_terminal_status) {
    printTaskTerminalStatus(task, outcome.status);
  }

  outcome.result = std::move(last_result);
  outcome.has_task_result = has_task_result;
  context.popTaskScope();

  return outcome;
}

bool WorkflowExecutor::evaluateWhen(const Task &task, WorkflowContext &context) const {
  if (!task.when || task.when->empty()) {
    return true;
  }

  return Praktor::Expressions::ExpressionEvaluator{}.evaluateAsBool(*task.when, context);
}

std::unordered_map<std::string, std::string> WorkflowExecutor::buildTaskEnvironment(
    const Task &task, const std::unordered_map<std::string, std::string> &inherited_env,
    WorkflowContext &context) const {
  EnvMap env = inherited_env;

  for (const auto &file : task.dot_env) {
    std::filesystem::path env_path = Praktor::util::resolveRelativePath(task.source_path, file);
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

bool WorkflowExecutor::executeTriggers(const Task &task, bool success, WorkflowContext &context,
                                       size_t trigger_depth) {
  if (!task.triggers || task.triggers->empty()) {
    return true;
  }

  logd("Executing triggers for task '{}' (success={})", task.name, success);

  return trigger_executor_.executeTriggers(
      task, success, context, all_tasks_,
      [this, &context](const Task &t, size_t depth) {
        return this->executeTriggeredTask(t, context, depth);
      },
      trigger_depth);
}

bool WorkflowExecutor::executeTriggeredTask(const Task& task, WorkflowContext& context,
                                            size_t trigger_depth) {
  std::unordered_set<std::string> active_stack;
  return executeTriggeredTask(task, context, active_stack, false, trigger_depth);
}

bool WorkflowExecutor::executeTriggeredTask(const Task& task, WorkflowContext& context,
                                           std::unordered_set<std::string>& active_stack,
                                           bool dependency_only, size_t trigger_depth) {
  const std::string status = context.getTaskStatus(task.name);
  if (dependency_only &&
      (status == "success" || status == "skipped" || status == "failed" || status == "running")) {
    return true;
  }

  if (!dependency_only && status == "running") {
    return true;
  }

  if (!active_stack.insert(task.name).second) {
    throw std::runtime_error("Trigger dependency cycle detected at task '" + task.name + "'");
  }

  for (const auto& dependency_name : task.depends_on) {
    const Task* dependency = findTaskByName(dependency_name);
    if (!dependency) {
      active_stack.erase(task.name);
      throw std::runtime_error("Trigger task '" + task.name +
                               "' depends on unknown task '" + dependency_name + "'");
    }

    if (!executeTriggeredTask(*dependency, context, active_stack, true, trigger_depth)) {
      active_stack.erase(task.name);
      return false;
    }
  }

  active_stack.erase(task.name);
  return executeTask(task, context, std::nullopt, true, trigger_depth);
}
