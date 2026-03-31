#include "util/logging.hpp"
#include "workflow_runner.hpp"
#include <iostream>


int main() {
  std::cout << "=== Praktor Behavior-Tree Integration Demo ===\n\n";

  // Example YAML with an explicit behavior-tree task
  const char *yaml_content = R"(
name: "Behavior Tree Demo"

tasks:
  - name: explicit_btdsl
    btdsl:
      Sequence:
        - Shell: "echo Starting YAML behavior tree..."
        - Shell:
            cmd: "echo All systems go"
            timeout: "5s"
)";

  try {
    // This would normally be loaded from a file
    // For now, just demonstrate the structure
    std::cout << "Behavior-tree task structure:\n";
    std::cout << "- Task action: Btdsl\n";
    std::cout << "- Executor: DeclarativeTreeExecutor\n";
    std::cout << "- Integration: YAML only, no text DSL parsing in Praktor\n";
    std::cout << "- Behavior tree handles task-internal execution logic\n\n";

    std::cout << "Example YAML:\n" << yaml_content << "\n";

    std::cout << "\nTo run this workflow:\n";
    std::cout << "  praktor run btdsl-example.yml\n";

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
