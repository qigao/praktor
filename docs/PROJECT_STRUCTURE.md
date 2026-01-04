# Prakter Project Structure Guide

## Current Structure Overview

```
prakter/
├── include/                     # Public headers
│   ├── dag/                     # DAG and execution (⚠️ confusing name)
│   │   ├── executors/           # Task executors
│   │   ├── enhanced_graph.hpp   # Enhanced DAG implementation
│   │   ├── graph.hpp            # Basic graph structure
│   │   ├── workflow_executor.hpp # Main workflow executor
│   │   ├── workflow_context.hpp  # Execution context
│   │   └── trigger_executor.hpp  # Trigger system
│   ├── expressions/             # Expression evaluation
│   │   └── expression_evaluator.hpp
│   ├── flow/                    # Flow visualization (⚠️ unclear purpose)
│   │   ├── task_tree.hpp
│   │   ├── task_dot.hpp
│   │   └── dot_writer.hpp
│   ├── util/                    # Utilities
│   │   ├── logger.hpp
│   │   ├── system_info.hpp
│   │   ├── variable_substitution.hpp
│   │   └── ...
│   ├── yml/                     # YAML parsing (⚠️ implementation detail)
│   │   ├── task_parser.hpp
│   │   ├── task_yaml.hpp
│   │   ├── task.hpp
│   │   └── task_types.hpp
│   └── workflow_runner.hpp      # Main entry point
├── src/                         # Implementation files (mirrors include/)
│   ├── dag/
│   ├── expressions/
│   ├── util/
│   ├── yml/
│   └── workflow_runner.cpp
├── test/                        # Unit tests
└── main.cpp                     # CLI entry point
```

## Structure by Functionality

### Core Workflow Engine
**Purpose:** Main workflow execution logic

| File | Location | Description |
|------|----------|-------------|
| `workflow_runner.hpp/cpp` | `include/`, `src/` | Main workflow runner |
| `workflow_executor.hpp/cpp` | `include/dag/`, `src/dag/` | Executes workflow DAG |
| `workflow_context.hpp` | `include/dag/` | Execution context and state |

### Graph & DAG
**Purpose:** Task dependency graph management

| File | Location | Description |
|------|----------|-------------|
| `graph.hpp` | `include/dag/` | Basic graph structure |
| `enhanced_graph.hpp` | `include/dag/` | DAG with topological sort |
| `task_tree.hpp` | `include/flow/` | Tree representation |
| `task_dot.hpp` | `include/flow/` | DOT format export |
| `dot_writer.hpp` | `include/flow/` | DOT file writer |

### Parsers & Evaluation
**Purpose:** YAML parsing and expression evaluation

| File | Location | Description |
|------|----------|-------------|
| `task_parser.hpp/cpp` | `include/yml/`, `src/yml/` | Main YAML parser |
| `task_yaml.hpp/cpp` | `include/yml/`, `src/yml/` | YAML serialization |
| `expression_evaluator.hpp/cpp` | `include/expressions/`, `src/expressions/` | Expression engine |

### Task Executors
**Purpose:** Execute different task types

| File | Location | Description |
|------|----------|-------------|
| `run_command_executor.hpp/cpp` | `include/dag/executors/`, `src/dag/executors/` | Command execution |
| `script_executor.hpp/cpp` | `include/dag/executors/`, `src/dag/executors/` | JavaScript execution |
| `trigger_executor.hpp/cpp` | `include/dag/`, `src/dag/` | Trigger system |

### Data Models
**Purpose:** Core data structures

| File | Location | Description |
|------|----------|-------------|
| `task.hpp` | `include/yml/` | Task definition |
| `task_types.hpp` | `include/yml/` | Task type enums and structs |

### Utilities
**Purpose:** Helper functions and tools

| File | Location | Description |
|------|----------|-------------|
| `logger.hpp` | `include/util/` | Logging system |
| `system_info.hpp/cpp` | `include/util/`, `src/util/` | System information |
| `variable_substitution.hpp` | `include/util/` | Variable interpolation |
| `file_utils.hpp` | `include/util/` | File operations |
| `thread_pool.hpp` | `include/util/` | Thread pool |
| `template_engine.hpp/cpp` | `include/util/`, `src/util/` | Template processing |
| `cxxopts.hpp` | `include/util/` | CLI argument parsing |

## Naming Conventions

### Current Issues
1. **`dag/`** - Too technical, doesn't describe purpose
2. **`yml/`** - Implementation detail, not domain concept
3. **`flow/`** - Unclear what "flow" means
4. **`util/`** - Too generic, everything is a "utility"

### Recommended Naming
- **`dag/` → `core/` or `engine/`** - Core workflow execution
- **`yml/` → `parsers/` or `io/`** - Input/output and parsing
- **`util/` → `utils/` or split into specific categories**

## Logical Grouping

### By Layer (Recommended)

