# Prakter Architecture Overview

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         CLI (main.cpp)                       │
│                    Command Line Interface                    │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                    WorkflowRunner                            │
│              (workflow_runner.hpp/cpp)                       │
│  • Loads YAML files                                          │
│  • Initializes context                                       │
│  • Coordinates execution                                     │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                      TaskParser                              │
│                (yml/task_parser.cpp)                         │
│  • Parses YAML syntax                                        │
│  • Validates structure                                       │
│  • Creates Task objects                                      │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                    EnhancedGraph                             │
│              (dag/enhanced_graph.hpp)                        │
│  • Builds dependency DAG                                     │
│  • Topological sort                                          │
│  • Detects cycles                                            │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                  WorkflowExecutor                            │
│            (dag/workflow_executor.cpp)                       │
│  • Executes tasks in order                                   │
│  • Manages context                                           │
│  • Handles retries                                           │
│  • Evaluates conditions                                      │
└─────────────┬───────────────┬───────────────┬───────────────┘
              │               │               │
              ▼               ▼               ▼
    ┌─────────────┐ ┌─────────────┐ ┌─────────────┐
    │  Command    │ │   Script    │ │    Uses     │
    │  Executor   │ │  Executor   │ │  Executor   │
    └─────────────┘ └─────────────┘ └─────────────┘
              │               │               │
              └───────────────┴───────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │ TriggerExecutor │
                    │  (on_success,   │
                    │  on_failure,    │
                    │  on_complete)   │
                    └─────────────────┘
```

## Component Layers

```
┌─────────────────────────────────────────────────────────────┐
│                      Presentation Layer                      │
│                                                               │
│  main.cpp                                                     │
│  • CLI argument parsing                                       │
│  • User interaction                                           │
│  • Output formatting                                          │
└─────────────────────────────────────────────────────────────┘
                             │
┌─────────────────────────────────────────────────────────────┐
│                      Application Layer                       │
│                                                               │
│  workflow_runner.hpp/cpp                                      │
│  • Workflow lifecycle management                             │
│  • Configuration loading                                      │
│  • Error handling                                             │
└─────────────────────────────────────────────────────────────┘
                             │
┌─────────────────────────────────────────────────────────────┐
│                        Domain Layer                          │
│                                                               │
│  dag/workflow_executor.cpp                                    │
│  • Core business logic                                        │
│  • Task execution orchestration                               │
│  • Context management                                         │
└─────────────────────────────────────────────────────────────┘
                             │
┌─────────────────────────────────────────────────────────────┐
│                      Infrastructure Layer                    │
│                                                               │
│  Parsers (yml/)                                               │
│  • YAML parsing                                               │
│  • Expression evaluation                                      │
│                                                               │
│  Executors (dag/executors/)                                   │
│  • Command execution                                          │
│  • Script execution                                           │
│  • Trigger execution                                          │
│                                                               │
│  Utilities (util/)                                            │
│  • Logging                                                    │
│  • System info                                                │
│  • Variable substitution                                      │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow

```
YAML File
   │
   ▼
[TaskParser] ──────────────────┐
   │                           │
   │ creates                   │ validates
   ▼                           ▼
Task Objects ────────────> Workflow
   │                           │
   │                           │
   ▼                           ▼
[EnhancedGraph] ◄──────── [TaskParser]
   │
   │ builds DAG
   ▼
Execution Order
   │
   ▼
[WorkflowExecutor] ◄────── WorkflowContext
   │                           ▲
   │ executes                  │
   ▼                           │ updates
[Task Executors] ──────────────┘
   │
   │ produces
   ▼
Task Outputs ──────────────> WorkflowContext
   │
   ▼
[TriggerExecutor]
   │
   ▼
Side Effects (HTTP, Files, Tasks)
```

## Module Dependencies

```
main.cpp
  │
  └─> WorkflowRunner
       │
       ├─> TaskParser ──────────┐
       │    │                   │
       │    ├─> TaskYaml        │
       │    └─> Task (models)   │
       │                        │
       ├─> EnhancedGraph ◄──────┘
       │    └─> Graph
       │
       └─> WorkflowExecutor
            │
            ├─> WorkflowContext
            │
            ├─> ExpressionEvaluator
            │
            ├─> Task Executors
            │    ├─> RunCommandExecutor
            │    ├─> ScriptExecutor
            │    └─> UsesExecutor (recursive)
            │
            └─> TriggerExecutor
                 ├─> HTTP (WinHTTP/curl)
                 ├─> File I/O
                 └─> RunCommandExecutor
```

