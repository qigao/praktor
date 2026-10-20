#include "tree_executor_session.hpp"

#include <json_parser.h>
#include <salts/process.h>
#include <praktor/shell/shell_executor.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace actions::c11 {
namespace {

salts_bt_node_status toSaltsStatus(actions::NodeStatus status) {
  switch (status) {
  case actions::NodeStatus::SUCCESS: return SALTS_BT_SUCCESS;
  case actions::NodeStatus::FAILURE: return SALTS_BT_FAILURE;
  case actions::NodeStatus::RUNNING: return SALTS_BT_RUNNING;
  }
  return SALTS_BT_FAILURE;
}

std::string resolveStringValue(const actions::Value& value,
                               const actions::Blackboard& blackboard) {
  if (!std::holds_alternative<std::string>(value)) {
    if (std::holds_alternative<double>(value)) {
      const double number = std::get<double>(value);
      if (!std::isfinite(number)) {
        throw std::invalid_argument("numeric value is not finite");
      }
      return std::to_string(number);
    }
    return std::get<bool>(value) ? "true" : "false";
  }

  const std::string& raw = std::get<std::string>(value);
  if (raw.size() >= 3u && raw.front() == '{' && raw.back() == '}') {
    return blackboard.get(raw.substr(1u, raw.size() - 2u));
  }
  return raw;
}

bool shellBooleanValue(const actions::Value& value,
                       const actions::Blackboard& blackboard) {
  if (std::holds_alternative<bool>(value)) return std::get<bool>(value);
  if (std::holds_alternative<double>(value)) {
    throw std::invalid_argument("Shell stream_output must be a boolean");
  }
  std::string raw = resolveStringValue(value, blackboard);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  if (raw == "true") return true;
  if (raw == "false") return false;
  throw std::invalid_argument("Shell stream_output must be true or false");
}

bool conditionValue(const actions::Value& value,
                    const actions::Blackboard& blackboard) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  if (std::holds_alternative<double>(value)) {
    return std::fpclassify(std::get<double>(value)) != FP_ZERO;
  }

  std::string raw = resolveStringValue(value, blackboard);
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return !(raw.empty() || raw == "0" || raw == "false" || raw == "no" ||
           raw == "off" || raw == "null");
}

long long parseInteger(const std::string& raw) {
  const double lower = static_cast<double>((std::numeric_limits<int>::min)());
  const double upper = static_cast<double>((std::numeric_limits<int>::max)());
  try {
    size_t consumed = 0u;
    const long long integer = std::stoll(raw, &consumed, 10);
    if (consumed == raw.size() &&
        integer >= (std::numeric_limits<int>::min)() &&
        integer <= (std::numeric_limits<int>::max)()) return integer;
  } catch (const std::exception&) {
    // Try the legacy executor's integral floating-point string form below.
  }

  try {
    size_t consumed = 0u;
    const double number = std::stod(raw, &consumed);
    const double fractional = number - std::trunc(number);
    if (consumed != raw.size() || !std::isfinite(number) ||
        std::abs(fractional) > 0.0 || number < lower || number > upper) {
      throw std::invalid_argument("not an in-range integral number");
    }
    return static_cast<long long>(number);
  } catch (const std::exception&) {
    throw std::invalid_argument("switch variable is not a 32-bit integer: " + raw);
  }
}

int resolveSwitchValue(const actions::Node& source,
                       const actions::Blackboard& blackboard) {
  const auto it = source.params.find("variable");
  if (it == source.params.end()) {
    throw std::invalid_argument("Switch requires parameter 'variable'");
  }

  if (std::holds_alternative<std::string>(it->second)) {
    const std::string& original = std::get<std::string>(it->second);
    std::string raw = resolveStringValue(it->second, blackboard);
    // The legacy Executor accepts a bare Blackboard key as well as {key}.
    if (raw == original && blackboard.has(raw)) {
      raw = blackboard.get(raw);
    }
    return static_cast<int>(parseInteger(raw));
  }

  if (std::holds_alternative<double>(it->second)) {
    const double number = std::get<double>(it->second);
    const double fractional = number - std::trunc(number);
    if (!std::isfinite(number) || std::abs(fractional) > 0.0 ||
        number < static_cast<double>((std::numeric_limits<int>::min)()) ||
        number > static_cast<double>((std::numeric_limits<int>::max)())) {
      throw std::invalid_argument("Switch variable must resolve to a 32-bit integer");
    }
    return static_cast<int>(number);
  }

  throw std::invalid_argument("Switch variable must be an integer, not a boolean");
}


struct JsonValueDeleter {
  void operator()(json_value_t* value) const noexcept {
    if (value != nullptr) json_free(value);
  }
};

struct JsonTextDeleter {
  void operator()(char* value) const noexcept {
    if (value != nullptr) json_serialize_free(value);
  }
};

using OwnedJsonValue = std::unique_ptr<json_value_t, JsonValueDeleter>;
using OwnedJsonText = std::unique_ptr<char, JsonTextDeleter>;

std::string serializeJsonValue(const json_value_t* value) {
  size_t length = 0u;
  OwnedJsonText serialized(json_serialize(value, &length));
  if (!serialized) throw std::runtime_error("JSON serialization failed");
  return std::string(serialized.get(), length);
}

bool consumeJsonArrayFront(std::string_view input, std::string& item,
                           std::string& remaining, bool& has_more,
                           bool& queue_is_empty) {
  json_value_t* parsed = json_parse(input.data(), input.size());
  if (parsed == nullptr) {
    throw std::invalid_argument("ConsumeQueue payload is not valid JSON");
  }
  OwnedJsonValue document(parsed);
  if (json_type(document.get()) != JSON_ARRAY) {
    throw std::invalid_argument("ConsumeQueue payload must be a JSON array");
  }
  if (json_array_size(document.get()) == 0u) {
    queue_is_empty = true;
    has_more = false;
    remaining = "[]";
    return false;
  }

  item = serializeJsonValue(json_array_get(document.get(), 0u));
  OwnedJsonValue tail(json_create_array());
  if (!tail) throw std::bad_alloc();

  const size_t count = json_array_size(document.get());
  for (size_t index = 1u; index < count; ++index) {
    OwnedJsonValue child(json_clone(json_array_get(document.get(), index)));
    if (!child || !json_array_add_checked(tail.get(), child.get())) {
      throw std::runtime_error("failed to build remaining JSON queue");
    }
    (void)child.release();
  }

  has_more = json_array_size(tail.get()) != 0u;
  remaining = serializeJsonValue(tail.get());
  queue_is_empty = false;
  return true;
}

std::string resolveSubTreeName(const actions::Node& node,
                               const actions::Blackboard& blackboard) {
  const auto it = node.params.find("tree");
  if (it == node.params.end()) {
    throw std::invalid_argument("SubTree requires 'tree' parameter");
  }
  if (std::holds_alternative<std::string>(it->second)) {
    const std::string& raw = std::get<std::string>(it->second);
    if (raw.size() >= 3u && raw.front() == '{' && raw.back() == '}') {
      return blackboard.get(raw.substr(1u, raw.size() - 2u));
    }
    return raw;
  }
  if (std::holds_alternative<double>(it->second)) {
    return std::to_string(std::get<double>(it->second));
  }
  return std::get<bool>(it->second) ? "true" : "false";
}

