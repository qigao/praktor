#include "core/executor.hpp"
#include "tree_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace actions::c11 {
namespace {

using Node = actions::Node;
using Value = actions::Value;
using Blackboard = actions::Blackboard;

const Value *findParam(const Node &node, const char *key) {
  const auto it = node.params.find(key);
  return it == node.params.end() ? nullptr : &it->second;
}

bool isRuntimeSubstitutedParam(const Node &node, const char *key) {
  const Value *value = findParam(node, key);
  if (value == nullptr || !std::holds_alternative<std::string>(*value))
    return false;
  const std::string &raw = std::get<std::string>(*value);
  return raw.size() >= 2u && raw.front() == '{' && raw.back() == '}';
}

std::string valueToText(const Value &value, const Blackboard &blackboard,
                        const std::string &owner, const char *key,
                        bool bare_string_may_name_key = false) {
  if (std::holds_alternative<std::string>(value)) {
    const std::string &raw = std::get<std::string>(value);
    if (raw.size() >= 2u && raw.front() == '{' && raw.back() == '}') {
      const std::string name = raw.substr(1u, raw.size() - 2u);
      if (!blackboard.has(name)) {
        throw std::invalid_argument("Parameter '" + std::string(key) +
                                    "' on node '" + owner +
                                    "' references missing blackboard key '" + name +
                                    "'");
      }
      return blackboard.get(name);
    }
    if (bare_string_may_name_key && blackboard.has(raw)) {
      return blackboard.get(raw);
    }
    return raw;
  }
  if (std::holds_alternative<double>(value)) {
    const double numeric = std::get<double>(value);
    if (!std::isfinite(numeric)) {
      throw std::invalid_argument("Parameter '" + std::string(key) +
                                  "' on node '" + owner + "' must be finite");
    }
    return std::to_string(numeric);
  }
  return std::get<bool>(value) ? "true" : "false";
}

int64_t parseIntegerText(const std::string &raw, const Node &node,
                         const char *key) {
  const double lower = static_cast<double>((std::numeric_limits<int>::min)());
  const double upper = static_cast<double>((std::numeric_limits<int>::max)());
  try {
    size_t consumed = 0u;
    const long long value = std::stoll(raw, &consumed, 10);
    if (consumed == raw.size() &&
        value >= (std::numeric_limits<int>::min)() &&
        value <= (std::numeric_limits<int>::max)()) {
      return static_cast<int64_t>(value);
    }
  } catch (const std::exception &) {
    // The legacy Executor also accepts integral floating-point text.
  }

  try {
    size_t consumed = 0u;
    const double numeric = std::stod(raw, &consumed);
    const double fractional = numeric - std::trunc(numeric);
    if (consumed != raw.size() || !std::isfinite(numeric) ||
        std::abs(fractional) > 0.0 || numeric < lower || numeric > upper) {
      throw std::invalid_argument("not an in-range integral number");
    }
    return static_cast<int64_t>(numeric);
  } catch (const std::exception &) {
    throw std::invalid_argument("Parameter '" + std::string(key) +
                                "' on node '" + node.id +
                                "' must be a 32-bit integer, got '" + raw + "'");
  }
}

int64_t readIntegerParam(const Node &node, const Blackboard &blackboard,
                         const char *key, int64_t default_value,
                         bool required = false,
                         bool bare_string_may_name_key = false) {
  const Value *value = findParam(node, key);
  if (value == nullptr) {
    if (required) {
      throw std::invalid_argument("Node '" + node.id + "' requires parameter '" +
                                  key + "'");
    }
    return default_value;
  }

  if (std::holds_alternative<double>(*value)) {
    const double numeric = std::get<double>(*value);
    const double fractional = numeric - std::trunc(numeric);
    // Use an ordered comparison rather than ==/!= on floating-point values:
    // this project treats -Wfloat-equal as an error.
    if (!std::isfinite(numeric) || std::abs(fractional) > 0.0 ||
        numeric < static_cast<double>((std::numeric_limits<int>::min)()) ||
        numeric > static_cast<double>((std::numeric_limits<int>::max)())) {
      throw std::invalid_argument("Parameter '" + std::string(key) +
                                  "' on node '" + node.id +
                                  "' must be an integer");
    }
    return static_cast<int64_t>(numeric);
  }
  if (std::holds_alternative<bool>(*value)) {
    throw std::invalid_argument("Parameter '" + std::string(key) +
                                "' on node '" + node.id +
                                "' must be an integer, not a boolean");
  }
  return parseIntegerText(valueToText(*value, blackboard, node.id, key,
                                      bare_string_may_name_key),
                          node, key);
}

uint64_t millisecondsToNanoseconds(int64_t milliseconds, const Node &node,
                                   const char *key) {
  if (milliseconds < 0) {
    throw std::invalid_argument("Parameter '" + std::string(key) +
                                "' on node '" + node.id +
                                "' cannot be negative");
  }
  constexpr uint64_t kNsPerMs = 1000000u;
  const auto value = static_cast<uint64_t>(milliseconds);
  if (value > std::numeric_limits<uint64_t>::max() / kNsPerMs) {
    throw std::invalid_argument("Parameter '" + std::string(key) +
                                "' on node '" + node.id + "' overflows nanoseconds");
  }
  return value * kNsPerMs;
}

void validateNodeArity(const Node &node, salts_bt_node_kind kind) {
  const size_t count = node.children.size();
  const bool scalar_condition = findParam(node, "condition") != nullptr;
  switch (kind) {
  case SALTS_BT_NODE_INVERTER:
  case SALTS_BT_NODE_FORCE_SUCCESS:
  case SALTS_BT_NODE_FORCE_FAILURE:
  case SALTS_BT_NODE_RETRY:
  case SALTS_BT_NODE_REPEAT:
  case SALTS_BT_NODE_TIMEOUT:
  case SALTS_BT_NODE_DELAY:
  case SALTS_BT_NODE_RUN_ONCE:
  case SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE:
  case SALTS_BT_NODE_ENTRY_UPDATED:
  case SALTS_BT_NODE_CONSUME_QUEUE:
    if (count != 1u) {
      throw std::invalid_argument("Node '" + node.id +
                                  "' must have exactly one child");
    }
    break;
  case SALTS_BT_NODE_IF_THEN_ELSE:
    if (scalar_condition ? (count < 1u || count > 2u)
                         : (count < 2u || count > 3u)) {
      throw std::invalid_argument(
          "IfThenElse must have 1-2 children with a scalar condition, or 2-3 with an in-tree condition");
    }
    break;
  case SALTS_BT_NODE_PRECONDITION:
    if (count != 1u) {
      throw std::invalid_argument("Precondition must have exactly one child");
    }
    break;
  case SALTS_BT_NODE_WHILE_DO:
    if (count != (scalar_condition ? 1u : 2u)) {
      throw std::invalid_argument(
          "WhileDo must have one child with a scalar condition, or two with an in-tree condition");
    }
    break;
  case SALTS_BT_NODE_PARALLEL:
  case SALTS_BT_NODE_REACTIVE_SEQUENCE:
    /* The legacy Executor defines these empty controls as SUCCESS no-ops. */
    break;
  case SALTS_BT_NODE_REACTIVE_FALLBACK:
    /* The legacy Executor defines an empty ReactiveFallback as FAILURE. */
    break;
  case SALTS_BT_NODE_SWITCH:
    /* Empty Switch still resolves variable before returning FAILURE. */
    break;
  default:
    break;
  }
}

salts_bt_node_kind mapNodeKind(const Node &node) {
  if (node.id == "Sequence") return SALTS_BT_NODE_SEQUENCE;
  if (node.id == "Fallback" || node.id == "Selector")
    return SALTS_BT_NODE_FALLBACK;
  if (node.id == "Parallel") return SALTS_BT_NODE_PARALLEL;
  if (node.id == "ReactiveSequence") return SALTS_BT_NODE_REACTIVE_SEQUENCE;
  if (node.id == "ReactiveFallback") return SALTS_BT_NODE_REACTIVE_FALLBACK;
  if (node.id == "Retry") return SALTS_BT_NODE_RETRY;
  if (node.id == "Switch") return SALTS_BT_NODE_SWITCH;
  if (node.id == "IfThenElse") return SALTS_BT_NODE_IF_THEN_ELSE;
  if (node.id == "Precondition") return SALTS_BT_NODE_PRECONDITION;
  if (node.id == "WhileDo") return SALTS_BT_NODE_WHILE_DO;
  if (node.id == "Inverter") return SALTS_BT_NODE_INVERTER;
  if (node.id == "ForceSuccess") return SALTS_BT_NODE_FORCE_SUCCESS;
  if (node.id == "ForceFailure") return SALTS_BT_NODE_FORCE_FAILURE;
  if (node.id == "Repeat") return SALTS_BT_NODE_REPEAT;
  if (node.id == "Timeout") return SALTS_BT_NODE_TIMEOUT;
  if (node.id == "Delay") return SALTS_BT_NODE_DELAY;
  if (node.id == "KeepRunningUntilFailure")
    return SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE;
  if (node.id == "RunOnce") return SALTS_BT_NODE_RUN_ONCE;
  if (node.id == "PipelineSequence") return SALTS_BT_NODE_PIPELINE_SEQUENCE;
  if (node.id == "EntryUpdated") return SALTS_BT_NODE_ENTRY_UPDATED;
  if (node.id == "ConsumeQueue") return SALTS_BT_NODE_CONSUME_QUEUE;

  if (node.id == "SubTree") {
    throw std::invalid_argument(
        "C11 action-tree plan compilation does not yet support SubTree expansion");
  }
  if (!node.children.empty()) {
    throw std::invalid_argument("Unsupported control node '" + node.id +
                                "' has children; refusing to reinterpret it as a leaf");
  }
  return SALTS_BT_NODE_LEAF;
}

void appendNode(TreePlan &plan, const Node &node, size_t parent_index,
                size_t depth, const Blackboard &blackboard,
                const salts_bt_tree_config &limits) {
  if (node.id.empty()) {
    throw std::invalid_argument("Action tree contains a node with an empty type");
  }
  if (depth > limits.max_depth) {
    throw std::length_error("Action tree exceeds configured max_depth");
  }
  if (plan.nodes.size() >= limits.max_nodes) {
    throw std::length_error("Action tree exceeds configured max_nodes");
  }

  const size_t node_index = plan.nodes.size();
  const salts_bt_node_kind kind = mapNodeKind(node);
  validateNodeArity(node, kind);
  praktor_c11_bt_node_desc desc{};
  bool skip_children = false;
  TreePlanTerminalAction terminal_action = TreePlanTerminalAction::Runtime;
  desc.parent_index = parent_index;
  desc.kind = kind;

  switch (kind) {
  case SALTS_BT_NODE_IF_THEN_ELSE:
    desc.condition_callback = findParam(node, "condition") != nullptr;
    break;
  case SALTS_BT_NODE_PRECONDITION:
    if (findParam(node, "condition") == nullptr) {
      throw std::invalid_argument(
          "Precondition requires a scalar 'condition' parameter in the current Praktor AST");
    }
    desc.condition_callback = true;
    break;
  case SALTS_BT_NODE_WHILE_DO: {
    const bool scalar_condition = findParam(node, "condition") != nullptr;
    const size_t expected_children = scalar_condition ? 1u : 2u;
    if (node.children.size() != expected_children) {
      throw std::invalid_argument(
          "WhileDo must have exactly 1 child with a scalar condition, or 2 children with an in-tree condition");
    }
    const bool dynamic_iterations =
        isRuntimeSubstitutedParam(node, "max_iterations");
    /*
     * The concrete limit is refreshed at tick time. Keep the construction
     * placeholder within the configured runtime budget so a smaller host limit
     * does not reject a valid late-bound Blackboard value before the first tick.
     */
    const int64_t iterations = dynamic_iterations
        ? static_cast<int64_t>(
              (std::min)(static_cast<size_t>(100), limits.max_control_iterations))
        : readIntegerParam(node, blackboard, "max_iterations", 100);
    if (iterations < 0 ||
        static_cast<uint64_t>(iterations) > limits.max_control_iterations) {
      throw std::invalid_argument(
          "WhileDo max_iterations must be non-negative and within the tree limit");
    }
    if (dynamic_iterations)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
    if (iterations == 0 && !dynamic_iterations) {
      /*
       * Salts rejects a zero-limit WhileDo constructor, but the legacy
       * Executor validates child arity and then returns SUCCESS without
       * evaluating condition or body. Lower that static no-op into a synthetic leaf.
       */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Success;
    } else {
      desc.condition_callback = scalar_condition;
      /* Dynamic zero is applied on the first tick, after tree construction. */
      desc.limit = static_cast<size_t>(iterations > 0 ? iterations : 1);
    }
    break;
  }
  case SALTS_BT_NODE_REACTIVE_FALLBACK:
    if (node.children.empty()) {
      /* Legacy ReactiveFallback returns FAILURE when no child can succeed. */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Failure;
    }
    break;
  case SALTS_BT_NODE_SWITCH:
    if (findParam(node, "variable") == nullptr) {
      throw std::invalid_argument("Switch requires parameter 'variable'");
    }
    if (node.children.empty()) {
      /*
       * The legacy Executor resolves/parses the selector, then returns FAILURE
       * because no child can be selected. The session performs selector
       * resolution at tick time so Blackboard changes retain runtime semantics.
       */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::SwitchFailure;
    }
    break;
  case SALTS_BT_NODE_PARALLEL:
    if (node.children.empty()) {
      /* Legacy Parallel with no branches completes successfully. */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Success;
    } else {
      desc.parallel_hooks = true;
    }
    break;
  case SALTS_BT_NODE_REACTIVE_SEQUENCE:
    if (node.children.empty()) {
      /* Legacy ReactiveSequence with no children completes successfully. */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Success;
    }
    break;
  case SALTS_BT_NODE_RETRY: {
    const bool dynamic_attempts =
        isRuntimeSubstitutedParam(node, "num_attempts");
    const int64_t attempts = dynamic_attempts
        ? static_cast<int64_t>(
              (std::min)(static_cast<size_t>(3), limits.max_control_iterations))
        : readIntegerParam(node, blackboard, "num_attempts", 3);
    if (attempts < 0 ||
        static_cast<uint64_t>(attempts) > limits.max_control_iterations) {
      throw std::invalid_argument(
          "Retry num_attempts must be non-negative and within the tree limit");
    }
    if (dynamic_attempts)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
    if (attempts == 0 && !dynamic_attempts) {
      /* Static zero attempts means FAILURE; validate child arity before lowering. */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Failure;
    } else {
      desc.limit = static_cast<size_t>(attempts > 0 ? attempts : 1);
    }
    break;
  }
  case SALTS_BT_NODE_REPEAT: {
    const bool dynamic_count =
        isRuntimeSubstitutedParam(node, "num_cycles");
    const int64_t count = dynamic_count
        ? 0
        : readIntegerParam(node, blackboard, "num_cycles", 0, true);
    if (count < 0 ||
        static_cast<uint64_t>(count) > limits.max_control_iterations) {
      throw std::invalid_argument("Repeat num_cycles must be non-negative and within the tree limit");
    }
    if (dynamic_count)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
    desc.limit = static_cast<size_t>(count);
    break;
  }
  case SALTS_BT_NODE_KEEP_RUNNING_UNTIL_FAILURE: {
    if (node.children.size() != 1u) {
      throw std::invalid_argument(
          "KeepRunningUntilFailure must have exactly 1 child");
    }
    const bool dynamic_count =
        isRuntimeSubstitutedParam(node, "max_iterations");
    /* As with WhileDo, the dynamic value is resolved at tick time. */
    const int64_t count = dynamic_count
        ? static_cast<int64_t>(
              (std::min)(static_cast<size_t>(1000), limits.max_control_iterations))
        : readIntegerParam(node, blackboard, "max_iterations", 1000);
    if (count < 0 ||
        static_cast<uint64_t>(count) > limits.max_control_iterations) {
      throw std::invalid_argument("KeepRunningUntilFailure max_iterations must be non-negative and within the tree limit");
    }
    if (dynamic_count)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_LIMIT;
    if (count == 0 && !dynamic_count) {
      /* Static zero validates one child, executes zero iterations, then fails. */
      desc.kind = SALTS_BT_NODE_LEAF;
      skip_children = true;
      terminal_action = TreePlanTerminalAction::Failure;
    } else {
      desc.limit = static_cast<size_t>(count > 0 ? count : 1);
    }
    break;
  }
  case SALTS_BT_NODE_TIMEOUT: {
    const bool dynamic_timeout = isRuntimeSubstitutedParam(node, "timeout_ms");
    if (dynamic_timeout)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS;
    const int64_t timeout_ms = dynamic_timeout
        ? 1000
        : readIntegerParam(node, blackboard, "timeout_ms", 1000);
    desc.duration_ns = millisecondsToNanoseconds(timeout_ms, node, "timeout_ms");
    break;
  }
  case SALTS_BT_NODE_DELAY: {
    const bool dynamic_delay = isRuntimeSubstitutedParam(node, "delay_ms");
    if (dynamic_delay)
      desc.dynamic_parameter_mask |= SALTS_BT_DYNAMIC_PARAMETER_DURATION_NS;
    const int64_t delay_ms = dynamic_delay
        ? 100
        : readIntegerParam(node, blackboard, "delay_ms", 100);
    desc.duration_ns = millisecondsToNanoseconds(delay_ms, node, "delay_ms");
    break;
  }
  case SALTS_BT_NODE_PIPELINE_SEQUENCE:
    break;
  case SALTS_BT_NODE_ENTRY_UPDATED:
    if (findParam(node, "watch_key") == nullptr) {
      throw std::invalid_argument("EntryUpdated requires parameter 'watch_key'");
    }
    break;
  case SALTS_BT_NODE_CONSUME_QUEUE:
    /* queue_key defaults to "task_queue", matching actions::Executor. */
    break;
  default:
    break;
  }

  plan.nodes.push_back(desc);
  plan.source_nodes.push_back(&node);
  plan.terminal_actions.push_back(terminal_action);

  if (skip_children) return;
  for (const Node &child : node.children) {
    appendNode(plan, child, node_index, depth + 1u, blackboard, limits);
  }
}

} // namespace