## Folder to Component Mapping

```
Folder Structure              Component Type
─────────────────────────────────────────────────
main.cpp                  →   Entry Point

workflow_runner.*         →   Application Controller

dag/
├── workflow_executor.*   →   Core Engine
├── workflow_context.*    →   State Management
├── enhanced_graph.*      →   Graph Algorithm
├── graph.*               →   Data Structure
├── trigger_executor.*    →   Event Handler
└── executors/
    ├── run_command.*     →   Command Runner
    └── script.*          →   Script Runner

yml/
├── task_parser.*         →   Parser
├── task_yaml.*           →   Serializer
├── task.*                →   Data Model
└── task_types.*          →   Type Definitions

expressions/
└── expression_evaluator.* →  Expression Engine

flow/
├── task_tree.*           →   Tree Structure
└── task_dot.*            →   Visualization

util/
├── logger.*              →   Logging
├── system_info.*         →   System Interface
└── variable_substitution.* → Template Engine
```

## Execution Sequence

```
1. User runs: prakter [run|validate|export|...] workflow.yml
   │
2. main.cpp parses subcommands and arguments
   │
3. Dispatcher selects specialized logic (Init, Valdiate, Export, Run)
   │
4. If running: WorkflowRunner created
   │
4. TaskParser reads YAML
   │
   ├─> Parses tasks
   ├─> Validates syntax
   └─> Creates Task objects
   │
5. EnhancedGraph builds DAG
   │
   ├─> Analyzes dependencies
   ├─> Detects cycles
   └─> Determines execution order
   │
6. WorkflowContext initialized
   │
   ├─> Sets global variables
   ├─> Loads environment
   └─> Prepares task outputs storage
   │
7. WorkflowExecutor starts
   │
   ├─> For each task in order:
   │    │
   │    ├─> Evaluate 'when' condition
   │    │
   │    ├─> Apply task variables
   │    │
   │    ├─> Execute task (with retries)
   │    │    │
   │    │    ├─> RunCommandExecutor
   │    │    │    └─> Spawn process
   │    │    │         └─> Capture output
   │    │    │
   │    │    ├─> ScriptExecutor
   │    │    │    └─> Run JavaScript
   │    │    │         └─> Update context
   │    │    │
   │    │    └─> UsesExecutor
   │    │         └─> Load nested workflow
   │    │              └─> Recursive execution
   │    │
   │    ├─> Store outputs in context
   │    │
   │    ├─> Execute triggers
   │    │    │
   │    │    ├─> on_success
   │    │    ├─> on_failure
   │    │    └─> on_complete
   │    │
   │    └─> Restore variables
   │
8. Workflow completes
   │
9. Exit with status code
```

## Context Flow

```
WorkflowContext (Shared State)
┌─────────────────────────────────────────┐
│                                         │
│  variables: {                           │
│    APP_NAME: "my-app",                  │
│    VERSION: "1.0.0"                     │
│  }                                      │
│                                         │
│  tasks: {                               │
│    build: {                             │
│      status: "success",                 │
│      outputs: {                         │
│        stdout: "Build complete",        │
│        exit_code: 0                     │
│      }                                  │
│    },                                   │
│    test: {                              │
│      status: "success",                 │
│      outputs: { ... }                   │
│    }                                    │
│  }                                      │
│                                         │
│  env: {                                 │
│    NODE_ENV: "production"               │
│  }                                      │
│                                         │
└─────────────────────────────────────────┘
         ▲                    │
         │                    │
         │ read               │ write
         │                    ▼
    ┌─────────┐         ┌─────────┐
    │  Task   │         │  Task   │
    │ Executor│         │ Executor│
    └─────────┘         └─────────┘
```

## Key Design Patterns

### 1. Strategy Pattern
**Location:** `dag/executors/`
```
TaskExecutor (interface)
  ├─> RunCommandExecutor
  ├─> ScriptExecutor
  └─> UsesExecutor
```

