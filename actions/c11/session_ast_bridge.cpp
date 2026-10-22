#include "tree_executor_session.hpp"
#include "session_compatibility.hpp"
#include "session.h"
#include "plan.h"
#include <algorithm>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

namespace actions::c11 {
namespace {
struct AstParameters {
  std::vector<praktor_c11_leaf_parameter> values;
  std::vector<size_t> sizes;
  explicit AstParameters(const actions::Node& source) {
    if (source.id.find('\0') != std::string::npos) throw std::invalid_argument("Action type contains NUL");
    for (const auto& entry : source.params) {
      if (entry.first.find('\0') != std::string::npos) throw std::invalid_argument("Parameter name contains NUL");
      praktor_c11_leaf_parameter p{}; size_t size = 0;
      p.name = entry.first.c_str();
      if (std::holds_alternative<std::string>(entry.second)) {
        const auto& text = std::get<std::string>(entry.second);
        p.kind = PRAKTOR_C11_LEAF_STRING; p.text = text.c_str(); size = text.size();
      } else if (std::holds_alternative<double>(entry.second)) {
        p.kind = PRAKTOR_C11_LEAF_NUMBER; p.number = std::get<double>(entry.second);
      } else { p.kind = PRAKTOR_C11_LEAF_BOOLEAN; p.boolean = std::get<bool>(entry.second); }
      values.push_back(p); sizes.push_back(size);
    }
  }
};
void appendPlan(TreePlan& plan, const actions::Node& source, size_t parent, size_t depth,
                const salts_bt_tree_config& limits) {
  if (depth > limits.max_depth) throw std::length_error("Action tree exceeds configured max_depth");
  if (plan.nodes.size() >= limits.max_nodes) throw std::length_error("Action tree exceeds configured max_nodes");
  AstParameters params(source);
  praktor_c11_session_node node{}; bool prune = false; char error[512];
  const int status = praktor_c11_compile_node(&limits,source.id.c_str(),params.values.data(),params.values.size(),
      params.sizes.data(),source.children.size(),parent,&node,&prune,error,sizeof(error));
  if (status != SALTS_OK) throw std::invalid_argument(error);
  const size_t index = plan.nodes.size();
  plan.nodes.push_back(node.tree); plan.source_nodes.push_back(&source);
  plan.terminal_actions.push_back(static_cast<TreePlanTerminalAction>(node.terminal));
  if (!prune) for (const auto& child : source.children) appendPlan(plan,child,index,depth+1,limits);
}
std::string resolveSubTreeName(const actions::Node& node) {
  const auto it = node.params.find("tree");
  if (it == node.params.end()) {
    throw std::invalid_argument("SubTree requires 'tree' parameter");
  }
  if (std::holds_alternative<std::string>(it->second)) {
    const std::string& raw = std::get<std::string>(it->second);
    if (raw.size() >= 3u && raw.front() == '{' && raw.back() == '}') {
      // Expansion happens only once. A mutable Blackboard selector would be
      // frozen at construction rather than resolved on each SubTree visit.
      throw std::invalid_argument(
          "Dynamic SubTree selector is unsupported by the fixed C11 plan; "
          "use a literal tree name or rebuild the session after binding");
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
    const SubTreeResolver& subtree_resolver,
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
    const std::string name = resolveSubTreeName(source);
    if (std::find(active_subtrees.begin(), active_subtrees.end(), name) !=
        active_subtrees.end()) {
      throw std::invalid_argument("recursive SubTree reference: " + name);
    }
    const actions::Tree* registered = subtree_resolver ? subtree_resolver(name) : nullptr;
    if (registered == nullptr) {
      throw std::invalid_argument("SubTree not found: " + name);
    }

    active_subtrees.push_back(name);
    try {
      actions::Node expanded = expandSubTreeNode(
          registered->root, subtree_resolver, limits, source_depth,
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
        child, subtree_resolver, limits, source_depth + 1u,
        subtree_depth, expanded_node_count, active_subtrees));
  }
  return expanded;
}

actions::Tree expandSubTrees(const actions::Tree& source,
                             const SubTreeResolver& subtree_resolver,
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
  expanded.root = expandSubTreeNode(source.root, subtree_resolver, limits,
                                    1u, 0u, expanded_node_count, active_subtrees);
  return expanded;
}

} // namespace

TreePlan compileTreePlan(const actions::Tree& tree, const salts_bt_tree_config& limits) {
  if (!limits.max_nodes || limits.max_nodes > UINT32_MAX || !limits.max_depth || limits.max_depth > SALTS_BT_MAX_DEPTH ||
      !limits.max_control_iterations || limits.max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS)
    throw std::invalid_argument("Invalid Salts BehaviorTree limits");
  TreePlan plan;
  plan.nodes.reserve(limits.max_nodes); plan.source_nodes.reserve(limits.max_nodes);
  appendPlan(plan,tree.root,PRAKTOR_C11_BT_NO_PARENT,1,limits); return plan;
}

/* Construction metadata and an exception boundary only. All execution state,
 * ownership selection, controls and Process lifetime live in session.c. */
struct TreeExecutorSession::Impl {
  actions::Tree tree;
  TreePlan compiled_plan;
  std::shared_ptr<detail::SessionCompatibility> compatibility;
  praktor_c11_bt_host hooks{};
  praktor_c11_session runtime{};
  std::vector<std::string> environment_entries;
  std::vector<const char*> environment_pointers;
  std::string callback_error, last_error;