int64_t resolveIntegerParameter(const actions::Node &node,
                                const actions::Blackboard &blackboard,
                                const char *key, int64_t default_value,
                                bool required) {
  return readIntegerParam(node, blackboard, key, default_value, required);
}

uint64_t resolveDurationParameter(const actions::Node &node,
                                  const actions::Blackboard &blackboard,
                                  const char *key, int64_t default_ms) {
  return millisecondsToNanoseconds(
      readIntegerParam(node, blackboard, key, default_ms), node, key);
}

TreePlan compileTreePlan(const actions::Tree &tree,
                         const actions::Blackboard &blackboard,
                         const salts_bt_tree_config &limits) {
  if (limits.max_nodes == 0u || limits.max_depth == 0u ||
      limits.max_control_iterations == 0u ||
      limits.max_depth > SALTS_BT_MAX_DEPTH ||
      limits.max_control_iterations > SALTS_BT_MAX_CONTROL_ITERATIONS) {
    throw std::invalid_argument("Invalid Salts BehaviorTree limits");
  }
  TreePlan plan;
  plan.nodes.reserve(limits.max_nodes);
  plan.source_nodes.reserve(limits.max_nodes);
  appendNode(plan, tree.root, PRAKTOR_C11_BT_NO_PARENT, 1u, blackboard,
             limits);
  return plan;
}

} // namespace actions::c11
