#include "tree_executor_session.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
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
  try {
    size_t consumed = 0u;
    const long long integer = std::stoll(raw, &consumed, 10);
    if (consumed == raw.size()) return integer;
  } catch (const std::exception&) {
    // Try the legacy executor's integral floating-point string form below.
  }

  try {
    size_t consumed = 0u;
    const double number = std::stod(raw, &consumed);
    const double fractional = number - std::trunc(number);
    const double upper_exclusive =
        -static_cast<double>(std::numeric_limits<long long>::min());
    if (consumed != raw.size() || !std::isfinite(number) ||
        std::abs(fractional) > 0.0 ||
        number < static_cast<double>(std::numeric_limits<long long>::min()) ||
        number >= upper_exclusive) {
      throw std::invalid_argument("not an integral number");
    }
    return static_cast<long long>(number);
  } catch (const std::exception&) {
    throw std::invalid_argument("switch variable is not an integer: " + raw);
  }
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

  std::vector<unsigned char> custom_leaf_active;
  std::vector<PipelineState> pipeline_states;
  std::vector<EntryUpdatedState> entry_updated_states;
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
    custom_leaf_active.assign(compiled_plan.nodes.size(), 0u);
    pipeline_states.resize(compiled_plan.nodes.size());
    entry_updated_states.resize(compiled_plan.nodes.size());

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

      if (source->id == "Shell" &&
          (hooks.tick_leaf == nullptr || hooks.halt_leaf == nullptr)) {
        throw std::invalid_argument(
            "C11 Shell leaves require explicit host tick_leaf and halt_leaf hooks; "
            "legacy ShellExecutor fallback is disabled");
      }
      if (hooks.tick_leaf != nullptr && hooks.halt_leaf == nullptr) {
        throw std::invalid_argument(
            "A custom leaf tick hook requires a halt hook to define cancellation cleanup");
      }
      if (desc.kind == SALTS_BT_NODE_PARALLEL && desc.parallel_hooks &&
          (hooks.parallel_enter == nullptr || hooks.parallel_step == nullptr ||
           hooks.parallel_join == nullptr || hooks.parallel_finish == nullptr)) {
        throw std::invalid_argument(
            "C11 Parallel requires host enter/step/join/finish hooks");
      }
      if (desc.kind == SALTS_BT_NODE_PIPELINE_SEQUENCE &&
          ((hooks.pipeline_prepare == nullptr) !=
           (hooks.pipeline_complete == nullptr))) {
        throw std::invalid_argument(
            "Custom PipelineSequence hooks must provide prepare and complete together");
      }
      if (desc.kind == SALTS_BT_NODE_CONSUME_QUEUE &&
          (hooks.queue_begin == nullptr || hooks.queue_finish == nullptr)) {
        throw std::invalid_argument("C11 ConsumeQueue requires host queue_begin/queue_finish hooks");
      }
    }
  }

  int tickLeaf(size_t index, salts_bt_node_status* out_status) noexcept {
    if (out_status == nullptr || nodeAt(index) == nullptr) return SALTS_EINVAL;
    try {
      const actions::Node* source = nodeAt(index);
      if (hooks.tick_leaf != nullptr) {
        const int host_status = hooks.tick_leaf(hooks.user, index, out_status);
        if (host_status != SALTS_ENOTSUP) {
          if (host_status == SALTS_OK) {
            custom_leaf_active[index] = (*out_status == SALTS_BT_RUNNING) ? 1u : 0u;
          } else {
            last_error = "host leaf hook returned status " + std::to_string(host_status) +
                         " for node '" + source->id + "'";
          }
          return host_status;
        }
      }

      if (source->id == "Shell") {
        last_error = "Shell host hook returned SALTS_ENOTSUP; refusing legacy ShellExecutor fallback";
        return SALTS_ENOTSUP;
      }

      *out_status = toSaltsStatus(executor.execute(*source, blackboard,
                                                   execution_context));
      // The legacy TaskFunction API has no independent halt slot, but a
      // session-level halt hook can still clean up a RUNNING registered leaf.
      // Track activity for every leaf path, not only the custom tick path.
      custom_leaf_active[index] =
          (*out_status == SALTS_BT_RUNNING) ? 1u : 0u;
      return SALTS_OK;
    } catch (...) {
      rememberCurrentException("C11 action leaf failed");
      return SALTS_EIO;
    }
  }

  void haltLeaf(size_t index) noexcept {
    if (index >= custom_leaf_active.size() || custom_leaf_active[index] == 0u) return;
    custom_leaf_active[index] = 0u;
    if (hooks.halt_leaf == nullptr) return;
    try {
      hooks.halt_leaf(hooks.user, index);
    } catch (...) {
      rememberCurrentException("C11 host leaf halt failed");
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
      const auto it = source->params.find("variable");
      if (it == source->params.end()) {
        last_error = "Switch requires parameter 'variable'";
        return SALTS_EINVAL;
      }

      std::string raw;
      if (std::holds_alternative<std::string>(it->second)) {
        raw = resolveStringValue(it->second, blackboard);
        if (raw == std::get<std::string>(it->second) && blackboard.has(raw)) {
          raw = blackboard.get(raw);
        }
      } else if (std::holds_alternative<double>(it->second)) {
        const double number = std::get<double>(it->second);
        const double fractional = number - std::trunc(number);
        if (!std::isfinite(number) || std::abs(fractional) > 0.0 ||
            number < static_cast<double>(std::numeric_limits<long long>::min()) ||
            number >= -static_cast<double>(std::numeric_limits<long long>::min())) {
          last_error = "Switch variable must resolve to an integer";
          return SALTS_EINVAL;
        }
        const auto parsed = static_cast<long long>(number);
        *out_child_index = parsed < 0 ? source->children.size()
                                      : static_cast<unsigned long long>(parsed) >
                                                static_cast<unsigned long long>(
                                                    std::numeric_limits<size_t>::max())
                                            ? source->children.size()
                                            : static_cast<size_t>(parsed);
        return SALTS_OK;
      } else {
        last_error = "Switch variable must not be a boolean";
        return SALTS_EINVAL;
      }

      const long long parsed = parseInteger(raw);
      *out_child_index = parsed < 0 ? source->children.size()
                                    : static_cast<unsigned long long>(parsed) >
                                              static_cast<unsigned long long>(
                                                  std::numeric_limits<size_t>::max())
                                          ? source->children.size()
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
          input_key = executor.resolveParam(input->second, blackboard);
        }
        state.output_key = "pipeline_result";
        const auto output = source->params.find("output_key");
        if (output != source->params.end()) {
          state.output_key = executor.resolveParam(output->second, blackboard);
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
      const std::string key = executor.resolveParam(key_it->second, blackboard);
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
    if (hooks.queue_begin == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 ConsumeQueue begin hook failed", [&] {
      return hooks.queue_begin(hooks.user, node, has_item);
    });
  }
  int queueFinish(size_t node, bool* has_more) noexcept {
    if (hooks.queue_finish == nullptr) return SALTS_ENOTSUP;
    return safeHostStatus("C11 ConsumeQueue finish hook failed", [&] {
      return hooks.queue_finish(hooks.user, node, has_more);
    });
  }
  void queueAbort(size_t node) noexcept {
    if (hooks.queue_abort == nullptr) return;
    safeHostVoid("C11 ConsumeQueue abort hook failed", [&] {
      hooks.queue_abort(hooks.user, node);
    });
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
    std::fill(impl_->custom_leaf_active.begin(), impl_->custom_leaf_active.end(), 0u);
    std::fill(impl_->pipeline_states.begin(), impl_->pipeline_states.end(), Impl::PipelineState{});
    std::fill(impl_->entry_updated_states.begin(), impl_->entry_updated_states.end(), Impl::EntryUpdatedState{});
  } else {
    impl_->last_error = "C11 behavior tree reset failed with status +"
                        std::to_string(status);
  }
  return status;
}

int TreeExecutorSession::cancel() {
  if (!impl_) return SALTS_EINVAL;
  impl_->last_error.clear();
  const int status = praktor_c11_bt_tree_cancel(&impl_->runtime);
  if (status == SALTS_OK) {
    std::fill(impl_->custom_leaf_active.begin(), impl_->custom_leaf_active.end(), 0u);
    std::fill(impl_->pipeline_states.begin(), impl_->pipeline_states.end(), Impl::PipelineState{});
    std::fill(impl_->entry_updated_states.begin(), impl_->entry_updated_states.end(), Impl::EntryUpdatedState{});
  } else {
    impl_->last_error = "C11 behavior tree cancel failed with status "
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