actions::Node expandSubTreeNode(
    const actions::Node& source,
    const actions::Executor& executor,
    const actions::Blackboard& blackboard,
    const salts_bt_tree_config& limits,
    size_t source_depth,
    size_t subtree_depth,
    size_t& expanded_node_count,
    std::vector<std::string>& active_subtrees) {
  if (source_depth > limits.max_depth) {
    throw std::length_error("SubTree expansion exceeds the configured tree depth");
  }

  if (source.id == "SubTree") {
    if (subtree_depth >= limits.max_depth) {
      throw std::length_error("SubTree expansion exceeds the configured nesting depth");
    }
    const std::string name = resolveSubTreeName(source, blackboard);
    if (std::find(active_subtrees.begin(), active_subtrees.end(), name) !=
        active_subtrees.end()) {
      throw std::invalid_argument("recursive SubTree reference: " + name);
    }
    const actions::Tree* registered = executor.getTree(name);
    if (registered == nullptr) {
      throw std::invalid_argument("SubTree not found: " + name);
    }

    active_subtrees.push_back(name);
    try {
      actions::Node expanded = expandSubTreeNode(
          registered->root, executor, blackboard, limits, source_depth,
          subtree_depth + 1u, expanded_node_count, active_subtrees);
      active_subtrees.pop_back();
      return expanded;
    } catch (...) {
      active_subtrees.pop_back();
      throw;
    }
  }

  if (expanded_node_count >= limits.max_nodes) {
    throw std::length_error("SubTree expansion exceeds the configured node count");
  }
  ++expanded_node_count;

  actions::Node expanded = source;
  expanded.children.clear();
  for (const auto& child : source.children) {
    expanded.children.push_back(expandSubTreeNode(
        child, executor, blackboard, limits, source_depth + 1u,
        subtree_depth, expanded_node_count, active_subtrees));
  }
  return expanded;
}

actions::Tree expandSubTrees(const actions::Tree& source,
                             const actions::Executor& executor,
                             const actions::Blackboard& blackboard,
                             const salts_bt_tree_config& limits) {
  if (limits.max_nodes == 0u || limits.max_nodes > UINT32_MAX ||
      limits.max_depth == 0u || limits.max_depth > SALTS_BT_MAX_DEPTH ||
      limits.max_control_iterations == 0u ||
      limits.max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS) {
    throw std::invalid_argument("invalid C11 behavior-tree limits");
  }

  actions::Tree expanded;
  expanded.name = source.name;
  size_t expanded_node_count = 0u;
  std::vector<std::string> active_subtrees;
  expanded.root = expandSubTreeNode(source.root, executor, blackboard, limits,
                                    1u, 0u, expanded_node_count, active_subtrees);
  return expanded;
}

} // namespace

struct TreeExecutorSession::Impl {
  actions::Tree tree;
  TreePlan compiled_plan;
  actions::Executor& executor;
  actions::Blackboard& blackboard;
  actions::ExecutionContext& execution_context;
  praktor_c11_bt_host hooks{};
  praktor_c11_bt_host runtime_host{};
  praktor_c11_bt_tree runtime{};
  struct PipelineState {
    bool initialized = false;
    std::string data;
    std::string output_key = "pipeline_result";
  };
  struct EntryUpdatedState {
    bool observed = false;
    std::string value;
  };
  struct QueueState {
    bool active = false;
    bool has_more = false;
  };
  enum class LeafOwner : unsigned char {
    None,
    HostHook,
    Executor,
    SaltsProcess
  };
  struct ShellState {
    Impl* owner = nullptr;
    size_t node_index = 0u;
    cflow_process process{};
    bool started = false;
    bool close_requested = false;
    bool stdin_write_pending = false;
    bool stdout_read_pending = false;
    bool stderr_read_pending = false;
    bool stdin_closed = false;
    bool stdout_eof = false;
    bool stderr_eof = false;
    bool process_terminal = false;
    bool io_failed = false;
    bool stream_output = true;
    uint64_t stdin_lease = 1u;
    uint64_t stdout_lease = 2u;
    uint64_t stderr_lease = 3u;
    size_t input_offset = 0u;
    std::array<char, 4096> stdin_buffer{};
    std::array<char, 4096> stdout_buffer{};
    std::array<char, 4096> stderr_buffer{};
    std::string command;
    std::string input;
    std::string working_dir;
    std::string output_key = "shell_output";
    std::string stderr_key = "shell_stderr";
    std::string exit_code_key = "shell_exit_code";
    std::string stdout_data;
    std::string stderr_data;
    std::string stdout_pending_line;
    std::string stderr_pending_line;
    std::string failure_reason;
    cmeta_process_result_t result{};
  };

  std::vector<LeafOwner> leaf_owners;
  std::vector<ShellState> shell_states;
  std::vector<PipelineState> pipeline_states;
  std::vector<EntryUpdatedState> entry_updated_states;
  std::vector<QueueState> queue_states;
  std::string last_error;

  Impl(const actions::Tree& source_tree,
       actions::Executor& source_executor,
       actions::Blackboard& source_blackboard,
       actions::ExecutionContext& source_execution_context,
       const salts_bt_tree_config& limits,
       const praktor_c11_bt_host* supplied_hooks)
      : tree(expandSubTrees(source_tree, source_executor, source_blackboard, limits)),
        compiled_plan(compileTreePlan(tree, source_blackboard, limits)),
        executor(source_executor),
        blackboard(source_blackboard),
        execution_context(source_execution_context) {
    if (supplied_hooks != nullptr) {
      hooks = *supplied_hooks;
    }

    validateHostContracts();
    leaf_owners.assign(compiled_plan.nodes.size(), LeafOwner::None);
    shell_states.resize(compiled_plan.nodes.size());
    pipeline_states.resize(compiled_plan.nodes.size());
    entry_updated_states.resize(compiled_plan.nodes.size());
    queue_states.resize(compiled_plan.nodes.size());
    for (size_t index = 0u; index < shell_states.size(); ++index) {
      shell_states[index].owner = this;
      shell_states[index].node_index = index;
    }

    runtime_host.tick_leaf = &onTickLeaf;
    runtime_host.halt_leaf = &onHaltLeaf;
    runtime_host.condition = &onCondition;
    runtime_host.select_child = &onSelectChild;
    runtime_host.parallel_enter = &onParallelEnter;
    runtime_host.parallel_step = &onParallelStep;
    runtime_host.parallel_join = &onParallelJoin;
    runtime_host.parallel_finish = &onParallelFinish;
    runtime_host.parallel_abort = &onParallelAbort;
    runtime_host.pipeline_prepare = &onPipelinePrepare;
    runtime_host.pipeline_complete = &onPipelineComplete;
    runtime_host.pipeline_abort = &onPipelineAbort;
    runtime_host.entry_changed = &onEntryChanged;
    runtime_host.queue_begin = &onQueueBegin;
    runtime_host.queue_finish = &onQueueFinish;
    runtime_host.queue_abort = &onQueueAbort;
    runtime_host.user = this;

    const int status = praktor_c11_bt_tree_init(
        &runtime, &limits, compiled_plan.nodes.data(),
        compiled_plan.nodes.size(), &runtime_host);
    if (status != SALTS_OK) {
      throw std::runtime_error("C11 behavior tree initialization failed with status " +
                               std::to_string(status));
    }
  }