```
prakter/
├── core/              # Core workflow engine
│   ├── workflow_runner
│   ├── workflow_executor
│   └── workflow_context
├── graph/             # DAG and graph algorithms
│   ├── graph
│   ├── enhanced_graph
│   ├── task_tree
│   └── visualization (dot_writer, task_dot)
├── parsers/           # Input parsing
│   ├── task_parser
│   ├── task_yaml
│   └── expression_evaluator
├── executors/         # Task execution
│   ├── run_command_executor
│   ├── script_executor
│   └── trigger_executor
├── models/            # Data models
│   ├── task
│   └── task_types
└── utils/             # Utilities
    ├── logger
    ├── system_info
    ├── variable_substitution
    └── ...
```

### By Feature (Alternative)

```
prakter/
├── workflow/          # Workflow management
│   ├── runner
│   ├── executor
│   └── context
├── tasks/             # Task definitions and execution
│   ├── models/
│   ├── executors/
│   └── parser/
├── graph/             # Graph algorithms
├── expressions/       # Expression evaluation
└── common/            # Shared utilities
```

## Navigation Guide

### "I want to..."

**...understand how workflows execute**
→ Start with `workflow_runner.hpp` → `workflow_executor.cpp`

**...add a new task type**
→ Look at `task_types.hpp` → `executors/` folder

**...modify YAML parsing**
→ Check `yml/task_parser.cpp` and `yml/task_yaml.cpp`

**...understand the DAG algorithm**
→ Read `dag/enhanced_graph.hpp`

**...add a new trigger action**
→ Modify `dag/trigger_executor.cpp`

**...change expression syntax**
→ Update `expressions/expression_evaluator.cpp`

**...add logging**
→ Use `util/logger.hpp`

## Dependencies

### Internal Dependencies

```
main.cpp
  └─> workflow_runner
       └─> workflow_executor
            ├─> enhanced_graph
            │    └─> graph
            ├─> task_parser
            │    ├─> task_yaml
            │    └─> task (models)
            ├─> executors/
            │    ├─> run_command_executor
            │    ├─> script_executor
            │    └─> trigger_executor
            ├─> expression_evaluator
            └─> workflow_context
```

### External Dependencies

- **ryml** - Fast YAML parsing
- **jsoncons** - JSON handling and JMESPath
- **quickjs-ng** - JavaScript execution
- **libuv** - Async I/O (for script executor)
- **curl** - HTTP requests (Linux/macOS)
- **WinHTTP** - HTTP requests (Windows)
- **pegtl** - Expression parsing

## Best Practices

### Adding New Files

1. **Choose the right folder** based on functionality
2. **Follow naming conventions** (snake_case for files)
3. **Update CMakeLists.txt** to include new files
4. **Add corresponding test file** in `test/` folder
5. **Document public APIs** with Doxygen comments

### Include Paths

**Current style:**
```cpp
#include "dag/workflow_executor.hpp"
#include "yml/task_parser.hpp"
#include "util/logger.hpp"
```

**Recommended style (with namespace):**
```cpp
#include "prakter/core/workflow_executor.hpp"
#include "prakter/parsers/task_parser.hpp"
#include "prakter/utils/logger.hpp"
```

### Namespace Organization

```cpp
namespace Prakter {
    namespace Core {
        // workflow_runner, workflow_executor
    }
    namespace Graph {
        // enhanced_graph, task_tree
    }
    namespace Parsers {
        // task_parser, expression_evaluator
    }
    namespace Executors {
        // run_command_executor, script_executor, trigger_executor
    }
    namespace Models {
        // task, task_types
    }
    namespace Utils {
        // logger, system_info
    }
}
```

## File Organization Checklist

When adding a new component:

- [ ] Is it in the right folder based on functionality?
- [ ] Does the folder name clearly describe its purpose?
- [ ] Are related files co-located?
- [ ] Is the include path intuitive?
- [ ] Does it follow the project's naming conventions?
- [ ] Is there a corresponding test file?
- [ ] Is it documented in this guide?

## Future Improvements

### Short Term
1. Rename `dag/` to `core/` or `engine/`
2. Rename `yml/` to `parsers/`
4. Add `prakter/` prefix to all include paths

### Long Term
1. Separate public API from internal implementation
2. Create a `prakter/` namespace for all code
3. Split large files (e.g., `workflow_executor.cpp`)
4. Add module-level README files
5. Generate API documentation with Doxygen

## Migration Path

If refactoring the structure:

1. **Create new folders** alongside old ones
2. **Copy files** to new locations
3. **Update include paths** gradually
4. **Add compatibility headers** in old locations
5. **Update build system** (CMakeLists.txt)
6. **Run tests** to verify nothing broke
7. **Remove old folders** after verification
8. **Update documentation**

## Quick Reference

| What | Where |
|------|-------|
| Main entry point | `main.cpp` |
| Workflow execution | `workflow_runner.cpp`, `dag/workflow_executor.cpp` |
| Task parsing | `yml/task_parser.cpp` |
| Task execution | `dag/executors/*.cpp` |
| Trigger system | `dag/trigger_executor.cpp` |
| Expression evaluation | `expressions/expression_evaluator.cpp` |
| Graph algorithms | `dag/enhanced_graph.hpp` |
| Data models | `yml/task.hpp`, `yml/task_types.hpp` |
| Utilities | `util/*.hpp` |
| Tests | `test/*.cpp` |
| Examples | `examples/*.yml` |
| Documentation | `docs/*.md` |