  const actions::Node* nodeAt(size_t index) const noexcept {
    return index < compiled_plan.source_nodes.size() ? compiled_plan.source_nodes[index] : nullptr;
  }
  bool hasLeafDispatch() const noexcept { return hooks.tick_leaf || hooks.tick_typed_leaf; }
  bool hasLeafHalt() const noexcept { return hooks.halt_leaf || hooks.halt_typed_leaf; }
  void remember(const char* operation) noexcept {
    try { throw; }
    catch (const std::exception& error) { callback_error = std::string(operation) + ": " + error.what(); }
    catch (...) { callback_error = std::string(operation) + ": unknown C++ exception"; }
  }
  template<class F> static int boundary(Impl* self, const char* operation, F&& call, int failure = SALTS_EIO) noexcept {
    try { return call(); } catch (...) { self->remember(operation); return failure; }
  }
  int finish(int status) {
    last_error = praktor_c11_session_error(&runtime);
    if (!callback_error.empty()) {
      if (!last_error.empty()) last_error += ": ";
      last_error += callback_error;
      if (status == SALTS_OK) status = SALTS_EIO;
    }
    return status;
  }
  static int bridge_tick_leaf(void* user, size_t n, salts_bt_node_status* status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 host leaf tick hook failed", [&] { return self->hooks.tick_leaf(self->hooks.user,n,status); });
  }
  static void bridge_halt_leaf(void* user, size_t n) noexcept {
    auto* self = static_cast<Impl*>(user);
    (void)boundary(self, "C11 host leaf halt failed", [&] { self->hooks.halt_leaf(self->hooks.user,n); return SALTS_OK; });
  }
  static int bridge_tick_typed_leaf(void* user, const praktor_c11_leaf_request* req, salts_bt_node_status* status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 host leaf tick hook failed", [&] { return self->hooks.tick_typed_leaf(self->hooks.user,req,status); });
  }
  static void bridge_halt_typed_leaf(void* user, const praktor_c11_leaf_request* req) noexcept {
    auto* self = static_cast<Impl*>(user);
    (void)boundary(self, "C11 host leaf halt failed", [&] { self->hooks.halt_typed_leaf(self->hooks.user,req); return SALTS_OK; });
  }
  static int bridge_condition(void* user, size_t n, salts_bt_node_status* status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 condition evaluation failed", [&] { return self->hooks.condition(self->hooks.user,n,status); });
  }
  static int bridge_select_child(void* user, size_t n, size_t* child) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Switch selector failed", [&] { return self->hooks.select_child(self->hooks.user,n,child); });
  }
  static int bridge_resolve_dynamic_parameters(void* user, size_t n, salts_bt_node_parameters* params) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Parallel dynamic parameter resolver failed", [&] { return self->hooks.resolve_dynamic_parameters(self->hooks.user,n,params); });
  }
  static int bridge_parallel_enter(void* user, size_t n, size_t child, bool first) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Parallel enter hook failed", [&] { return self->hooks.parallel_enter(self->hooks.user,n,child,first); });
  }
  static int bridge_parallel_step(void* user, size_t n, size_t child, salts_bt_node_status status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Parallel step hook failed", [&] { return self->hooks.parallel_step(self->hooks.user,n,child,status); });
  }
  static int bridge_parallel_join(void* user, size_t n, salts_bt_node_status status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Parallel join hook failed", [&] { return self->hooks.parallel_join(self->hooks.user,n,status); });
  }
  static int bridge_parallel_finish(void* user, size_t n, size_t child, salts_bt_node_status status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Parallel finish hook failed", [&] { return self->hooks.parallel_finish(self->hooks.user,n,child,status); });
  }
  static void bridge_parallel_abort(void* user, size_t n, size_t child) noexcept {
    auto* self = static_cast<Impl*>(user);
    (void)boundary(self, "C11 Parallel abort hook failed", [&] { self->hooks.parallel_abort(self->hooks.user,n,child); return SALTS_OK; });
  }
  static int bridge_pipeline_prepare(void* user, size_t n, size_t child) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 PipelineSequence prepare hook failed", [&] { return self->hooks.pipeline_prepare(self->hooks.user,n,child); });
  }
  static int bridge_pipeline_complete(void* user, size_t n, size_t child, salts_bt_node_status status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 PipelineSequence complete hook failed", [&] { return self->hooks.pipeline_complete(self->hooks.user,n,child,status); });
  }
  static void bridge_pipeline_abort(void* user, size_t n, size_t child) noexcept {
    auto* self = static_cast<Impl*>(user);
    (void)boundary(self, "C11 PipelineSequence abort hook failed", [&] { self->hooks.pipeline_abort(self->hooks.user,n,child); return SALTS_OK; });
  }
  static int bridge_entry_changed(void* user, size_t n, bool* changed) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 EntryUpdated hook failed", [&] { return self->hooks.entry_changed(self->hooks.user,n,changed); });
  }
  static int bridge_queue_begin(void* user, size_t n, bool* item) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 ConsumeQueue begin hook failed", [&] { return self->hooks.queue_begin(self->hooks.user,n,item); });
  }
  static int bridge_queue_finish(void* user, size_t n, bool* more) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 ConsumeQueue finish hook failed", [&] { return self->hooks.queue_finish(self->hooks.user,n,more); });
  }
  static void bridge_queue_abort(void* user, size_t n) noexcept {
    auto* self = static_cast<Impl*>(user);
    (void)boundary(self, "C11 ConsumeQueue abort hook failed", [&] { self->hooks.queue_abort(self->hooks.user,n); return SALTS_OK; });
  }
  static int bridge_value_get(void* user, const char* key, char* value, size_t capacity, size_t* size) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 host value read failed", [&] { return self->hooks.value_get(self->hooks.value_user,key,value,capacity,size); }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int bridge_value_set(void* user, const char* key, const char* value) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 host value write failed", [&] { return self->hooks.value_set(self->hooks.value_user,key,value); }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int bridge_value_contains(void* user, const char* key, int* contains) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 host value contains failed", [&] { return self->hooks.value_contains(self->hooks.value_user,key,contains); }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int compatibilityGet(void* user, const char* key, char* output, size_t capacity, size_t* size) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 compatibility value read failed", [&] {
      if (!self->compatibility->has(key)) return static_cast<int>(PRAKTOR_C11_BT_VALUE_NOT_FOUND);
      const auto value = self->compatibility->get(key);
      *size = value.size();
      if (!output || capacity <= value.size()) return static_cast<int>(PRAKTOR_C11_BT_VALUE_BUFFER_TOO_SMALL);
      std::memcpy(output, value.data(), value.size()); output[value.size()] = '\0';
      return static_cast<int>(PRAKTOR_C11_BT_VALUE_OK);
    }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int compatibilityContains(void* user, const char* key, int* contains) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 compatibility value contains failed", [&] {
      *contains = self->compatibility->has(key) ? 1 : 0; return static_cast<int>(PRAKTOR_C11_BT_VALUE_OK);
    }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int compatibilitySetBytes(void* user, const char* key, const char* data, size_t size) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 compatibility value write failed", [&] {
      self->compatibility->set(key, std::string(data,size)); return static_cast<int>(PRAKTOR_C11_BT_VALUE_OK);
    }, PRAKTOR_C11_BT_VALUE_ERROR);
  }
  static int compatibilitySet(void* user, const char* key, const char* value) noexcept {
    return compatibilitySetBytes(user,key,value,std::strlen(value));
  }
  static int compatibilityTick(void* user, size_t node, salts_bt_node_status* status) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 action leaf failed", [&] {
      *status = self->compatibility->tick(*self->nodeAt(node)); return SALTS_OK;
    });
  }
  static int environment(void* user, const char* const** entries) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Process environment failed", [&] {
      self->environment_entries = self->compatibility->environment();
      self->environment_pointers.clear();
      for (const auto& value : self->environment_entries) self->environment_pointers.push_back(value.c_str());
      self->environment_pointers.push_back(nullptr);
      *entries = self->environment_entries.empty() ? nullptr : self->environment_pointers.data();
      return SALTS_OK;
    });
  }
  static int emit(void* user, const char* line, size_t size) noexcept {
    auto* self = static_cast<Impl*>(user);
    return boundary(self, "C11 Process stream callback failed", [&] {
      self->compatibility->emit(std::string(line,size)); return SALTS_OK;
    });
  }
  void validateHostContracts() const {
    if (hooks.halt_leaf != nullptr && hooks.halt_typed_leaf != nullptr)
      throw std::invalid_argument("Select exactly one C11 leaf halt callback");
    if (hooks.halt_typed_leaf != nullptr && hooks.tick_typed_leaf == nullptr)
      throw std::invalid_argument("Typed halt requires typed leaf dispatch");
    if (hooks.tick_leaf != nullptr && hooks.tick_typed_leaf != nullptr)
      throw std::invalid_argument("Select exactly one C11 leaf dispatcher");
    const bool has_value_get = hooks.value_get != nullptr;
    const bool has_value_set = hooks.value_set != nullptr;
    const bool has_value_contains = hooks.value_contains != nullptr;
    if ((has_value_get || has_value_set || has_value_contains) &&
        (!has_value_get || !has_value_set || !has_value_contains ||
         hooks.value_user == nullptr)) {
      throw std::invalid_argument(
          "C11 host value store requires get/set/contains callbacks and value_user");
    }
    if (!compatibility &&
        (!has_value_get || !hasLeafDispatch() || !hasLeafHalt())) {
      throw std::invalid_argument(
          "Host-only C11 session requires host value get/set/contains and tick/halt hooks");
    }
    if (has_value_get &&
        (!hasLeafDispatch() || !hasLeafHalt())) {
      throw std::invalid_argument(
          "C11 host value store requires explicit host leaf dispatch and halt hooks");
    }
    for (size_t i = 0u; i < compiled_plan.nodes.size(); ++i) {
      const auto& desc = compiled_plan.nodes[i];
      const auto* source = nodeAt(i);
      if (source == nullptr) {
        throw std::invalid_argument("TreePlan has no source AST node");
      }

      if (hasLeafDispatch() && !hasLeafHalt()) {
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

  Impl(const actions::Tree& source, std::shared_ptr<detail::SessionCompatibility> adapter,
       const salts_bt_tree_config& limits, const SubTreeResolver& resolver,
       const praktor_c11_bt_host* supplied, cflow_process_retainer* retainer, uint32_t close_budget)
      : tree(expandSubTrees(source,resolver,limits)), compiled_plan(compileTreePlan(tree,limits)),
        compatibility(std::move(adapter)) {
    if (supplied) hooks = *supplied;
    validateHostContracts();
    praktor_c11_session_config config;
    praktor_c11_session_config_init(&config);
    config.tree = limits; config.retainer = retainer; config.shell_close_budget_ms = close_budget;
    config.host.user = this; config.host.value_user = this;
    if (hooks.tick_leaf) config.host.tick_leaf = bridge_tick_leaf;
    if (hooks.halt_leaf) config.host.halt_leaf = bridge_halt_leaf;
    if (hooks.tick_typed_leaf) config.host.tick_typed_leaf = bridge_tick_typed_leaf;
    if (hooks.halt_typed_leaf) config.host.halt_typed_leaf = bridge_halt_typed_leaf;
    if (hooks.condition) config.host.condition = bridge_condition;
    if (hooks.select_child) config.host.select_child = bridge_select_child;
    if (hooks.resolve_dynamic_parameters) config.host.resolve_dynamic_parameters = bridge_resolve_dynamic_parameters;
    if (hooks.parallel_enter) config.host.parallel_enter = bridge_parallel_enter;
    if (hooks.parallel_step) config.host.parallel_step = bridge_parallel_step;
    if (hooks.parallel_join) config.host.parallel_join = bridge_parallel_join;
    if (hooks.parallel_finish) config.host.parallel_finish = bridge_parallel_finish;
    if (hooks.parallel_abort) config.host.parallel_abort = bridge_parallel_abort;
    if (hooks.pipeline_prepare) config.host.pipeline_prepare = bridge_pipeline_prepare;
    if (hooks.pipeline_complete) config.host.pipeline_complete = bridge_pipeline_complete;
    if (hooks.pipeline_abort) config.host.pipeline_abort = bridge_pipeline_abort;
    if (hooks.entry_changed) config.host.entry_changed = bridge_entry_changed;
    if (hooks.queue_begin) config.host.queue_begin = bridge_queue_begin;
    if (hooks.queue_finish) config.host.queue_finish = bridge_queue_finish;
    if (hooks.queue_abort) config.host.queue_abort = bridge_queue_abort;
    if (hooks.value_get) config.host.value_get = bridge_value_get;
    if (hooks.value_set) config.host.value_set = bridge_value_set;
    if (hooks.value_contains) config.host.value_contains = bridge_value_contains;
    if (compatibility) {
      config.compatibility_values.value_get = compatibilityGet;
      config.compatibility_values.value_set = compatibilitySet;
      config.compatibility_values.value_contains = compatibilityContains;
      config.compatibility_values.value_user = this;
      config.compatibility_tick = compatibilityTick; config.compatibility_set_bytes = compatibilitySetBytes;
      config.compatibility_user = this;
      config.environment = environment; config.emit = emit; config.process_user = this;
    }
    std::vector<praktor_c11_session_node> nodes(compiled_plan.nodes.size());
    std::vector<std::vector<praktor_c11_leaf_parameter>> parameters(nodes.size());
    std::vector<std::vector<size_t>> sizes(nodes.size());
    for (size_t n = 0; n < nodes.size(); ++n) {
      const auto* source_node = nodeAt(n);
      AstParameters ports(*source_node);
      parameters[n] = std::move(ports.values); sizes[n] = std::move(ports.sizes);
      nodes[n] = {compiled_plan.nodes[n],source_node->id.c_str(),parameters[n].data(),parameters[n].size(),sizes[n].data(),
                  static_cast<praktor_c11_terminal_action>(compiled_plan.terminal_actions[n])};
    }
    const int status = praktor_c11_session_init(&runtime,&config,nodes.data(),nodes.size());
    if (status != SALTS_OK) throw std::runtime_error("C11 Session initialization failed with status " + std::to_string(status));
  }
  ~Impl() { if (praktor_c11_session_destroy(&runtime) != SALTS_OK) std::terminate(); }
};
TreeExecutorSession::TreeExecutorSession(
    const actions::Tree& tree,
    const SubTreeResolver& subtree_resolver,
    const salts_bt_tree_config& limits,
    const praktor_c11_bt_host& host_hooks,
    cflow_process_retainer* recovery_owner,
    uint32_t shell_close_budget_ms)
    : TreeExecutorSession(tree, subtree_resolver, limits, &host_hooks,
                          recovery_owner, shell_close_budget_ms, nullptr) {}

TreeExecutorSession::TreeExecutorSession(
    const actions::Tree& tree,
    const SubTreeResolver& subtree_resolver,
    const salts_bt_tree_config& limits,
    const praktor_c11_bt_host* host_hooks,
    cflow_process_retainer* recovery_owner,
    uint32_t shell_close_budget_ms,
    std::shared_ptr<detail::SessionCompatibility> compatibility)
    : impl_(std::make_unique<Impl>(tree, std::move(compatibility), limits,
                                  subtree_resolver, host_hooks, recovery_owner,
                                  shell_close_budget_ms)) {}

TreeExecutorSession::~TreeExecutorSession() = default;
int TreeExecutorSession::tick(salts_bt_node_status* status) {
  if (!impl_) return SALTS_EINVAL;
  impl_->callback_error.clear(); return impl_->finish(praktor_c11_session_tick(&impl_->runtime,status));
}
int TreeExecutorSession::reset() {
  if (!impl_) return SALTS_EINVAL;
  impl_->callback_error.clear(); return impl_->finish(praktor_c11_session_reset(&impl_->runtime));
}
int TreeExecutorSession::cancel() {
  if (!impl_) return SALTS_EINVAL;
  impl_->callback_error.clear(); return impl_->finish(praktor_c11_session_cancel(&impl_->runtime));
}
int TreeExecutorSession::validate() const { return impl_ ? praktor_c11_session_validate(&impl_->runtime) : SALTS_EINVAL; }
const actions::Node* TreeExecutorSession::sourceNode(size_t index) const noexcept { return impl_ ? impl_->nodeAt(index) : nullptr; }
const TreePlan& TreeExecutorSession::plan() const noexcept { return impl_->compiled_plan; }
const std::string& TreeExecutorSession::lastError() const noexcept {
  static const std::string empty; return impl_ ? impl_->last_error : empty;
}
} // namespace actions::c11
