#include "yml/task_types.hpp"
#include <algorithm>
#include <cctype>

namespace {

std::string normalizeNodeLookup(std::string value) {
  value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
  return value;
}

} // namespace

// NODE_REGISTRY: Registry of all supported orchestration node types
const std::unordered_map<std::string, OrchNodeSpec> NODE_REGISTRY = {
  // Control Flow Nodes
  {"Sequence", {"Sequence", true, {}, {}, true}},
  {"Fallback", {"Fallback", true, {}, {}, true}},
  {"Selector", {"Selector", true, {}, {}, true}},
  {"Parallel", {"Parallel", true, {}, {}, true}},
  {"ReactiveSequence", {"ReactiveSequence", true, {}, {}, true}},
  {"Switch", {"Switch", true, {"variable"}, {}, true}},
  {"WhileDo", {"WhileDo", true, {}, {"max_iterations"}, true}},
  {"IfThenElse", {"IfThenElse", true, {}, {}, true}},
  {"Inverter", {"Inverter", true, {}, {}, true}},
  {"ForceSuccess", {"ForceSuccess", true, {}, {}, true}},
  {"ForceFailure", {"ForceFailure", true, {}, {}, true}},
  {"Repeat", {"Repeat", true, {}, {"num_cycles"}, true}},
  {"Timeout", {"Timeout", true, {}, {"timeout_ms"}, true}},
  {"Delay", {"Delay", true, {}, {"delay_ms"}, true}},
  {"SubTree", {"SubTree", false, {"tree"}, {}, false}},
  
  // Advanced Decorators
  {"KeepRunningUntilFailure", {"KeepRunningUntilFailure", true, {}, {"max_iterations"}, true}},
  {"RunOnce", {"RunOnce", true, {}, {}, true}},
  {"ConsumeQueue", {"ConsumeQueue", true, {"queue_key"}, {"item_key"}, true}},
  
  // Extended Decorators
  {"Precondition", {"Precondition", true, {"condition"}, {}, true}},
  {"EntryUpdated", {"EntryUpdated", true, {"watch_key"}, {}, true}},
  
  // Extended Control Flow
  {"PipelineSequence", {"PipelineSequence", true, {}, {"input_key", "output_key"}, true}},
  
  // Leaf Nodes
  {"Shell", {"Shell", false, {"cmd"}, {"output_key", "stderr_key", "exit_code_key", "working_dir", "timeout", "stream_output"}, false}},
  {"ParseJson", {"ParseJson", false, {"input_key"}, {"path", "output_key"}, false}},
  {"ParseRegex", {"ParseRegex", false, {"input_key", "pattern"}, {"capture_group", "output_key"}, false}},
  {"ParseLines", {"ParseLines", false, {"input_key"}, {"filter", "output_key"}, false}},
  {"ParseKeyValue", {"ParseKeyValue", false, {"input_key"}, {"delimiter", "line_separator", "output_key"}, false}},
  {"CheckExitCode", {"CheckExitCode", false, {"expected"}, {"input_key"}, false}},
  {"WaitEvent", {"WaitEvent", false, {"event"}, {"timeout"}, false}},
  {"Sleep", {"Sleep", false, {"duration"}, {}, false}},
  {"FileExists", {"FileExists", false, {"path"}, {"output_key", "fail_if_missing"}, false}},
  {"SetVariable", {"SetVariable", false, {"key"}, {"value", "from"}, false}}
};

const OrchNodeSpec* findNodeSpec(const std::string& name) {
  // Try exact match first
  auto it = NODE_REGISTRY.find(name);
  if (it != NODE_REGISTRY.end()) {
    return &it->second;
  }

  // Accept snake_case YAML keys and case-insensitive spellings.
  std::string normalized_name = normalizeNodeLookup(name);

  for (const auto& [node_name, spec] : NODE_REGISTRY) {
    if (normalized_name == normalizeNodeLookup(node_name)) {
      return &spec;
    }
  }
  return nullptr;
}

// OrchNode helper method implementations
bool OrchNode::isControlNode() const {
  auto* spec = findNodeSpec(type);
  return spec && spec->isControl;
}

bool OrchNode::isLeafNode() const {
  auto* spec = findNodeSpec(type);
  return spec && !spec->isControl;
}
