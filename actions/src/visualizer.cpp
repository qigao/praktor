#include "actions/visualizer.hpp"
#include <sstream>

namespace actions {

std::string Visualizer::toDot(const Tree &tree) {
  std::string output;
  output += "digraph " + tree.name + " {\n";
  output += "  rankdir=TB;\n";
  output += "  node [shape=box, style=rounded];\n\n";

  int node_id = 0;
  nodeToDot(tree.root, output, node_id);

  output += "}\n";
  return output;
}

std::string Visualizer::toDot(const Program &program) {
  std::ostringstream oss;

  for (const auto &tree : program.trees) {
    oss << toDot(tree) << "\n\n";
  }

  return oss.str();
}

void Visualizer::nodeToDot(const Node &node, std::string &output, int &node_id, int parent_id) {
  int current_id = node_id++;

  std::ostringstream oss;

  // Node definition
  oss << "  node" << current_id << " [label=\"" << node.id;

  // Add parameters
  if (!node.params.empty()) {
    oss << "\\n";
    for (const auto &[key, value] : node.params) {
      oss << key << "=";
      if (std::holds_alternative<std::string>(value)) {
        oss << std::get<std::string>(value);
      } else if (std::holds_alternative<double>(value)) {
        oss << std::get<double>(value);
      } else if (std::holds_alternative<bool>(value)) {
        oss << (std::get<bool>(value) ? "true" : "false");
      }
      oss << "\\n";
    }
  }

  oss << "\"";

  // Node styling based on type
  std::string color = getNodeColor(node.id);
  std::string shape = getNodeShape(node.id);

  oss << ", fillcolor=\"" << color << "\", style=\"filled,rounded\"";
  oss << ", shape=" << shape;
  oss << "];\n";

  output += oss.str();

  // Edge from parent
  if (parent_id >= 0) {
    output +=
        "  node" + std::to_string(parent_id) + " -> node" + std::to_string(current_id) + ";\n";
  }

  // Process children
  for (const auto &child : node.children) {
    nodeToDot(child, output, node_id, current_id);
  }
}

std::string Visualizer::toMermaid(const Tree &tree) {
  std::ostringstream oss;
  oss << "graph TD\n";

  int node_id = 0;
  std::string output;
  nodeToMermaid(tree.root, output, node_id);

  oss << output;
  return oss.str();
}

std::string Visualizer::toMermaid(const Program &program) {
  std::ostringstream oss;

  for (const auto &tree : program.trees) {
    oss << "subgraph " << tree.name << "\n";
    oss << toMermaid(tree);
    oss << "end\n\n";
  }

  return oss.str();
}

void Visualizer::nodeToMermaid(const Node &node, std::string &output, int &node_id, int parent_id) {
  int current_id = node_id++;

  std::ostringstream oss;

  // Node definition
  std::string shape_start, shape_end;
  if (node.id == "Sequence" || node.id == "Fallback" || node.id == "Parallel") {
    shape_start = "[";
    shape_end = "]";
  } else if (node.id.find("Reactive") != std::string::npos) {
    shape_start = "[[";
    shape_end = "]]";
  } else if (node.id == "Retry" || node.id == "Inverter" || node.id == "ForceSuccess" ||
             node.id == "ForceFailure" || node.id == "Repeat") {
    shape_start = "{";
    shape_end = "}";
  } else {
    shape_start = "(";
    shape_end = ")";
  }

  oss << "  N" << current_id << shape_start << node.id;

  // Add key parameters
  if (!node.params.empty()) {
    oss << "<br/>";
    int count = 0;
    for (const auto &[key, value] : node.params) {
      if (count++ > 2)
        break; // Limit to 3 params for readability
      oss << key << "=";
      if (std::holds_alternative<std::string>(value)) {
        oss << std::get<std::string>(value);
      } else if (std::holds_alternative<double>(value)) {
        oss << std::get<double>(value);
      } else if (std::holds_alternative<bool>(value)) {
        oss << (std::get<bool>(value) ? "true" : "false");
      }
      oss << "<br/>";
    }
  }

  oss << shape_end << "\n";

  output += oss.str();

  // Edge from parent
  if (parent_id >= 0) {
    output += "  N" + std::to_string(parent_id) + " --> N" + std::to_string(current_id) + "\n";
  }

  // Process children
  for (const auto &child : node.children) {
    nodeToMermaid(child, output, node_id, current_id);
  }
}

std::string Visualizer::getNodeShape(const std::string &node_type) {
  // Control nodes
  if (node_type == "Sequence" || node_type == "Fallback" || node_type == "Parallel" ||
      node_type == "ReactiveSequence" || node_type == "ReactiveFallback") {
    return "box";
  }

  // Decorators
  if (node_type == "Retry" || node_type == "Inverter" || node_type == "ForceSuccess" ||
      node_type == "ForceFailure" || node_type == "Repeat" || node_type == "Timeout" ||
      node_type == "Delay") {
    return "diamond";
  }

  // Actions/Conditions
  return "ellipse";
}

std::string Visualizer::getNodeColor(const std::string &node_type) {
  // Control nodes - blue
  if (node_type == "Sequence" || node_type == "Fallback" || node_type == "Parallel" ||
      node_type == "ReactiveSequence" || node_type == "ReactiveFallback" || node_type == "Switch" ||
      node_type == "WhileDo" || node_type == "IfThenElse") {
    return "#87CEEB";
  }

  // Decorators - yellow
  if (node_type == "Retry" || node_type == "Inverter" || node_type == "ForceSuccess" ||
      node_type == "ForceFailure" || node_type == "Repeat" || node_type == "Timeout" ||
      node_type == "Delay") {
    return "#FFD700";
  }

  // SubTree - purple
  if (node_type == "SubTree") {
    return "#DDA0DD";
  }

  // Actions/Conditions - green
  return "#90EE90";
}

} // namespace actions
