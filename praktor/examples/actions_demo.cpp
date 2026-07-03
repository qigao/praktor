#include "util/logging.hpp"
#include "workflow_runner.hpp"
#include <iostream>


int main() {
  std::cout << "=== Praktor Action Orchestration Integration Demo ===\n\n";

  // Example YAML with action orchestration task
  const char *yaml_content = R"(
name: "Action Orchestration Demo"

tasks:
  - name: orchestrated_workflow
    sequence:
      - shell: "echo Starting YAML action orchestration..."
      - shell:
          cmd: "echo All systems go"
          timeout: 5000
)";

  try {
    // This would normally be loaded from a file
    // For now, just demonstrate the structure
    std::cout << "Action orchestration task structure:\n";
    std::cout << "- Task action: Orch\n";
    std::cout << "- Executor: DeclarativeTreeExecutor\n";
    std::cout << "- Integration: YAML only, flat syntax\n";
    std::cout << "- Actions handle task-internal execution logic\n\n";

    std::cout << "Example YAML:\n" << yaml_content << "\n";

    std::cout << "\nTo run this workflow:\n";
    std::cout << "  praktor run actions-example.yml\n";

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
