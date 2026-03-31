# btdsl Runtime

This directory contains the lightweight behavior-tree runtime used by Praktor's `btdsl:` task runner.

## Scope

- In-memory tree data structures in `core/ast.hpp`
- Tree execution and blackboard state in `core/executor.hpp`
- Async and event helpers
- Output parsing helpers
- DOT and Mermaid visualization

## Non-Goals

- No standalone text DSL authoring
- No lexer/parser pipeline
- No plugin system
- No workflow migration tooling

YAML is the only workflow and behavior-tree authoring grammar in this repository. Praktor parses YAML and builds `btdsl::Tree` / `btdsl::Node` objects directly.

## Execution Model

Praktor has two layers:

1. Workflow orchestration at the DAG level
2. Task execution inside a runner

`btdsl` belongs only to the second layer. It provides task-internal control flow such as:

- `Sequence`
- `Fallback`
- `Parallel`
- `Retry`
- `IfThenElse`
- `Switch`
- `Timeout`
- `Delay`

Leaf work should call external processes or runtime helpers. HTTP belongs in TurboScript or another external process path, not in BT-specific plugins.

## Minimal C++ Example

```cpp
#include "core/ast.hpp"
#include "core/executor.hpp"

int main() {
  btdsl::Tree tree;
  tree.name = "BuildTree";
  tree.root.id = "Sequence";

  btdsl::Node step1;
  step1.id = "Configure";

  btdsl::Node step2;
  step2.id = "Build";
  step2.params["target"] = std::string("app");

  tree.root.children.push_back(step1);
  tree.root.children.push_back(step2);

  btdsl::Executor executor;
  executor.registerTask("Configure", [](const auto&, btdsl::Blackboard&) {
    return btdsl::NodeStatus::SUCCESS;
  });
  executor.registerTask("Build", [](const auto&, btdsl::Blackboard&) {
    return btdsl::NodeStatus::SUCCESS;
  });

  btdsl::Blackboard blackboard;
  return executor.execute(tree, blackboard) == btdsl::NodeStatus::SUCCESS ? 0 : 1;
}
```

## Visualization

`btdsl::Visualizer` can export a tree or program to:

- GraphViz DOT
- Mermaid

## Building

The library is built as part of the repository CMake project.

```bash
cmake --preset=default
cmake --build --preset=default
ctest --output-on-failure
```

## Tests

The retained unit test in this module validates the core AST model:

- `test_ast`

Higher-level behavior-tree authoring and orchestration coverage lives in Praktor tests, where YAML is the source of truth.