  ~Impl() {
    if (runtime.impl != nullptr) {
      (void)praktor_c11_bt_tree_destroy(&runtime);
    }
    for (auto& shell : shell_states) {
      if (shell.process.impl != nullptr) {
        (void)closeShellProcess(shell, true);
      }
    }
  }

  const actions::Node* nodeAt(size_t index) const noexcept {
    if (index >= compiled_plan.source_nodes.size()) return nullptr;
    return compiled_plan.source_nodes[index];
  }

  void rememberCurrentException(const char* where) noexcept {
    try {
      throw;
    } catch (const std::exception& error) {
      last_error = std::string(where) + ": " + error.what();
    } catch (...) {
      last_error = std::string(where) + ": unknown C++ exception";
    }
  }

  void validateHostContracts() const {
    for (size_t i = 0u; i < compiled_plan.nodes.size(); ++i) {
      const auto& desc = compiled_plan.nodes[i];
      const auto* source = nodeAt(i);
      if (source == nullptr) {
        throw std::invalid_argument("TreePlan has no source AST node");
      }

      if (hooks.tick_leaf != nullptr && hooks.halt_leaf == nullptr) {
        throw std::invalid_argument(
            "A custom leaf tick hook requires a halt hook to define cancellation cleanup");
      }
      if (desc.kind == SALTS_BT_NODE_PARALLEL && desc.parallel_hooks &&
          (hooks.parallel_enter == nullptr || hooks.parallel_step == nullptr ||
           hooks.parallel_join == nullptr || hooks.parallel_finish == nullptr ||
           hooks.parallel_abort == nullptr)) {
        throw std::invalid_argument(
            "C11 Parallel requires enter/step/join/finish/abort hooks");
      }
      if (desc.kind == SALTS_BT_NODE_PIPELINE_SEQUENCE) {
        const bool has_prepare = hooks.pipeline_prepare != nullptr;
        const bool has_complete = hooks.pipeline_complete != nullptr;
        const bool has_abort = hooks.pipeline_abort != nullptr;
        const bool has_custom_pipeline = has_prepare || has_complete || has_abort;
        if (has_custom_pipeline && (!has_prepare || !has_complete || !has_abort)) {
          throw std::invalid_argument(
              "Custom PipelineSequence hooks must provide prepare/complete/abort together");
        }
      }
      if (desc.kind == SALTS_BT_NODE_CONSUME_QUEUE) {
        const bool has_begin = hooks.queue_begin != nullptr;
        const bool has_finish = hooks.queue_finish != nullptr;
        const bool has_abort = hooks.queue_abort != nullptr;
        const bool has_custom_queue = has_begin || has_finish || has_abort;
        if (has_custom_queue && (!has_begin || !has_finish || !has_abort)) {
          throw std::invalid_argument(
              "Custom ConsumeQueue hooks must provide begin/finish/abort together");
        }
      }
    }
  }

  static void emitStreamBytes(std::string& pending_line,
                              const char* bytes, size_t length,
                              bool enabled) {
    if (!enabled) return;
    for (size_t offset = 0u; offset < length; ++offset) {
      const char ch = bytes[offset];
      if (ch == '\r') continue;
      if (ch == '\n') {
        Praktor::Shell::ShellExecutor::emitStreamLine(pending_line);
        pending_line.clear();
      } else {
        pending_line.push_back(ch);
      }
    }
  }

  static void flushStreamLine(std::string& pending_line, bool enabled) {
    if (!enabled || pending_line.empty()) return;
    Praktor::Shell::ShellExecutor::emitStreamLine(pending_line);
    pending_line.clear();
  }

  static void onProcessCompletion(void* user, cflow_io_request_id,
                                  cflow_io_lease_id, cflow_process_stream stream,
                                  const cflow_io_completion* completion) noexcept {
    auto* state = static_cast<ShellState*>(user);
    if (state == nullptr || completion == nullptr) return;
    try {
      if (state->close_requested) {
        if (stream == CFLOW_PROCESS_STDOUT) {
          state->stdout_read_pending = false;
          if (completion->kind == CFLOW_IO_COMPLETION_EOF ||
              completion->kind == CFLOW_IO_COMPLETION_CANCELLED ||
              completion->kind == CFLOW_IO_COMPLETION_FAILED)
            state->stdout_eof = true;
        } else if (stream == CFLOW_PROCESS_STDERR) {
          state->stderr_read_pending = false;
          if (completion->kind == CFLOW_IO_COMPLETION_EOF ||
              completion->kind == CFLOW_IO_COMPLETION_CANCELLED ||
              completion->kind == CFLOW_IO_COMPLETION_FAILED)
            state->stderr_eof = true;
        } else {
          state->stdin_write_pending = false;
        }
        return;
      }

      if (stream == CFLOW_PROCESS_STDOUT || stream == CFLOW_PROCESS_STDERR) {
        const bool is_stdout = stream == CFLOW_PROCESS_STDOUT;
        auto& pending = is_stdout ? state->stdout_read_pending
                                  : state->stderr_read_pending;
        auto& eof = is_stdout ? state->stdout_eof : state->stderr_eof;
        auto& buffer = is_stdout ? state->stdout_buffer : state->stderr_buffer;
        auto& output = is_stdout ? state->stdout_data : state->stderr_data;
        auto& pending_line = is_stdout ? state->stdout_pending_line
                                       : state->stderr_pending_line;
        pending = false;
        if (completion->kind == CFLOW_IO_COMPLETION_OK) {
          if (completion->bytes > buffer.size() ||
              state->stdout_data.size() + state->stderr_data.size() >
                  SALTS_PROCESS_DEFAULT_MAX_OUTPUT_BYTES ||
              completion->bytes > SALTS_PROCESS_DEFAULT_MAX_OUTPUT_BYTES -
                  (state->stdout_data.size() + state->stderr_data.size())) {
            state->io_failed = true;
            state->failure_reason = "Shell output exceeded the bounded capture limit";
            return;
          }
          output.append(buffer.data(), completion->bytes);
          emitStreamBytes(pending_line, buffer.data(), completion->bytes,
                          state->stream_output);
        } else if (completion->kind == CFLOW_IO_COMPLETION_EOF) {
          eof = true;
        } else {
          eof = true;
          state->io_failed = true;
          state->failure_reason = "Shell stream I/O failed (error " +
                                  std::to_string(completion->error) + ")";
        }
        return;
      }

      state->stdin_write_pending = false;
      if (completion->kind == CFLOW_IO_COMPLETION_OK) {
        if (completion->bytes == 0u ||
            completion->bytes > state->input.size() - state->input_offset) {
          state->io_failed = true;
          state->failure_reason = "Shell stdin write made invalid progress";
          return;
        }
        state->input_offset += completion->bytes;
      } else {
        state->io_failed = true;
        state->failure_reason = "Shell stdin I/O failed (error " +
                                std::to_string(completion->error) + ")";
      }
    } catch (...) {
      state->io_failed = true;
      state->failure_reason = "Exception while processing a Salts::Process completion";
    }
  }

  void clearShellState(ShellState& state) noexcept {
    // Never erase the callback owner while the C process still owns leases or
    // teardown has failed. The shell session remains reachable for retry.
    if (state.process.impl != nullptr) return;
    Impl* owner = state.owner;
    const size_t node_index = state.node_index;
    state = ShellState{};
    state.owner = owner;
    state.node_index = node_index;
  }

