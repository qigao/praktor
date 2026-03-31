#pragma once

#include "core/ast.hpp"
#include <string>

namespace btdsl {

class Visualizer {
public:
  // Export tree to DOT format (GraphViz)
  static std::string toDot(const Tree& tree);
  static std::string toDot(const Program& program);

  // Export tree to Mermaid format
  static std::string toMermaid(const Tree& tree);
  static std::string toMermaid(const Program& program);

private:
  static void nodeToDot(const Node& node, std::string& output, int& node_id, int parent_id = -1);
  static void nodeToMermaid(const Node& node, std::string& output, int& node_id, int parent_id = -1);
  static std::string getNodeShape(const std::string& node_type);
  static std::string getNodeColor(const std::string& node_type);
};

} // namespace btdsl
