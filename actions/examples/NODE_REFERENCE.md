# actions Node Reference

Complete reference for all available nodes in actions C++ API.

## Table of Contents

- [Advanced Decorators](#advanced-decorators)
  - [KeepRunningUntilFailure](#keeprunninguntilfailure)
  - [RunOnce](#runonce)
  - [ConsumeQueue](#consumequeue)
- [Extended Nodes](#extended-nodes)
  - [Precondition](#precondition)
  - [EntryUpdated](#entryupdated)
  - [PipelineSequence](#pipelinesequence)

---

## Advanced Decorators

### KeepRunningUntilFailure

**Description**: Continuously executes its child node until the child returns FAILURE. Returns SUCCESS when child fails, or FAILURE if max iterations reached.

**Use Cases**:
- Polling until an error occurs
- Waiting for a service to become unavailable
- Monitoring loops

**Parameters**:
- `max_iterations` (number, default: 1000) - Maximum number of iterations before giving up

**Returns**:
- `SUCCESS` - Child returned FAILURE (expected behavior)
- `FAILURE` - Max iterations reached without child failing
- `RUNNING` - Child returned RUNNING

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;

Node decorator;
decorator.id = "KeepRunningUntilFailure";
decorator.params["max_iterations"] = 100.0;

Node child;
child.id = "PollingTask";
decorator.children.push_back(child);

NodeStatus status = executor.execute(decorator, bb);
```

**Best Practices**:
- Always set `max_iterations` to prevent infinite loops
- Combine with `Sleep` to avoid busy-waiting
- Use for health checks and monitoring scenarios

---

### RunOnce

**Description**: Executes its child node only once, then caches and returns the result on subsequent executions. Requires `ExecutionContext`.

**Use Cases**:
- One-time initialization
- Expensive operations that should run once
- Configuration loading

**Parameters**: None

**Returns**:
- First execution: Returns child's result
- Subsequent executions: Returns cached result
- If child returns `RUNNING`: Does not cache, re-executes next time

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;
ExecutionContext ctx;

Node decorator;
decorator.id = "RunOnce";

Node child;
child.id = "InitTask";
decorator.children.push_back(child);

// First execution - runs child
NodeStatus status1 = executor.execute(decorator, bb, ctx);

// Second execution - returns cached result
NodeStatus status2 = executor.execute(decorator, bb, ctx);
```

**Best Practices**:
- Use for initialization tasks
- Ensure child is idempotent (safe to run multiple times during RUNNING state)
- Clear `ExecutionContext` if you need to re-initialize

---

### ConsumeQueue

**Description**: Processes items from a JSON array queue one at a time. Pops the first item, sets it in blackboard, executes child, and continues until queue is empty.

**Use Cases**:
- Task queue processing
- Batch job execution
- Sequential item processing

**Parameters**:
- `queue_key` (string, default: "task_queue") - Blackboard key containing JSON array
- `item_key` (string, default: "current_item") - Blackboard key to store current item

**Returns**:
- `SUCCESS` - Processed last item successfully
- `FAILURE` - Queue empty, invalid format, or child failed
- `RUNNING` - More items to process

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;

// Setup queue
bb.set("task_queue", R"(["task1", "task2", "task3"])");

Node decorator;
decorator.id = "ConsumeQueue";
decorator.params["queue_key"] = std::string("task_queue");
decorator.params["item_key"] = std::string("current_task");

Node child;
child.id = "ProcessTask";
decorator.children.push_back(child);

// Process first item
NodeStatus status = executor.execute(decorator, bb);
```

**Best Practices**:
- Ensure queue is valid JSON array
- Handle child failures appropriately (they stop queue processing)
- Use with `Retry` for fault tolerance

---

## Extended Nodes

### Precondition

**Description**: Evaluates a condition before executing its child. If condition is false, returns FAILURE without executing child.

**Use Cases**:
- Feature flags
- Environment checks
- Conditional execution

**Parameters**:
- `condition` (string or boolean, required) - Condition to evaluate
  - Boolean literal: `true` or `false`
  - Blackboard reference: `"{variable_name}"`
  - Truthy evaluation: non-empty strings, non-zero numbers

**Returns**:
- `FAILURE` - Condition is false (child not executed)
- Child's result - Condition is true

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;

bb.set("feature_enabled", "true");

Node decorator;
decorator.id = "Precondition";
decorator.params["condition"] = std::string("{feature_enabled}");

Node child;
child.id = "FeatureTask";
decorator.children.push_back(child);

NodeStatus status = executor.execute(decorator, bb);
```

**Best Practices**:
- Use for feature flags and environment checks
- Combine with `Fallback` for default behavior
- Keep conditions simple and readable

---

### EntryUpdated

**Description**: Executes its child only when a watched blackboard value changes. Caches the last value and compares on each execution. Requires `ExecutionContext`.

**Use Cases**:
- Configuration hot-reload
- File watching
- Reactive updates

**Parameters**:
- `watch_key` (string, required) - Blackboard key to monitor for changes

**Returns**:
- First execution: Executes child, returns child's result
- Value unchanged: Returns cached result (child not executed)
- Value changed: Executes child, returns new result

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;
ExecutionContext ctx;

Node decorator;
decorator.id = "EntryUpdated";
decorator.params["watch_key"] = std::string("config_hash");

Node child;
child.id = "ReloadConfig";
decorator.children.push_back(child);

// First execution
bb.set("config_hash", "abc123");
NodeStatus status1 = executor.execute(decorator, bb, ctx);

// Second execution - same value, child not executed
NodeStatus status2 = executor.execute(decorator, bb, ctx);

// Third execution - value changed, child executed
bb.set("config_hash", "def456");
NodeStatus status3 = executor.execute(decorator, bb, ctx);
```

**Best Practices**:
- Use in loops for reactive behavior
- Combine with `Sleep` to avoid busy-waiting
- Ensure watched value is deterministic

---

### PipelineSequence

**Description**: Executes children in sequence, passing output of each stage to the next via `pipeline_input` and `pipeline_output` blackboard keys.

**Use Cases**:
- Data transformation pipelines
- ETL workflows
- Multi-stage processing

**Parameters**:
- `input_key` (string, default: "pipeline_input") - Initial input key
- `output_key` (string, default: "pipeline_result") - Final output key

**Returns**:
- `SUCCESS` - All stages completed successfully
- `FAILURE` - Any stage failed
- `RUNNING` - A stage returned RUNNING

**Data Flow**:
1. Reads initial input from `input_key` (if exists)
2. For each child:
   - Sets `pipeline_input` with data from previous stage
   - Child executes
   - Reads `pipeline_output` for next stage
3. Stores final output in `output_key`

**C++ Example**:
```cpp
Executor executor;
Blackboard bb;

// Register pipeline stages
executor.registerTask("Stage1", [](const auto&, Blackboard& bb) {
    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
    bb.set("pipeline_output", input + "A");
    return NodeStatus::SUCCESS;
});

executor.registerTask("Stage2", [](const auto&, Blackboard& bb) {
    std::string input = bb.get("pipeline_input");
    bb.set("pipeline_output", input + "B");
    return NodeStatus::SUCCESS;
});

Node pipeline;
pipeline.id = "PipelineSequence";
pipeline.params["output_key"] = std::string("result");

Node stage1;
stage1.id = "Stage1";
pipeline.children.push_back(stage1);

Node stage2;
stage2.id = "Stage2";
pipeline.children.push_back(stage2);

NodeStatus status = executor.execute(pipeline, bb);
// bb.get("result") == "AB"
```

**Best Practices**:
- Each stage should read from `pipeline_input` and write to `pipeline_output`
- Use for linear data transformations
- Combine with `Fallback` for error handling
- Keep stages focused and single-purpose

---

## Monitoring

All nodes can be monitored when `Executor::enableMonitoring(true)` is called.

**Metrics Collected**:
- Execution count
- Success/Failure/Running counts
- Min/Max/Average duration
- Success rate

**C++ Example**:
```cpp
Executor executor;
executor.enableMonitoring(true);

// Execute tree...
executor.execute(tree, blackboard);

// Get metrics
auto metrics = executor.getMonitor().getMetrics("Shell");
std::cout << "Success rate: " << metrics.success_rate() << "%\n";
std::cout << "Avg duration: " << metrics.avg_duration.count() << "ms\n";

// Get summary
auto summary = executor.getMonitor().getSummary();
std::cout << "Total executions: " << summary.total_executions << "\n";
```

---

## Thread Pool

All `Parallel` nodes use a shared thread pool to prevent thread explosion.

**Configuration**:
- Thread count: `2 * hardware_concurrency`, clamped to [4, 32]
- Shared across all executors
- Automatic cleanup

**No configuration needed** - works out of the box!

---

## Performance Considerations

- **RunOnce**: Zero overhead after first execution (cached result)
- **EntryUpdated**: String comparison overhead on each check
- **PipelineSequence**: Blackboard read/write overhead per stage
- **ConsumeQueue**: JSON parsing overhead per item
- **Monitoring**: ~1-5% overhead when enabled (disabled by default)

---

## Troubleshooting

**RunOnce not caching**:
- Ensure you're using `ExecutionContext` in execute call
- Check if child returns `RUNNING` (prevents caching)

**EntryUpdated always re-executing**:
- Verify watched value is stable (not changing every time)
- Check if value includes timestamps or random data

**PipelineSequence not passing data**:
- Ensure each stage writes to `pipeline_output`
- Verify stages read from `pipeline_input`

**ConsumeQueue failing**:
- Validate JSON array format
- Check if queue key exists in blackboard
- Ensure child handles items correctly

## Table of Contents

- [Advanced Decorators](#advanced-decorators)
  - [KeepRunningUntilFailure](#keeprunninguntilfailure)
  - [RunOnce](#runonce)
  - [ConsumeQueue](#consumequeue)
- [Extended Nodes](#extended-nodes)
  - [Precondition](#precondition)
  - [EntryUpdated](#entryupdated)
  - [PipelineSequence](#pipelinesequence)

---

## Advanced Decorators

### KeepRunningUntilFailure

**Description**: Continuously executes its child node until the child returns FAILURE. Returns SUCCESS when child fails, or FAILURE if max iterations reached.

**Use Cases**:
- Polling until an error occurs
- Waiting for a service to become unavailable
- Monitoring loops

**Parameters**:
- `max_iterations` (number, default: 1000) - Maximum number of iterations before giving up

**Returns**:
- `SUCCESS` - Child returned FAILURE (expected behavior)
- `FAILURE` - Max iterations reached without child failing
- `RUNNING` - Child returned RUNNING

**Example**:
```yaml
type: KeepRunningUntilFailure
params:
  max_iterations: 100
children:
  - type: Shell
    params:
      cmd: "curl -f http://localhost:8080/health"
  - type: Sleep
    params:
      duration: "1s"
```

**Best Practices**:
- Always set `max_iterations` to prevent infinite loops
- Combine with `Sleep` to avoid busy-waiting
- Use for health checks and monitoring scenarios

---

### RunOnce

**Description**: Executes its child node only once, then caches and returns the result on subsequent executions. Requires `ExecutionContext`.

**Use Cases**:
- One-time initialization
- Expensive operations that should run once
- Configuration loading

**Parameters**: None

**Returns**:
- First execution: Returns child's result
- Subsequent executions: Returns cached result
- If child returns `RUNNING`: Does not cache, re-executes next time

**Example**:
```yaml
type: Sequence
children:
  - type: RunOnce
    children:
      - type: Shell
        params:
          cmd: "initialize_database.sh"
  - type: Shell
    params:
      cmd: "run_application.sh"
```

**Best Practices**:
- Use for initialization tasks
- Ensure child is idempotent (safe to run multiple times during RUNNING state)
- Clear `ExecutionContext` if you need to re-initialize

---

### ConsumeQueue

**Description**: Processes items from a JSON array queue one at a time. Pops the first item, sets it in blackboard, executes child, and continues until queue is empty.

**Use Cases**:
- Task queue processing
- Batch job execution
- Sequential item processing

**Parameters**:
- `queue_key` (string, default: "task_queue") - Blackboard key containing JSON array
- `item_key` (string, default: "current_item") - Blackboard key to store current item

**Returns**:
- `SUCCESS` - Processed last item successfully
- `FAILURE` - Queue empty, invalid format, or child failed
- `RUNNING` - More items to process

**Example**:
```yaml
type: Sequence
children:
  - type: SetVariable
    params:
      key: "task_queue"
      value: '["task1", "task2", "task3"]'
  
  - type: ConsumeQueue
    params:
      queue_key: "task_queue"
      item_key: "current_task"
    children:
      - type: Shell
        params:
          cmd: "process_task.sh {current_task}"
```

**Best Practices**:
- Ensure queue is valid JSON array
- Handle child failures appropriately (they stop queue processing)
- Use with `Retry` for fault tolerance

---

## Extended Nodes

### Precondition

**Description**: Evaluates a condition before executing its child. If condition is false, returns FAILURE without executing child.

**Use Cases**:
- Feature flags
- Environment checks
- Conditional execution

**Parameters**:
- `condition` (string or boolean, required) - Condition to evaluate
  - Boolean literal: `true` or `false`
  - Blackboard reference: `"{variable_name}"`
  - Truthy evaluation: non-empty strings, non-zero numbers

**Returns**:
- `FAILURE` - Condition is false (child not executed)
- Child's result - Condition is true

**Example**:
```yaml
type: Precondition
params:
  condition: "{feature_enabled}"
children:
  - type: Shell
    params:
      cmd: "run_new_feature.sh"
```

**Example with boolean**:
```yaml
type: Precondition
params:
  condition: true
children:
  - type: Shell
    params:
      cmd: "always_runs.sh"
```

**Best Practices**:
- Use for feature flags and environment checks
- Combine with `Fallback` for default behavior
- Keep conditions simple and readable

---

### EntryUpdated

**Description**: Executes its child only when a watched blackboard value changes. Caches the last value and compares on each execution. Requires `ExecutionContext`.

**Use Cases**:
- Configuration hot-reload
- File watching
- Reactive updates

**Parameters**:
- `watch_key` (string, required) - Blackboard key to monitor for changes

**Returns**:
- First execution: Executes child, returns child's result
- Value unchanged: Returns cached result (child not executed)
- Value changed: Executes child, returns new result

**Example**:
```yaml
type: WhileDo
params:
  condition: "true"
  max_iterations: 1000
children:
  - type: Shell
    params:
      cmd: "cat config.yaml"
      output_key: "config_content"
  
  - type: EntryUpdated
    params:
      watch_key: "config_content"
    children:
      - type: Shell
        params:
          cmd: "reload_config.sh"
  
  - type: Sleep
    params:
      duration: "10s"
```

**Best Practices**:
- Use in loops for reactive behavior
- Combine with `Sleep` to avoid busy-waiting
- Ensure watched value is deterministic

---

### PipelineSequence

**Description**: Executes children in sequence, passing output of each stage to the next via `pipeline_input` and `pipeline_output` blackboard keys.

**Use Cases**:
- Data transformation pipelines
- ETL workflows
- Multi-stage processing

**Parameters**:
- `input_key` (string, default: "pipeline_input") - Initial input key
- `output_key` (string, default: "pipeline_result") - Final output key

**Returns**:
- `SUCCESS` - All stages completed successfully
- `FAILURE` - Any stage failed
- `RUNNING` - A stage returned RUNNING

**Data Flow**:
1. Reads initial input from `input_key` (if exists)
2. For each child:
   - Sets `pipeline_input` with data from previous stage
   - Child executes
   - Reads `pipeline_output` for next stage
3. Stores final output in `output_key`

**Example**:
```yaml
type: PipelineSequence
params:
  input_key: "raw_data"
  output_key: "processed_data"
children:
  # Stage 1: Fetch
  - type: Shell
    params:
      cmd: "curl -s https://api.example.com/data"
      output_key: "pipeline_output"
  
  # Stage 2: Parse
  - type: ParseJson
    params:
      input_key: "pipeline_input"
      path: "$.items"
      output_key: "pipeline_output"
  
  # Stage 3: Transform
  - type: Shell
    params:
      cmd: "echo '{pipeline_input}' | jq '.[] | .name'"
      output_key: "pipeline_output"
```

**Best Practices**:
- Each stage should read from `pipeline_input` and write to `pipeline_output`
- Use for linear data transformations
- Combine with `Fallback` for error handling
- Keep stages focused and single-purpose

---

## Monitoring

All nodes can be monitored when `Executor::enableMonitoring(true)` is called.

**Metrics Collected**:
- Execution count
- Success/Failure/Running counts
- Min/Max/Average duration
- Success rate

**Example (C++)**:
```cpp
Executor executor;
executor.enableMonitoring(true);

// Execute tree...
executor.execute(tree, blackboard);

// Get metrics
auto metrics = executor.getMonitor().getMetrics("Shell");
std::cout << "Success rate: " << metrics.success_rate() << "%\n";
std::cout << "Avg duration: " << metrics.avg_duration.count() << "ms\n";

// Get summary
auto summary = executor.getMonitor().getSummary();
std::cout << "Total executions: " << summary.total_executions << "\n";
```

---

## Thread Pool

All `Parallel` nodes use a shared thread pool to prevent thread explosion.

**Configuration**:
- Thread count: `2 * hardware_concurrency`, clamped to [4, 32]
- Shared across all executors
- Automatic cleanup

**No configuration needed** - works out of the box!

---

## Tips and Tricks

### Combining Nodes

**Retry with Precondition**:
```yaml
type: Retry
params:
  num_attempts: 3
children:
  - type: Precondition
    params:
      condition: "{retry_enabled}"
    children:
      - type: Shell
        params:
          cmd: "flaky_command.sh"
```

**Pipeline with Error Handling**:
```yaml
type: Fallback
children:
  - type: PipelineSequence
    children:
      - type: Shell
        params:
          cmd: "stage1.sh"
      - type: Shell
        params:
          cmd: "stage2.sh"
  
  - type: Shell
    params:
      cmd: "echo 'Pipeline failed, using default'"
```

**Reactive Monitoring Loop**:
```yaml
type: WhileDo
params:
  condition: "true"
  max_iterations: 10000
children:
  - type: Shell
    params:
      cmd: "get_status.sh"
      output_key: "status"
  
  - type: EntryUpdated
    params:
      watch_key: "status"
    children:
      - type: Shell
        params:
          cmd: "handle_status_change.sh {status}"
  
  - type: Sleep
    params:
      duration: "5s"
```

---

## Migration from BT.CPP

If you're familiar with BehaviorTree.CPP:

| BT.CPP | actions | Notes |
|--------|-------|-------|
| `KeepRunningUntilFailure` | `KeepRunningUntilFailure` | Same behavior |
| `RunOnce` | `RunOnce` | Requires ExecutionContext |
| `ConsumeQueue` | `ConsumeQueue` | JSON array format |
| N/A | `Precondition` | actions extension |
| N/A | `EntryUpdated` | actions extension |
| N/A | `PipelineSequence` | actions extension |

---

## Performance Considerations

- **RunOnce**: Zero overhead after first execution (cached result)
- **EntryUpdated**: String comparison overhead on each check
- **PipelineSequence**: Blackboard read/write overhead per stage
- **ConsumeQueue**: JSON parsing overhead per item
- **Monitoring**: ~1-5% overhead when enabled (disabled by default)

---

## Troubleshooting

**RunOnce not caching**:
- Ensure you're using `ExecutionContext` in execute call
- Check if child returns `RUNNING` (prevents caching)

**EntryUpdated always re-executing**:
- Verify watched value is stable (not changing every time)
- Check if value includes timestamps or random data

**PipelineSequence not passing data**:
- Ensure each stage writes to `pipeline_output`
- Verify stages read from `pipeline_input`

**ConsumeQueue failing**:
- Validate JSON array format
- Check if queue key exists in blackboard
- Ensure child handles items correctly