### 2. Observer Pattern
**Location:** `dag/trigger_executor.cpp`
```
Task completion event
  ├─> on_success triggers
  ├─> on_failure triggers
  └─> on_complete triggers
```

### 3. Composite Pattern
**Location:** `dag/enhanced_graph.hpp`
```
Workflow
  └─> Tasks
       └─> Uses (nested workflows)
            └─> Tasks (recursive)
```

### 4. Builder Pattern
**Location:** `yml/task_parser.cpp`
```
TaskParser
  └─> builds Task objects
       └─> from YAML structure
```

### 5. Context Pattern
**Location:** `dag/workflow_context.hpp`
```
WorkflowContext
  └─> Shared state across all tasks
       └─> Variables, outputs, environment
```

## Technology Stack

```
┌─────────────────────────────────────────┐
│          Application Layer              │
│                                         │
│  C++20                                  │
│  • Modern C++ features                  │
│  • Smart pointers                       │
│  • RAII                                 │
└─────────────────────────────────────────┘
                  │
┌─────────────────────────────────────────┐
│           Core Libraries                │
│                                         │
│  • ryml - YAML parsing                  │
│  • jsoncons - JSON + JMESPath           │
│  • pegtl - Expression parsing           │
│  • quickjs-ng - JavaScript engine       │
│  • libuv - Async I/O                    │
└─────────────────────────────────────────┘
                  │
┌─────────────────────────────────────────┐
│         Platform Libraries              │
│                                         │
│  Windows: WinHTTP                       │
│  Linux/macOS: libcurl                   │
└─────────────────────────────────────────┘
```

## Performance Characteristics

```
Component              Time Complexity    Space Complexity
─────────────────────────────────────────────────────────
YAML Parsing           O(n)               O(n)
DAG Building           O(V + E)           O(V + E)
Topological Sort       O(V + E)           O(V)
Task Execution         O(V)               O(V)
Context Lookup         O(1) avg           O(n)
Expression Eval        O(n)               O(n)

Where:
  n = file size
  V = number of tasks
  E = number of dependencies
```

## Concurrency Model

```
Current: Sequential Execution
┌────┐    ┌────┐    ┌────┐    ┌────┐
│ T1 │───▶│ T2 │───▶│ T3 │───▶│ T4 │
└────┘    └────┘    └────┘    └────┘

Future: Parallel Execution (DAG-based)
┌────┐    ┌────┐
│ T1 │───▶│ T3 │───┐
└────┘    └────┘   │
                   ├──▶┌────┐
┌────┐    ┌────┐   │   │ T5 │
│ T2 │───▶│ T4 │───┘   └────┘
└────┘    └────┘
```

## Error Handling Strategy

```
Error Level          Handler                Action
─────────────────────────────────────────────────────
Parse Error       → TaskParser           → Throw exception
Validation Error  → EnhancedGraph        → Throw exception
Task Failure      → WorkflowExecutor     → Retry or fail
Trigger Failure   → TriggerExecutor      → Log warning
Expression Error  → ExpressionEvaluator  → Return false
```

## Extension Points

```
1. New Task Type
   └─> Add to task_types.hpp
       └─> Create executor in dag/executors/
           └─> Register in workflow_executor.cpp

2. New Trigger Action
   └─> Add to task_types.hpp
       └─> Implement in trigger_executor.cpp

3. New Expression Function
   └─> Modify expression_evaluator.cpp
       └─> Add grammar rule
           └─> Implement function

4. New Output Format
   └─> Modify run_command_executor.cpp
       └─> Add parser for format
```

## Summary

The Prakter architecture follows a layered approach with clear separation of concerns:

1. **Presentation** - CLI interface
2. **Application** - Workflow management
3. **Domain** - Core execution logic
4. **Infrastructure** - Parsing, execution, utilities

Key strengths:
- ✅ Clear data flow
- ✅ Modular design
- ✅ Extensible architecture
- ✅ Well-defined interfaces

Areas for improvement:
- ⚠️ Folder names don't match architecture
- ⚠️ Some mixed concerns (yml/ folder)
- ⚠️ Could benefit from explicit interfaces