  void finalizeShellState(ShellState& state) noexcept {
    const size_t index = state.node_index;
    if (state.process.impl == nullptr) {
      clearShellState(state);
      if (index < leaf_owners.size()) leaf_owners[index] = LeafOwner::None;
    } else if (index < leaf_owners.size()) {
      leaf_owners[index] = LeafOwner::SaltsProcess;
    }
  }

  int closeShellProcess(ShellState& state, bool clear_state) noexcept {
    int result = SALTS_OK;
    if (state.process.impl != nullptr) {
      const int close_status = cflow_process_close(&state.process);
      state.close_requested = true;
      if (close_status != SALTS_OK && close_status != SALTS_EBUSY) {
        state.io_failed = true;
        if (state.failure_reason.empty()) {
          state.failure_reason = "Salts::Process close failed with status " +
                                 std::to_string(close_status);
        }
      }

      // close() requests native termination and cancellation. Keep driving until
      // every admitted I/O lease has reached its terminal callback and the
      // process adapter is quiescent; destroy must never race completion leases.
      while (!cflow_process_is_quiescent(&state.process)) {
        size_t progressed = 0u;
        const int drive_status =
            cflow_process_run_ready(&state.process, 64u, &progressed);
        if (drive_status != SALTS_OK && drive_status != SALTS_EBUSY) {
          state.io_failed = true;
          if (state.failure_reason.empty()) {
            state.failure_reason = "Salts::Process drain failed with status " +
                                   std::to_string(drive_status);
          }
        }
        if (progressed == 0u)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const int destroy_status = cflow_process_destroy(&state.process);
      if (destroy_status != SALTS_OK) {
        state.io_failed = true;
        if (state.failure_reason.empty()) {
          state.failure_reason = "Salts::Process destroy failed with status " +
                                 std::to_string(destroy_status);
        }
      }
    }

    if (state.io_failed) result = SALTS_EIO;
    const std::string failure = state.failure_reason;
    if (clear_state) finalizeShellState(state);
    else if (state.process.impl != nullptr &&
             state.node_index < leaf_owners.size())
      leaf_owners[state.node_index] = LeafOwner::SaltsProcess;
    if (result != SALTS_OK && last_error.empty()) {
      last_error = failure.empty() ? "Salts::Process cleanup failed" : failure;
    }
    return result;
  }

  void publishShellResult(ShellState& state, int exit_code, bool success) {
    blackboard.set(state.output_key, state.stdout_data);
    blackboard.set(state.stderr_key, state.stderr_data);
    blackboard.set(state.exit_code_key, std::to_string(exit_code));
    if (!success) {
      // Legacy Executor always publishes captured stderr here, even when the
      // process fails with no stderr output. Keep diagnostics in lastError().
      blackboard.set("shell_error", state.stderr_data);
    }
  }

  int shellSubmitRead(ShellState& state, bool is_stdout) noexcept {
    if (state.close_requested ||
        (is_stdout ? state.stdout_eof : state.stderr_eof) ||
        (is_stdout ? state.stdout_read_pending : state.stderr_read_pending))
      return SALTS_OK;

    auto result = is_stdout
        ? cflow_process_try_read_stdout(&state.process, state.stdout_lease,
                                        state.stdout_buffer.data(), state.stdout_buffer.size())
        : cflow_process_try_read_stderr(&state.process, state.stderr_lease,
                                        state.stderr_buffer.data(), state.stderr_buffer.size());
    if (result.status == CFLOW_PROCESS_SUBMIT_ACCEPTED) {
      if (is_stdout) state.stdout_read_pending = true;
      else state.stderr_read_pending = true;
      return SALTS_OK;
    }
    if (result.status == CFLOW_PROCESS_SUBMIT_FULL ||
        result.status == CFLOW_PROCESS_SUBMIT_LEASE_IN_USE)
      return SALTS_OK;
    if (result.status == CFLOW_PROCESS_SUBMIT_CLOSED && state.close_requested) {
      if (is_stdout) state.stdout_eof = true;
      else state.stderr_eof = true;
      return SALTS_OK;
    }
    state.io_failed = true;
    state.failure_reason = "Salts::Process read submission failed with status " +
                           std::to_string(static_cast<int>(result.status));
    return SALTS_EIO;
  }

  int shellSubmitInput(ShellState& state) noexcept {
    if (state.close_requested || state.stdin_closed || state.stdin_write_pending)
      return SALTS_OK;
    if (state.input_offset < state.input.size()) {
      size_t remaining = state.input.size() - state.input_offset;
      size_t chunk = remaining < state.stdin_buffer.size()
                         ? remaining : state.stdin_buffer.size();
      std::copy_n(state.input.data() + state.input_offset, chunk,
                  state.stdin_buffer.data());
      auto result = cflow_process_try_write_stdin(
          &state.process, state.stdin_lease, state.stdin_buffer.data(), chunk);
      if (result.status == CFLOW_PROCESS_SUBMIT_ACCEPTED) {
        state.stdin_write_pending = true;
        return SALTS_OK;
      }
      if (result.status == CFLOW_PROCESS_SUBMIT_FULL) return SALTS_OK;
      state.io_failed = true;
      state.failure_reason = "Salts::Process stdin submission failed with status " +
                             std::to_string(static_cast<int>(result.status));
      return SALTS_EIO;
    }

    const int status = cflow_process_close_stdin(&state.process);
    if (status == SALTS_OK) {
      state.stdin_closed = true;
      return SALTS_OK;
    }
    if (status == SALTS_EBUSY) return SALTS_OK;
    state.io_failed = true;
    state.failure_reason = "Salts::Process stdin close failed with status " +
                           std::to_string(status);
    return SALTS_EIO;
  }

  int startShellProcess(size_t index, const actions::Node& source,
                        ShellState& state) {
    const auto command_it = source.params.find("cmd");
    if (command_it == source.params.end()) {
      state.failure_reason = "Shell requires parameter 'cmd'";
      return SALTS_EINVAL;
    }

    state.command = resolveStringValue(command_it->second, blackboard);
    if (state.command.empty()) {
      state.failure_reason = "Shell command must not be empty";
      return SALTS_EINVAL;
    }
    state.input.clear();
    const auto input_it = source.params.find("input");
    if (input_it != source.params.end()) {
      state.input = resolveStringValue(input_it->second, blackboard);
    }
    state.working_dir.clear();
    const auto cwd_it = source.params.find("working_dir");
    if (cwd_it != source.params.end()) {
      state.working_dir = resolveStringValue(cwd_it->second, blackboard);
    }
    state.output_key = "shell_output";
    state.stderr_key = "shell_stderr";
    state.exit_code_key = "shell_exit_code";
    const auto output_it = source.params.find("output_key");
    if (output_it != source.params.end()) state.output_key = resolveStringValue(output_it->second, blackboard);
    const auto stderr_it = source.params.find("stderr_key");
    if (stderr_it != source.params.end()) state.stderr_key = resolveStringValue(stderr_it->second, blackboard);
    const auto exit_it = source.params.find("exit_code_key");
    if (exit_it != source.params.end()) state.exit_code_key = resolveStringValue(exit_it->second, blackboard);
    state.stream_output = true;
    const auto stream_it = source.params.find("stream_output");
    if (stream_it != source.params.end()) state.stream_output = shellBooleanValue(stream_it->second, blackboard);

    uint64_t timeout_ms = UINT64_C(30000);
    const auto timeout_it = source.params.find("timeout");
    if (timeout_it != source.params.end()) {
      std::string timeout_text;
      if (std::holds_alternative<bool>(timeout_it->second)) {
        state.failure_reason = "Shell timeout must be an integer";
        return SALTS_EINVAL;
      }
      if (std::holds_alternative<double>(timeout_it->second)) {
        timeout_text = std::to_string(std::get<double>(timeout_it->second));
      } else {
        timeout_text = resolveStringValue(timeout_it->second, blackboard);
      }
      const int64_t timeout = parseInteger(timeout_text);
      if (timeout <= 0) {
        state.failure_reason = "Shell timeout must be positive";
        return SALTS_EINVAL;
      }
      timeout_ms = static_cast<uint64_t>(timeout);
    }

    std::vector<std::string> env_entries;
    std::vector<const char*> env;
    env_entries.reserve(executor.getEnvironment().size());
    for (const auto& entry : executor.getEnvironment()) {
      env_entries.push_back(entry.first + "=" + entry.second);
    }
    env.reserve(env_entries.size() + 1u);
    for (const auto& entry : env_entries) env.push_back(entry.c_str());
    env.push_back(nullptr);

    cmeta_process_options_t options{};
    cmeta_process_options_init(&options);
    options.program = state.command.c_str();
    options.args = nullptr;
    options.env = env_entries.empty() ? nullptr : env.data();
    options.cwd = state.working_dir.empty() ? nullptr : state.working_dir.c_str();
    options.flags = SALTS_PROCESS_SHELL_COMMAND;
    options.timeout_ms = timeout_ms;

    cflow_process_config config{};
#if defined(_WIN32)
    config.backend_kind = CFLOW_IO_NATIVE_IOCP;
#elif defined(__linux__)
    config.backend_kind = CFLOW_IO_NATIVE_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
      defined(__OpenBSD__) || defined(__DragonFly__)
    config.backend_kind = CFLOW_IO_NATIVE_KQUEUE;
#else
    state.failure_reason = "Salts::Process has no native pipe backend for this platform";
    return SALTS_ENOTSUP;
#endif
    config.request_capacity = 8u;
    config.command_capacity = 8u;
    config.completion_batch_capacity = 8u;
    config.completion = &onProcessCompletion;
    config.completion_user = &state;

    const int status = cflow_process_start(&state.process, &options, &config);
    if (status != SALTS_OK) {
      state.failure_reason = "Salts::Process start failed with status " +
                             std::to_string(status);
      return status;
    }
    state.started = true;
    if (state.stream_output) {
      Praktor::Shell::ShellExecutor::emitStreamLine("$ " + state.command);
    }
    (void)index;
    return SALTS_OK;
  }

  int tickShell(size_t index, const actions::Node& source,
                salts_bt_node_status* out_status) noexcept {
    if (out_status == nullptr || index >= shell_states.size()) return SALTS_EINVAL;
    auto& state = shell_states[index];
    try {
      if (!state.started && state.process.impl == nullptr) {
        const int start_status = startShellProcess(index, source, state);
        if (start_status != SALTS_OK) {
          last_error = state.failure_reason;
          publishShellResult(state, -1, false);
          *out_status = SALTS_BT_FAILURE;
          finalizeShellState(state);
          return SALTS_OK;
        }
      }
      leaf_owners[index] = LeafOwner::SaltsProcess;

      if (state.io_failed) {
        const std::string reason = state.failure_reason;
        (void)closeShellProcess(state, false);
        if (!reason.empty()) state.failure_reason = reason;
        publishShellResult(state, -1, false);
        last_error = state.failure_reason;
        finalizeShellState(state);
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }

      (void)shellSubmitRead(state, true);
      (void)shellSubmitRead(state, false);
      (void)shellSubmitInput(state);
      size_t progressed = 0u;
      const int run_status = cflow_process_run_ready(&state.process, 64u, &progressed);
      if (run_status != SALTS_OK) {
        state.io_failed = true;
        state.failure_reason = "Salts::Process driver failed with status " +
                               std::to_string(run_status);
      }
      if (!state.io_failed) {
        (void)shellSubmitRead(state, true);
        (void)shellSubmitRead(state, false);
        (void)shellSubmitInput(state);
        progressed = 0u;
        const int second_run_status =
            cflow_process_run_ready(&state.process, 64u, &progressed);
        if (second_run_status != SALTS_OK) {
          state.io_failed = true;
          state.failure_reason = "Salts::Process driver failed with status " +
                                 std::to_string(second_run_status);
        }
      }

      if (state.io_failed) {
        const std::string reason = state.failure_reason;
        (void)closeShellProcess(state, false);
        if (!reason.empty()) state.failure_reason = reason;
        publishShellResult(state, -1, false);
        last_error = state.failure_reason;
        finalizeShellState(state);
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }

      const int poll_status = cflow_process_poll(&state.process, &state.result);
      if (poll_status == SALTS_OK) {
        state.process_terminal = true;
      } else if (poll_status != SALTS_EBUSY) {
        state.io_failed = true;
        state.failure_reason = "Salts::Process poll failed with status " +
                               std::to_string(poll_status);
        last_error = state.failure_reason;
        (void)closeShellProcess(state, true);
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }

      if (!state.process_terminal || !state.stdout_eof || !state.stderr_eof ||
          state.stdin_write_pending || !state.stdin_closed ||
          state.stdout_read_pending || state.stderr_read_pending) {
        *out_status = SALTS_BT_RUNNING;
        return SALTS_OK;
      }

      flushStreamLine(state.stdout_pending_line, state.stream_output);
      flushStreamLine(state.stderr_pending_line, state.stream_output);
      const bool success = state.result.state == SALTS_PROCESS_EXITED &&
                           state.result.exit_code == 0;
      const int exit_code = state.result.exit_code;
      if (!success && state.failure_reason.empty()) {
        state.failure_reason =
            state.result.state == SALTS_PROCESS_TIMED_OUT
                ? "Shell command timed out"
                : "Shell command exited unsuccessfully";
      }
      const int cleanup_status = closeShellProcess(state, false);
      if (cleanup_status != SALTS_OK) {
        last_error = state.failure_reason.empty()
                         ? "Salts::Process cleanup failed"
                         : state.failure_reason;
      }
      publishShellResult(state, exit_code, success && cleanup_status == SALTS_OK);
      if (!success || cleanup_status != SALTS_OK) {
        if (!state.failure_reason.empty()) last_error = state.failure_reason;
      }
      finalizeShellState(state);
      *out_status = (success && cleanup_status == SALTS_OK)
                        ? SALTS_BT_SUCCESS : SALTS_BT_FAILURE;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("Salts::Process Shell tick failed");
      if (state.process.impl != nullptr) (void)closeShellProcess(state, true);
      finalizeShellState(state);
      *out_status = SALTS_BT_FAILURE;
      return SALTS_OK;
    }
  }

  int tickLeaf(size_t index, salts_bt_node_status* out_status) noexcept {
    if (out_status == nullptr || nodeAt(index) == nullptr) return SALTS_EINVAL;
    if (index < compiled_plan.terminal_actions.size()) {
      const TreePlanTerminalAction action = compiled_plan.terminal_actions[index];
      if (action == TreePlanTerminalAction::Success) {
        *out_status = SALTS_BT_SUCCESS;
        return SALTS_OK;
      }
      if (action == TreePlanTerminalAction::Failure) {
        *out_status = SALTS_BT_FAILURE;
        return SALTS_OK;
      }
      if (action == TreePlanTerminalAction::SwitchFailure) {
        try {
          const auto* source = nodeAt(index);
          if (source == nullptr) return SALTS_EINVAL;
          // Preserve legacy ordering: validate the selector before concluding
          // that an empty Switch has no child and therefore fails.
          (void)resolveSwitchValue(*source, blackboard);
          *out_status = SALTS_BT_FAILURE;
          return SALTS_OK;
        } catch (...) {
          rememberCurrentException("C11 empty Switch selector failed");
          return SALTS_EINVAL;
        }
      }
    }
    try {
      const actions::Node* source = nodeAt(index);
      const LeafOwner owner = leaf_owners[index];

      // Once a leaf is RUNNING, keep the selected execution owner. In
      // particular, do not re-call a dispatcher hook and accidentally move
      // an Executor leaf into a host-owned activation (or vice versa).
      if (owner == LeafOwner::SaltsProcess) {
        return tickShell(index, *source, out_status);
      }
      if (owner == LeafOwner::Executor) {
        *out_status = toSaltsStatus(executor.execute(*source, blackboard,
                                                     execution_context));
        if (*out_status == SALTS_BT_RUNNING && hooks.halt_leaf == nullptr) {
          last_error = "registered action leaf '" + source->id +
                       "' returned RUNNING without a halt_leaf hook";
          leaf_owners[index] = LeafOwner::None;
          return SALTS_ENOTSUP;
        }
        leaf_owners[index] =
            (*out_status == SALTS_BT_RUNNING) ? LeafOwner::Executor : LeafOwner::None;
        return SALTS_OK;
      }
      if (owner == LeafOwner::HostHook) {
        const int host_status = hooks.tick_leaf == nullptr
            ? SALTS_ENOTSUP
            : hooks.tick_leaf(hooks.user, index, out_status);
        if (host_status == SALTS_ENOTSUP) {
          haltLeaf(index);
          last_error = "host leaf hook declined active node '" + source->id +
                       "'; refusing to switch execution owners";
          return SALTS_ENOTSUP;
        }
        if (host_status != SALTS_OK) {
          haltLeaf(index);
          last_error = "host leaf hook returned status " + std::to_string(host_status) +
                       " for node '" + source->id + "'";
          return host_status;
        }
        if (*out_status != SALTS_BT_SUCCESS &&
            *out_status != SALTS_BT_FAILURE &&
            *out_status != SALTS_BT_RUNNING) {
          haltLeaf(index);
          last_error = "host leaf hook returned an invalid node status for '" +
                       source->id + "'";
          return SALTS_EINVAL;
        }
        leaf_owners[index] =
            (*out_status == SALTS_BT_RUNNING) ? LeafOwner::HostHook : LeafOwner::None;
        return SALTS_OK;
      }

      // An inactive leaf may be claimed by the optional host hook. ENOTSUP
      // means "not mine"; other errors are authoritative and never fall back.
      if (hooks.tick_leaf != nullptr) {
        const int host_status = safeHostStatus("C11 host leaf tick hook failed", [&] {
          return hooks.tick_leaf(hooks.user, index, out_status);
        });
        if (host_status == SALTS_OK) {
          if (*out_status != SALTS_BT_SUCCESS &&
              *out_status != SALTS_BT_FAILURE &&
              *out_status != SALTS_BT_RUNNING) {
            safeHostVoid("C11 host leaf cleanup after invalid status failed", [&] {
              hooks.halt_leaf(hooks.user, index);
            });
            last_error = "host leaf hook returned an invalid node status for '" +
                         source->id + "'";
            return SALTS_EINVAL;
          }
          leaf_owners[index] =
              (*out_status == SALTS_BT_RUNNING) ? LeafOwner::HostHook : LeafOwner::None;
          return SALTS_OK;
        }
        if (host_status != SALTS_ENOTSUP) {
          // A failing tick may have partially acquired host resources even on
          // the first activation. Request idempotent cleanup before Salts resets
          // the tree; the adapter's subsequent halt callback sees no owner and
          // therefore cannot invoke this cleanup a second time.
          safeHostVoid("C11 host leaf cleanup after tick failure failed", [&] {
            hooks.halt_leaf(hooks.user, index);
          });
          if (last_error.empty()) {
            last_error = "host leaf hook returned status " + std::to_string(host_status) +
                         " for node '" + source->id + "'";
          }
          leaf_owners[index] = LeafOwner::None;
          return host_status;
        }
      }

      if (source->id == "Shell") {
        return tickShell(index, *source, out_status);
      }

      *out_status = toSaltsStatus(executor.execute(*source, blackboard,
                                                   execution_context));
      if (*out_status == SALTS_BT_RUNNING && hooks.halt_leaf == nullptr) {
        last_error = "registered action leaf '" + source->id +
                     "' returned RUNNING without a halt_leaf hook";
        return SALTS_ENOTSUP;
      }
      leaf_owners[index] =
          (*out_status == SALTS_BT_RUNNING) ? LeafOwner::Executor : LeafOwner::None;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 action leaf failed");
      return SALTS_EIO;
    }
  }

  void haltLeaf(size_t index) noexcept {
    if (index >= leaf_owners.size()) return;
    const LeafOwner owner = leaf_owners[index];
    if (owner == LeafOwner::SaltsProcess ||
        (index < shell_states.size() && shell_states[index].process.impl != nullptr)) {
      (void)closeShellProcess(shell_states[index], true);
      return;
    }
    leaf_owners[index] = LeafOwner::None;
    if ((owner == LeafOwner::HostHook || owner == LeafOwner::Executor) &&
        hooks.halt_leaf != nullptr) {
      try {
        hooks.halt_leaf(hooks.user, index);
      } catch (...) {
        rememberCurrentException("C11 host leaf halt failed");
      }
    }
  }

  int condition(size_t index, salts_bt_node_status* out_status) noexcept {
    if (out_status == nullptr) return SALTS_EINVAL;
    try {
      if (hooks.condition != nullptr) {
        return hooks.condition(hooks.user, index, out_status);
      }
      const auto* source = nodeAt(index);
      if (source == nullptr) return SALTS_EINVAL;
      const auto it = source->params.find("condition");
      if (it == source->params.end()) {
        last_error = "condition callback requested without scalar condition parameter";
        return SALTS_EINVAL;
      }
      *out_status = conditionValue(it->second, blackboard)
                        ? SALTS_BT_SUCCESS
                        : SALTS_BT_FAILURE;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 condition evaluation failed");
      return SALTS_EINVAL;
    }
  }

  int selectChild(size_t index, size_t* out_child_index) noexcept {
    if (out_child_index == nullptr) return SALTS_EINVAL;
    try {
      if (hooks.select_child != nullptr) {
        return hooks.select_child(hooks.user, index, out_child_index);
      }
      const auto* source = nodeAt(index);
      if (source == nullptr) return SALTS_EINVAL;
      const int parsed = resolveSwitchValue(*source, blackboard);
      *out_child_index = parsed < 0 ? source->children.size()
                                    : static_cast<size_t>(parsed);
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 Switch selector failed");
      return SALTS_EINVAL;
    }
  }

  template <typename Callback>
  int safeHostStatus(const char* operation, Callback&& callback) noexcept {
    try {
      return callback();
    } catch (...) {
      rememberCurrentException(operation);
      return SALTS_EIO;
    }
  }

  template <typename Callback>
  void safeHostVoid(const char* operation, Callback&& callback) noexcept {
    try {
      callback();
    } catch (...) {
      rememberCurrentException(operation);
    }
  }

  int parallelEnter(size_t node, size_t child, bool first) noexcept {
    if (hooks.parallel_enter == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 Parallel enter hook failed", [&] {
      return hooks.parallel_enter(hooks.user, node, child, first);
    });
  }
  int parallelStep(size_t node, size_t child, salts_bt_node_status status) noexcept {
    if (hooks.parallel_step == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 Parallel step hook failed", [&] {
      return hooks.parallel_step(hooks.user, node, child, status);
    });
  }
  int parallelJoin(size_t node, salts_bt_node_status status) noexcept {
    if (hooks.parallel_join == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 Parallel join hook failed", [&] {
      return hooks.parallel_join(hooks.user, node, status);
    });
  }
  int parallelFinish(size_t node, size_t child, salts_bt_node_status status) noexcept {
    if (hooks.parallel_finish == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 Parallel finish hook failed", [&] {
      return hooks.parallel_finish(hooks.user, node, child, status);
    });
  }
  void parallelAbort(size_t node, size_t child) noexcept {
    if (hooks.parallel_abort == nullptr) return;
    safeHostVoid("C11 Parallel abort hook failed", [&] {
      hooks.parallel_abort(hooks.user, node, child);
    });
  }
  int pipelinePrepare(size_t node, size_t child) noexcept {
    if (hooks.pipeline_prepare != nullptr) {
      return safeHostStatus("C11 PipelineSequence prepare hook failed", [&] {
        return hooks.pipeline_prepare(hooks.user, node, child);
      });
    }

    try {
      const auto* source = nodeAt(node);
      if (source == nullptr || node >= pipeline_states.size() ||
          child >= source->children.size()) return SALTS_EINVAL;
      auto& state = pipeline_states[node];
      if (!state.initialized) {
        std::string input_key = "pipeline_input";
        const auto input = source->params.find("input_key");
        if (input != source->params.end()) {
          input_key = resolveStringValue(input->second, blackboard);
        }
        state.output_key = "pipeline_result";
        const auto output = source->params.find("output_key");
        if (output != source->params.end()) {
          state.output_key = resolveStringValue(output->second, blackboard);
        }
        state.data = blackboard.has(input_key) ? blackboard.get(input_key) : "";
        state.initialized = true;
      }
      blackboard.set("pipeline_input", state.data);
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 PipelineSequence prepare failed");
      return SALTS_EINVAL;
    }
  }

  int pipelineComplete(size_t node, size_t child,
                       salts_bt_node_status status) noexcept {
    if (hooks.pipeline_complete != nullptr) {
      return safeHostStatus("C11 PipelineSequence complete hook failed", [&] {
        return hooks.pipeline_complete(hooks.user, node, child, status);
      });
    }

    try {
      const auto* source = nodeAt(node);
      if (source == nullptr || node >= pipeline_states.size() ||
          child >= source->children.size()) return SALTS_EINVAL;
      auto& state = pipeline_states[node];
      if (!state.initialized) return SALTS_EINVAL;

      if (status == SALTS_BT_SUCCESS) {
        if (blackboard.has("pipeline_output")) {
          state.data = blackboard.get("pipeline_output");
        }
        if (child + 1u == source->children.size()) {
          blackboard.set(state.output_key, state.data);
          state = PipelineState{};
        }
      } else {
        state = PipelineState{};
      }
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 PipelineSequence complete failed");
      return SALTS_EINVAL;
    }
  }

  void pipelineAbort(size_t node, size_t child) noexcept {
    if (hooks.pipeline_abort != nullptr) {
      safeHostVoid("C11 PipelineSequence abort hook failed", [&] {
        hooks.pipeline_abort(hooks.user, node, child);
      });
      return;
    }
    if (node < pipeline_states.size()) pipeline_states[node] = PipelineState{};
  }

  int entryChanged(size_t node, bool* changed) noexcept {
    if (changed == nullptr) return SALTS_EINVAL;
    if (hooks.entry_changed != nullptr) {
      return safeHostStatus("C11 EntryUpdated hook failed", [&] {
        return hooks.entry_changed(hooks.user, node, changed);
      });
    }

    try {
      const auto* source = nodeAt(node);
      if (source == nullptr || node >= entry_updated_states.size()) return SALTS_EINVAL;
      const auto key_it = source->params.find("watch_key");
      if (key_it == source->params.end()) {
        last_error = "EntryUpdated requires parameter 'watch_key'";
        return SALTS_EINVAL;
      }
      const std::string key = resolveStringValue(key_it->second, blackboard);
      const std::string current = blackboard.has(key) ? blackboard.get(key) : "";
      auto& state = entry_updated_states[node];
      *changed = !state.observed || current != state.value;
      state.value = current;
      state.observed = true;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 EntryUpdated evaluation failed");
      return SALTS_EINVAL;
    }
  }
  int queueBegin(size_t node, bool* has_item) noexcept {
    if (has_item == nullptr) return SALTS_EINVAL;
    if (hooks.queue_begin != nullptr) {
      return safeHostStatus("C11 ConsumeQueue begin hook failed", [&] {
        return hooks.queue_begin(hooks.user, node, has_item);
      });
    }

    try {
      const auto* source = nodeAt(node);
      if (source == nullptr || node >= queue_states.size()) return SALTS_EINVAL;
      auto& state = queue_states[node];
      state = QueueState{};

      std::string queue_key = "task_queue";
      const auto queue_key_it = source->params.find("queue_key");
      if (queue_key_it != source->params.end()) {
        queue_key = resolveStringValue(queue_key_it->second, blackboard);
      }
      if (!blackboard.has(queue_key)) {
        *has_item = false;
        return SALTS_OK;
      }

      std::string item;
      std::string remaining;
      bool has_more = false;
      bool queue_is_empty = false;
      if (!consumeJsonArrayFront(blackboard.get(queue_key), item, remaining,
                                 has_more, queue_is_empty)) {
        if (!queue_is_empty) return SALTS_EIO;
        *has_item = false;
        return SALTS_OK;
      }

      std::string item_key = "current_item";
      const auto item_key_it = source->params.find("item_key");
      if (item_key_it != source->params.end()) {
        item_key = resolveStringValue(item_key_it->second, blackboard);
      }

      // Match the legacy Executor: dequeue and publish the current item before
      // invoking the child, including when that child later fails or is halted.
      blackboard.set(queue_key, remaining);
      blackboard.set(item_key, item);
      state.active = true;
      state.has_more = has_more;
      *has_item = true;
      return SALTS_OK;
    } catch (const std::invalid_argument& error) {
      // The existing Executor maps malformed queue payloads to a node
      // FAILURE rather than a runtime error. Preserve that observable status,
      // while retaining detail for diagnostics.
      last_error = std::string("ConsumeQueue payload rejected: ") + error.what();
      *has_item = false;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 ConsumeQueue begin failed");
      return SALTS_EIO;
    }
  }

  int queueFinish(size_t node, bool* has_more) noexcept {
    if (has_more == nullptr) return SALTS_EINVAL;
    if (hooks.queue_finish != nullptr) {
      return safeHostStatus("C11 ConsumeQueue finish hook failed", [&] {
        return hooks.queue_finish(hooks.user, node, has_more);
      });
    }
    if (node >= queue_states.size() || !queue_states[node].active) {
      last_error = "ConsumeQueue finish called without an active queue item";
      return SALTS_EINVAL;
    }
    *has_more = queue_states[node].has_more;
    queue_states[node] = QueueState{};
    return SALTS_OK;
  }

  void queueAbort(size_t node) noexcept {
    if (hooks.queue_abort != nullptr) {
      safeHostVoid("C11 ConsumeQueue abort hook failed", [&] {
        hooks.queue_abort(hooks.user, node);
      });
      return;
    }
    if (node < queue_states.size()) queue_states[node] = QueueState{};
  }

  static Impl* self(void* user) noexcept { return static_cast<Impl*>(user); }
  static int onTickLeaf(void* user, size_t index,
                        salts_bt_node_status* out_status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->tickLeaf(index, out_status);
  }
  static void onHaltLeaf(void* user, size_t index) {
    if (auto* impl = self(user)) impl->haltLeaf(index);
  }
  static int onCondition(void* user, size_t index,
                         salts_bt_node_status* out_status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->condition(index, out_status);
  }
  static int onSelectChild(void* user, size_t index, size_t* out_index) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->selectChild(index, out_index);
  }
  static int onParallelEnter(void* user, size_t node, size_t child, bool first) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->parallelEnter(node, child, first);
  }
  static int onParallelStep(void* user, size_t node, size_t child,
                            salts_bt_node_status status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->parallelStep(node, child, status);
  }
  static int onParallelJoin(void* user, size_t node, salts_bt_node_status status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->parallelJoin(node, status);
  }
  static int onParallelFinish(void* user, size_t node, size_t child,
                              salts_bt_node_status status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->parallelFinish(node, child, status);
  }
  static void onParallelAbort(void* user, size_t node, size_t child) {
    if (auto* impl = self(user)) impl->parallelAbort(node, child);
  }
  static int onPipelinePrepare(void* user, size_t node, size_t child) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->pipelinePrepare(node, child);
  }
  static int onPipelineComplete(void* user, size_t node, size_t child,
                                salts_bt_node_status status) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->pipelineComplete(node, child, status);
  }
  static void onPipelineAbort(void* user, size_t node, size_t child) {
    if (auto* impl = self(user)) impl->pipelineAbort(node, child);
  }
  static int onEntryChanged(void* user, size_t node, bool* changed) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->entryChanged(node, changed);
  }
  static int onQueueBegin(void* user, size_t node, bool* has_item) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->queueBegin(node, has_item);
  }
  static int onQueueFinish(void* user, size_t node, bool* has_more) {
    auto* impl = self(user);
    return impl == nullptr ? SALTS_EINVAL : impl->queueFinish(node, has_more);
  }
  static void onQueueAbort(void* user, size_t node) {
    if (auto* impl = self(user)) impl->queueAbort(node);
  }
};

