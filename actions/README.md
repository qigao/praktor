# actions Runtime

This directory contains the lightweight action orchestration runtime used by Praktor's `actions:` task runner.

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

YAML is the only workflow and action authoring grammar in this repository. Praktor parses YAML and builds `actions::Tree` / `actions::Node` objects directly.

## Execution Model

Praktor has two layers:

1. Workflow orchestration at the DAG level
2. Task execution inside a runner

`actions` belongs only to the second layer. It provides task-internal control flow such as:

- `Sequence`
- `Fallback`
- `Parallel`
- `Retry`
- `If`
- `While`
- `Switch`
- `Timeout`
- `Delay`

Leaf work should call external processes or runtime helpers. HTTP belongs in TurboScript or another external process path, not in action-specific plugins.

YAML authoring lives in Praktor, not in this folder. Nodes may carry both parameters and children at once, so advanced forms are written as flat maps such as:

```yaml
if: "{ctx.tasks.fetch.outputs.ok}"
then:
  - shell: echo healthy
else:
  - shell: echo unhealthy

while: "{ctx.variables.keep_polling}"
do:
  - shell: echo poll

timeout: 5000
child:
  shell: echo guarded
```

`Repeat` is only safe with a finite `num_cycles` when driven through Praktor's synchronous task runner. Unbounded repeats are rejected instead of spinning forever.

`Timeout` now fails any child that overruns `timeout_ms`. If the immediate child is `Shell`, the timeout is also passed down to the shell executor so the process itself can be cut off.

`Switch` must resolve to a zero-based case index. It may be a numeric literal, a `{blackboard_key}` reference, or a bare blackboard key name.

## Minimal C++ Example

```cpp
#include "core/ast.hpp"
#include "core/executor.hpp"

int main() {
  actions::Tree tree;
  tree.name = "BuildTree";
  tree.root.id = "Sequence";

  actions::Node step1;
  step1.id = "Configure";

  actions::Node step2;
  step2.id = "Build";
  step2.params["target"] = std::string("app");

  tree.root.children.push_back(step1);
  tree.root.children.push_back(step2);

  actions::Executor executor;
  executor.registerTask("Configure", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::SUCCESS;
  });
  executor.registerTask("Build", [](const auto&, actions::Blackboard&) {
    return actions::NodeStatus::SUCCESS;
  });

  actions::Blackboard blackboard;
  return executor.execute(tree, blackboard) == actions::NodeStatus::SUCCESS ? 0 : 1;
}
```

## Visualization

`actions::Visualizer` can export a tree or program to:

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

Higher-level action authoring and orchestration coverage lives in Praktor tests, where YAML is the source of truth.
