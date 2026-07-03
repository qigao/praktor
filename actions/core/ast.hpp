#pragma once

#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace actions {

// Value types supported in DSL
using Value = std::variant<std::string, double, bool>;

// AST Node representing an orchestration node
struct Node {
  std::string id;                      // Node identifier (e.g., "Sequence", "MoveRobot")
  std::map<std::string, Value> params; // Node parameters/ports
  std::vector<Node> children;          // Child nodes
  int line = 0;                        // Source line number for error reporting
  int column = 0;                      // Source column number
};

// AST Tree representing a complete action orchestration definition
struct Tree {
  std::string name; // Tree name/identifier
  Node root;        // Root node of the tree
};

// AST Program representing the entire DSL file (can contain multiple trees)
struct Program {
  std::vector<Tree> trees; // All tree definitions in the file
};

} // namespace actions
