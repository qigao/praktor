# Prakter Project Structure

## Overview
Prakter is a workflow execution engine with a clean, modular architecture.

## Directory Layout

```
prakter/
├── include/              # Public headers
│   ├── dag/             # DAG orchestration & workflow execution
│   ├── executors/       # Task executor implementations
│   ├── expressions/     # Expression evaluation (advanced)
│   ├── flow/            # Workflow visualization & loading
│   ├── util/            # Utilities (logging, templates, etc.)
│   └── yml/             # YAML parsing & task definitions
│
├── src/                 # Implementation files (mirrors include/)
│   ├── dag/
│   ├── executors/
│   ├── expressions/
│   ├── util/
│   └── yml/
│
├── test/                # Unit tests
├── main.cpp             # Entry point
└── CMakeLists.txt       # Build configuration
```

## Module Responsibilities

### dag/
**Workflow orchestration and execution management**
- `workflow_executor` - Main workflow execution engine
- `trigger_executor` - Event-driven trigger execution (on_success, on_failure, on_complete)
- `task_executor_pool` - Thread pool for parallel task execution
- `workflow_context` - Shared state and variables during execution
- `enhanced_graph` - DAG representation with dependency tracking

### executors/
**Task execution implementations**
- `run_command_executor` - Execute shell commands
- `script_executor` - Execute embedded scripts (JavaScript, etc.)
- `file_operation_executor` - File I/O operations
- `control_flow_executor` - Conditional execution logic

### yml/
**Workflow definition parsing**
- `task_parser` - Parse YAML workflow files
- `task_yaml` - YAML serialization/deserialization
- `task` - Task and Workflow data structures
- `task_types` - Type definitions (TaskAction, Triggers, etc.)

### expressions/
**Advanced expression evaluation**
- `expression_evaluator` - Full-featured expression parser with functions
- Supports complex expressions, function calls, and type coercion

### util/
**Shared utilities**
- `expression_evaluator` - Simple boolean expression evaluator (PEGTL-based)
- `logger` - Logging infrastructure
- `template_engine` - String templating
- `variable_substitution` - Variable interpolation
- `system_info` - System information gathering
- `thread_pool` - Generic thread pool implementation

### flow/
**Workflow visualization and analysis**
- `task_loader` - Load workflow definitions
- `task_tree` - Tree representation of workflows
- `dot_writer` - Generate GraphViz DOT files for visualization

## Key Design Decisions

1. **Separation of Concerns**: Executors are separate from orchestration logic
2. **Two Expression Evaluators**:
   - `util/expression_evaluator` - Simple, fast boolean evaluation for `when` conditions
   - `expressions/expression_evaluator` - Full-featured for complex expressions
3. **Trigger System**: Event-driven actions (HTTP POST, file writes, notifications) separate from main execution
4. **Thread Safety**: WorkflowContext and executor pool designed for concurrent execution

## Build System

CMake-based build with:
- Object library (`prakter_lib`) for reusable components
- Main executable (`prakter`)
- Optional test suite (enabled with `ENABLE_TESTS`)

## Dependencies

- **ryml** - YAML parsing
- **jsoncons** - JSON handling
- **libuv** - Async I/O for command execution
- **PEGTL** - Expression parsing (header-only)