TreeExecutorSession::TreeExecutorSession(
    const actions::Tree& tree, actions::Executor& executor,
    actions::Blackboard& blackboard,
    actions::ExecutionContext& execution_context,
    const salts_bt_tree_config& limits,
    const praktor_c11_bt_host* host_hooks)
    : impl_(std::make_unique<Impl>(tree, executor, blackboard,
                                  execution_context, limits, host_hooks)) {}

TreeExecutorSession::~TreeExecutorSession() = default;

int TreeExecutorSession::tick(salts_bt_node_status* out_status) {
  if (!impl_) return SALTS_EINVAL;
  impl_->last_error.clear();
  const int status = praktor_c11_bt_tree_tick(&impl_->runtime, out_status);
  if (status != SALTS_OK && impl_->last_error.empty()) {
    impl_->last_error = "C11 behavior tree tick failed with status " +
                        std::to_string(status);
  }
  return status;
}

int TreeExecutorSession::reset() {
  if (!impl_) return SALTS_EINVAL;
  impl_->last_error.clear();
  const int status = praktor_c11_bt_tree_reset(&impl_->runtime);
  if (status == SALTS_OK) {
    for (size_t i = 0u; i < impl_->leaf_owners.size(); ++i) {
      impl_->leaf_owners[i] =
          i < impl_->shell_states.size() &&
                  impl_->shell_states[i].process.impl != nullptr
              ? Impl::LeafOwner::SaltsProcess
              : Impl::LeafOwner::None;
    }
    std::fill(impl_->pipeline_states.begin(), impl_->pipeline_states.end(), Impl::PipelineState{});
    std::fill(impl_->entry_updated_states.begin(), impl_->entry_updated_states.end(), Impl::EntryUpdatedState{});
    std::fill(impl_->queue_states.begin(), impl_->queue_states.end(), Impl::QueueState{});
    if (!impl_->last_error.empty()) return SALTS_EIO;
  } else {
    impl_->last_error = "C11 behavior tree reset failed with status " +
                        std::to_string(status);
  }
  return status;
}

int TreeExecutorSession::cancel() {
  if (!impl_) return SALTS_EINVAL;
  impl_->last_error.clear();
  const int status = praktor_c11_bt_tree_cancel(&impl_->runtime);
  if (status == SALTS_OK) {
    for (size_t i = 0u; i < impl_->leaf_owners.size(); ++i) {
      impl_->leaf_owners[i] =
          i < impl_->shell_states.size() &&
                  impl_->shell_states[i].process.impl != nullptr
              ? Impl::LeafOwner::SaltsProcess
              : Impl::LeafOwner::None;
    }
    std::fill(impl_->pipeline_states.begin(), impl_->pipeline_states.end(), Impl::PipelineState{});
    std::fill(impl_->entry_updated_states.begin(), impl_->entry_updated_states.end(), Impl::EntryUpdatedState{});
    std::fill(impl_->queue_states.begin(), impl_->queue_states.end(), Impl::QueueState{});
    if (!impl_->last_error.empty()) return SALTS_EIO;
  } else {
    impl_->last_error = "C11 behavior tree cancel failed with status " +
                        std::to_string(status);
  }
  return status;
}

int TreeExecutorSession::validate() const {
  return impl_ ? praktor_c11_bt_tree_validate(&impl_->runtime) : SALTS_EINVAL;
}

const actions::Node* TreeExecutorSession::sourceNode(size_t node_index) const noexcept {
  return impl_ ? impl_->nodeAt(node_index) : nullptr;
}

const TreePlan& TreeExecutorSession::plan() const noexcept {
  return impl_->compiled_plan;
}

const std::string& TreeExecutorSession::lastError() const noexcept {
  static const std::string empty;
  return impl_ ? impl_->last_error : empty;
}

} // namespace actions::c11
