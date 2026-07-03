# actions Node Semantics Reference

Complete semantic specification for all action orchestration nodes in actions.

## Table of Contents

1. [Control Flow Nodes](#control-flow-nodes)
2. [Decorators](#decorators)
3. [Advanced Decorators](#advanced-decorators)
4. [Extended Nodes](#extended-nodes)
5. [Leaf Nodes](#leaf-nodes)
6. [Execution Semantics](#execution-semantics)

---

## Control Flow Nodes

### Sequence

**Semantic**: Executes children in order until one fails or all succeed.

**Execution Logic**:
```
for each child:
    status = execute(child)
    if status == FAILURE:
        return FAILURE
    if status == RUNNING:
        return RUNNING
return SUCCESS
```

**Returns**:
- `SUCCESS` - All children returned SUCCESS
- `FAILURE` - First child that returned FAILURE
- `RUNNING` - First child that returned RUNNING

**State Behavior** (with ExecutionContext):
- Saves progress at current child when RUNNING
- Resumes from saved position on next execution
- Resets state on completion (SUCCESS/FAILURE)

**Use Cases**:
- Sequential task execution
- Pipeline workflows
- Prerequisite chains


**Example**:
```yaml
sequence:
  - shell: echo "Step 1"
  - shell: echo "Step 2"
  - shell: echo "Step 3"
# Executes in order, stops on first failure
```

---

### Fallback

**Semantic**: Executes children in order until one succeeds or all fail.

**Execution Logic**:
```
for each child:
    status = execute(child)
    if status == SUCCESS:
        return SUCCESS
    if status == RUNNING:
        return RUNNING
return FAILURE
```

**Returns**:
- `SUCCESS` - First child that returned SUCCESS
- `FAILURE` - All children returned FAILURE
- `RUNNING` - First child that returned RUNNING

**State Behavior** (with ExecutionContext):
- Saves progress at current child when RUNNING
- Resumes from saved position on next execution
- Resets state on completion (SUCCESS/FAILURE)

**Use Cases**:
- Fallback strategies
- Error recovery
- Alternative approaches

**Example**:
```yaml
fallback:
  - shell: curl https://primary.api.com
  - shell: curl https://backup.api.com
  - shell: echo "All APIs failed"
# Tries each until one succeeds
```

---

### Parallel

**Semantic**: Executes all children concurrently using thread pool.

**Execution Logic**:
```
tasks = []
for each child:
    tasks.append(async_execute(child))

wait_all(tasks)

if any task returned RUNNING:
    return RUNNING
if any task returned FAILURE:
    return FAILURE
return SUCCESS
```

**Returns**:
- `SUCCESS` - All children returned SUCCESS
- `FAILURE` - At least one child returned FAILURE
- `RUNNING` - At least one child returned RUNNING

**Thread Safety**:
- Uses shared thread pool (2 * CPU cores, clamped [4, 32])
- Each child gets independent blackboard copy
- Results merged after all complete

**Use Cases**:
- Concurrent operations
- Parallel testing
- Independent tasks

**Example**:
```yaml
parallel:
  - shell: npm run test:unit
  - shell: npm run test:integration
  - shell: npm run lint
# All run concurrently
```

---


### ReactiveSequence

**Semantic**: Like Sequence, but re-evaluates all previous children on each tick.

**Execution Logic**:
```
for each child:
    status = execute(child)  // Always re-execute from start
    if status == FAILURE:
        return FAILURE
    if status == RUNNING:
        return RUNNING
return SUCCESS
```

**Key Difference from Sequence**:
- Sequence: Saves progress, skips completed children
- ReactiveSequence: Always starts from first child

**Returns**:
- `SUCCESS` - All children returned SUCCESS in this tick
- `FAILURE` - Any child returned FAILURE
- `RUNNING` - Any child returned RUNNING

**Use Cases**:
- Continuous condition monitoring
- Reactive behaviors
- Real-time validation

**Example**:
```yaml
reactive_sequence:
  - check_exit_code:  # Re-checked every tick
      input_key: health_status
      expected: 0
  - shell: process_data.sh  # Only runs if health check passes
# Health check re-evaluated on every execution
```

---

### ReactiveFallback

**Semantic**: Like Fallback, but re-evaluates all previous children on each tick.

**Execution Logic**:
```
for each child:
    status = execute(child)  // Always re-execute from start
    if status == SUCCESS:
        return SUCCESS
    if status == RUNNING:
        return RUNNING
return FAILURE
```

**Key Difference from Fallback**:
- Fallback: Saves progress, skips failed children
- ReactiveFallback: Always starts from first child

**Returns**:
- `SUCCESS` - Any child returned SUCCESS
- `FAILURE` - All children returned FAILURE in this tick
- `RUNNING` - Any child returned RUNNING

**Use Cases**:
- Priority-based selection
- Dynamic fallback chains
- Condition-based routing

**Example**:
```yaml
reactive_fallback:
  - precondition:  # Re-checked every tick
      condition: "{high_priority_mode}"
      child:
        shell: process_high_priority.sh
  - shell: process_normal.sh
# Priority re-evaluated on every execution
```

---


### PipelineSequence

**Semantic**: Executes children in sequence, passing output of each to the next.

**Execution Logic**:
```
data = blackboard.get(input_key) or ""

for each child:
    blackboard.set("pipeline_input", data)
    status = execute(child)
    
    if status == FAILURE:
        return FAILURE
    if status == RUNNING:
        return RUNNING
    
    data = blackboard.get("pipeline_output")

blackboard.set(output_key, data)
return SUCCESS
```

**Parameters**:
- `input_key` (default: "pipeline_input") - Initial input
- `output_key` (default: "pipeline_result") - Final output

**Data Flow**:
1. Stage N reads from `pipeline_input`
2. Stage N writes to `pipeline_output`
3. Stage N+1 reads `pipeline_output` as its `pipeline_input`

**Returns**:
- `SUCCESS` - All stages completed
- `FAILURE` - Any stage failed
- `RUNNING` - Any stage returned RUNNING

**Use Cases**:
- Data transformation pipelines
- ETL workflows
- Multi-stage processing

**Example**:
```yaml
pipeline_sequence:
  output_key: final_result
  children:
    - shell:
        cmd: curl -s https://api.example.com/data
        output_key: pipeline_output
    - parse_json:
        input_key: pipeline_input
        path: $.items
        output_key: pipeline_output
    - shell:
        cmd: echo "{pipeline_input}" | jq '.[] | .name'
        output_key: pipeline_output
# Data flows: API → JSON → Transform
```

---

### Switch

**Semantic**: Executes child at index matching variable value (zero-based).

**Execution Logic**:
```
index = resolve_switch_index(variable)

if index < 0 or index >= children.size():
    return FAILURE

return execute(children[index])
```

**Parameters**:
- `variable` (required) - Variable to evaluate as case index

**Returns**:
- Child's status if index valid
- `FAILURE` if index out of range

**Use Cases**:
- Multi-way branching
- State machines
- Case-based routing

**Example**:
```yaml
switch:
  variable: "{deployment_env}"
  cases:
    - shell: deploy_dev.sh      # Case 0
    - shell: deploy_staging.sh  # Case 1
    - shell: deploy_prod.sh     # Case 2
```

---


### WhileDo

**Semantic**: Repeatedly executes action while condition is true.

**Execution Logic**:
```
iteration = 0
while iteration < max_iterations:
    condition_status = evaluate_condition()
    
    if condition_status == FAILURE:
        return SUCCESS  // Loop exit is success
    if condition_status == RUNNING:
        return RUNNING
    
    action_status = execute(action)
    
    if action_status == FAILURE:
        return FAILURE
    if action_status == RUNNING:
        return RUNNING
    
    iteration++

return SUCCESS
```

**Parameters**:
- `condition` (optional) - Scalar condition or blackboard reference
- `max_iterations` (default: 100) - Safety limit

**Children**:
- If `condition` parameter: 1 child (action)
- If no parameter: 2 children (condition node, action node)

**Returns**:
- `SUCCESS` - Condition became false or max iterations reached
- `FAILURE` - Action failed
- `RUNNING` - Condition or action returned RUNNING

**Use Cases**:
- Polling loops
- Retry until condition
- Iterative processing

**Example with parameter**:
```yaml
while:
  condition: "{keep_running}"
  max_iterations: 1000
  do:
    sequence:
      - shell: process_batch.sh
      - sleep: 1000
```

**Example with node**:
```yaml
while:
  max_iterations: 100
  do:
    - check_exit_code:  # Condition node
        input_key: queue_size
        expected: 0
    - shell: process_item.sh  # Action node
```

---

### IfThenElse

**Semantic**: Executes then-branch if condition succeeds, else-branch otherwise.

**Execution Logic**:
```
condition_status = evaluate_condition()

if condition_status == RUNNING:
    return RUNNING

if condition_status == SUCCESS:
    return execute(then_branch)
else:
    if else_branch exists:
        return execute(else_branch)
    return FAILURE
```

**Parameters**:
- `condition` (optional) - Scalar condition or blackboard reference

**Children**:
- If `condition` parameter: 1-2 children (then, [else])
- If no parameter: 2-3 children (condition node, then, [else])

**Returns**:
- Condition RUNNING: `RUNNING`
- Condition SUCCESS: then-branch status
- Condition FAILURE: else-branch status (or FAILURE if no else)

**Use Cases**:
- Conditional execution
- Binary decisions
- Feature flags

**Example with parameter**:
```yaml
if:
  condition: "{deploy_enabled}"
then:
  shell: deploy.sh
else:
  shell: echo "Deployment disabled"
```

**Example with node**:
```yaml
if:
then:
  - file_exists:  # Condition node
      path: /tmp/ready.flag
  - shell: start_service.sh  # Then branch
  - shell: echo "Not ready"  # Else branch (optional)
```

---


## Decorators

### Retry

**Semantic**: Retries child up to N times until success.

**Execution Logic**:
```
for attempt in 1..num_attempts:
    status = execute(child)
    
    if status == SUCCESS:
        return SUCCESS
    if status == RUNNING:
        return RUNNING
    
    // FAILURE: retry

return FAILURE  // All attempts failed
```

**Parameters**:
- `num_attempts` (default: 3) - Maximum retry count

**Returns**:
- `SUCCESS` - Child succeeded on any attempt
- `FAILURE` - All attempts failed
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Flaky operations
- Network requests
- Transient failures

**Example**:
```yaml
retry:
  num_attempts: 5
  child:
    shell: curl -f https://api.example.com
```

---

### Inverter

**Semantic**: Inverts child's SUCCESS/FAILURE status.

**Execution Logic**:
```
status = execute(child)

if status == SUCCESS:
    return FAILURE
if status == FAILURE:
    return SUCCESS
return RUNNING
```

**Returns**:
- `SUCCESS` - Child returned FAILURE
- `FAILURE` - Child returned SUCCESS
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Negation logic
- Failure detection
- Inverse conditions

**Example**:
```yaml
inverter:
  child:
    file_exists:
      path: /tmp/lock.file
# Returns SUCCESS if file does NOT exist
```

---

### ForceSuccess

**Semantic**: Converts child's FAILURE to SUCCESS.

**Execution Logic**:
```
status = execute(child)

if status == RUNNING:
    return RUNNING
return SUCCESS
```

**Returns**:
- `SUCCESS` - Always (unless child RUNNING)
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Optional operations
- Best-effort tasks
- Ignore failures

**Example**:
```yaml
force_success:
  child:
    shell: optional_cleanup.sh
# Never fails, even if cleanup fails
```

---


### ForceFailure

**Semantic**: Converts child's SUCCESS to FAILURE.

**Execution Logic**:
```
status = execute(child)

if status == RUNNING:
    return RUNNING
return FAILURE
```

**Returns**:
- `FAILURE` - Always (unless child RUNNING)
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Negative assertions
- Failure injection
- Testing

**Example**:
```yaml
force_failure:
  child:
    shell: test_error_handling.sh
# Always fails, tests error path
```

---

### Repeat

**Semantic**: Executes child N times.

**Execution Logic**:
```
for cycle in 1..num_cycles:
    status = execute(child)
    
    if status == RUNNING:
        return RUNNING
    
    // Continue regardless of SUCCESS/FAILURE

return SUCCESS
```

**Parameters**:
- `num_cycles` (required) - Number of repetitions

**Returns**:
- `SUCCESS` - All cycles completed
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Batch processing
- Warm-up operations
- Load generation

**Example**:
```yaml
repeat:
  num_cycles: 10
  child:
    shell: curl https://api.example.com/warmup
# Calls API 10 times
```

---

### Timeout

**Semantic**: Fails if child exceeds time limit.

**Execution Logic**:
```
start_time = now()
status = execute(child)
elapsed = now() - start_time

if elapsed > timeout_ms:
    return FAILURE

return status
```

**Parameters**:
- `timeout_ms` (default: 1000) - Timeout in milliseconds

**Special Behavior**:
- If child is `Shell` node, timeout is pushed down to process level and the process is forcibly killed if timeout is exceeded.
- **LIMITATION**: For non-Shell composite subtrees (e.g., `Sequence`, `Fallback`), timeout is checked **after** the child completes. The timeout acts as a post-execution assertion rather than preemptive termination. Complex subtrees that block indefinitely cannot be interrupted mid-execution.

**Returns**:
- Child's status if within timeout
- `FAILURE` if timeout exceeded

**Use Cases**:
- Prevent hanging (Shell nodes only)
- SLA enforcement
- Resource limits

**Example**:
```yaml
timeout:
  timeout_ms: 5000
  child:
    shell: long_running_task.sh
# Fails if takes > 5 seconds (process is killed)

# WARNING: This will NOT interrupt the sequence mid-execution:
timeout:
  timeout_ms: 5000
  child:
    sequence:
      - shell: step1.sh  # If this hangs, timeout cannot interrupt it
      - shell: step2.sh
```

---


### Delay

**Semantic**: Waits before executing child.

**Execution Logic**:
```
sleep(delay_ms)
return execute(child)
```

**Parameters**:
- `delay_ms` (default: 100) - Delay in milliseconds

**Returns**:
- Child's status (after delay)

**Use Cases**:
- Rate limiting
- Debouncing
- Scheduled execution

**Example**:
```yaml
delay:
  delay_ms: 2000
  child:
    shell: send_notification.sh
# Waits 2 seconds before sending
```

---

## Advanced Decorators

### KeepRunningUntilFailure

**Semantic**: Continuously executes child until it fails (inverse of Retry).

**Execution Logic**:
```
for iteration in 1..max_iterations:
    status = execute(child)
    
    if status == FAILURE:
        return SUCCESS  // Failure is success!
    if status == RUNNING:
        return RUNNING
    
    // SUCCESS: continue looping

return FAILURE  // Max iterations without failure
```

**Parameters**:
- `max_iterations` (default: 1000) - Safety limit

**Returns**:
- `SUCCESS` - Child returned FAILURE (expected behavior)
- `FAILURE` - Max iterations reached without child failing
- `RUNNING` - Child returned RUNNING

**Use Cases**:
- Monitoring until error
- Waiting for service failure
- Health check loops

**Example**:
```yaml
keep_running_until_failure:
  max_iterations: 100
  child:
    sequence:
      - shell:
          cmd: curl -f http://localhost:8080/health
      - sleep: 1000
# Polls health endpoint until it fails
```

**Key Insight**: This is NOT a retry mechanism. It expects the child to eventually fail and treats that failure as success.

---


### RunOnce

**Semantic**: Executes child only once, caches result for subsequent calls.

**Execution Logic**:
```
node_id = generate_unique_id(node)

if context.has_state(node_id) and state.has_run:
    return state.last_status  // Return cached result

status = execute(child)

if status != RUNNING:
    state.has_run = true
    state.last_status = status

return status
```

**Parameters**: None

**State Requirements**:
- Requires `ExecutionContext`
- State persists across executions
- Clear context to re-initialize

**Returns**:
- First execution: Child's status
- Subsequent executions: Cached status
- If child returns RUNNING: Does not cache, re-executes next time

**Use Cases**:
- One-time initialization
- Expensive setup operations
- Configuration loading

**Example**:
```yaml
sequence:
  - run_once:
      child:
        shell: initialize_database.sh
  - shell: run_application.sh
  - shell: run_tests.sh
# Database initialized only once, even if tree re-executed
```

**Key Insight**: The child is executed exactly once per ExecutionContext lifetime. If the child returns RUNNING, it will be re-executed until it returns SUCCESS or FAILURE, then that result is cached.

---

### ConsumeQueue

**Semantic**: Processes items from a JSON array queue one at a time.

**Execution Logic**:
```
queue = json_parse(blackboard.get(queue_key))

if queue is empty or invalid:
    return FAILURE

item = queue.pop_first()
blackboard.set(queue_key, queue)  // Update queue
blackboard.set(item_key, item)    // Set current item

status = execute(child)

if status == SUCCESS and queue not empty:
    return RUNNING  // More items to process

return status
```

**Parameters**:
- `queue_key` (required) - Blackboard key containing JSON array
- `item_key` (default: "current_item") - Key to store current item

**Returns**:
- `SUCCESS` - Processed last item successfully
- `FAILURE` - Queue empty, invalid format, or child failed
- `RUNNING` - More items to process

**Queue Format**:
```json
["item1", "item2", "item3"]
```

**Use Cases**:
- Task queue processing
- Batch job execution
- Sequential item processing

**Example**:
```yaml
sequence:
  - set_variable:
      key: work_queue
      value: '["task1", "task2", "task3"]'
  
  - consume_queue:
      queue_key: work_queue
      item_key: current_task
      child:
        shell: process_task.sh {current_task}
# Processes each task sequentially
```

**Key Insight**: The queue is mutated (items removed) on each execution. If child fails, queue processing stops and remaining items are left in the queue.

---


## Extended Nodes

### Precondition

**Semantic**: Executes child only if condition is true, otherwise fails immediately.

**Execution Logic**:
```
condition_met = evaluate_condition(condition)

if not condition_met:
    return FAILURE  // Child not executed

return execute(child)
```

**Parameters**:
- `condition` (required) - Boolean, string, or blackboard reference
  - Boolean: `true` / `false`
  - String: `"{variable_name}"` - resolved from blackboard
  - Truthy: Non-empty strings, non-zero numbers

**Returns**:
- `FAILURE` - Condition is false (child not executed)
- Child's status - Condition is true

**Use Cases**:
- Feature flags
- Environment checks
- Conditional execution
- Guard clauses

**Example**:
```yaml
precondition:
  condition: "{deploy_enabled}"
  child:
    shell: deploy_to_production.sh
# Only deploys if deploy_enabled is truthy
```

**Example with boolean**:
```yaml
precondition:
  condition: true
  child:
    shell: always_runs.sh
```

**Key Insight**: This is a guard clause pattern. The child is never executed if the condition is false, making it more efficient than using IfThenElse when you don't need an else branch.

---

### EntryUpdated

**Semantic**: Executes child only when watched blackboard value changes.

**Execution Logic**:
```
node_id = generate_unique_id(node)
current_value = blackboard.get(watch_key)

if not state.has_run:
    // First execution
    state.has_run = true
    blackboard.set(node_id + "_stored_value", current_value)
    return execute(child)

stored_value = blackboard.get(node_id + "_stored_value")

if current_value != stored_value:
    // Value changed
    blackboard.set(node_id + "_stored_value", current_value)
    context.reset(node_id + "_child")  // Reset child state
    return execute(child)
else:
    // Value unchanged
    return state.last_status  // Return cached result
```

**Parameters**:
- `watch_key` (required) - Blackboard key to monitor

**State Requirements**:
- Requires `ExecutionContext`
- Stores last observed value
- Resets child state on change

**Returns**:
- First execution: Child's status
- Value unchanged: Cached status (child not executed)
- Value changed: Child's new status

**Use Cases**:
- Configuration hot-reload
- File watching
- Reactive updates
- Change detection

**Example**:
```yaml
while:
  condition: "true"
  max_iterations: 1000
  do:
    - shell:
        cmd: md5sum /etc/config.yaml
        output_key: config_hash
    
    - entry_updated:
        watch_key: config_hash
        child:
          shell: reload_config.sh
    
    - sleep: 5000
# Reloads config only when file changes
```

**Key Insight**: This enables reactive programming patterns. The child is only executed when the watched value actually changes, avoiding unnecessary work.

---


## Leaf Nodes

### Shell

**Semantic**: Executes shell command and captures output.

**Parameters**:
- `cmd` (required) - Command to execute
- `output_key` (default: "shell_output") - Stdout storage key
- `stderr_key` (default: "shell_stderr") - Stderr storage key
- `exit_code_key` (default: "shell_exit_code") - Exit code storage key
- `working_dir` (optional) - Working directory
- `timeout` (optional) - Timeout in milliseconds
- `stream_output` (default: false) - Stream output to console

**Returns**:
- `SUCCESS` - Exit code 0
- `FAILURE` - Non-zero exit code or timeout

**Example**:
```yaml
shell:
  cmd: npm test
  output_key: test_output
  timeout: 30000
```

---

### ParseJson

**Semantic**: Extracts value from JSON using JSONPath.

**Parameters**:
- `input_key` (required) - Blackboard key containing JSON
- `path` (optional) - JSONPath expression (e.g., `$.user.name`)
- `output_key` (default: "parsed_output") - Result storage key

**Returns**:
- `SUCCESS` - Value extracted
- `FAILURE` - Invalid JSON or path not found

**Example**:
```yaml
parse_json:
  input_key: api_response
  path: $.data.items[0].id
  output_key: item_id
```

---

### ParseRegex

**Semantic**: Extracts value from text using regex.

**Parameters**:
- `input_key` (required) - Blackboard key containing text
- `pattern` (required) - Regex pattern
- `capture_group` (default: 0) - Capture group index
- `output_key` (default: "parsed_output") - Result storage key

**Returns**:
- `SUCCESS` - Pattern matched
- `FAILURE` - No match

**Example**:
```yaml
parse_regex:
  input_key: build_output
  pattern: "Built target (\\w+)"
  capture_group: 1
  output_key: target_name
```

---

### ParseLines

**Semantic**: Splits text into lines, optionally filtering.

**Parameters**:
- `input_key` (required) - Blackboard key containing text
- `filter` (optional) - Substring filter
- `output_key` (default: "parsed_lines") - Result storage key

**Returns**:
- `SUCCESS` - Lines extracted
- `FAILURE` - Input key not found

**Example**:
```yaml
parse_lines:
  input_key: log_output
  filter: "ERROR"
  output_key: error_lines
```

---


### ParseKeyValue

**Semantic**: Parses key-value pairs from text.

**Parameters**:
- `input_key` (required) - Blackboard key containing text
- `delimiter` (default: "=") - Key-value separator
- `line_separator` (default: "\n") - Line separator
- `output_key` (default: "parsed_output") - Result storage key (JSON object)

**Returns**:
- `SUCCESS` - Pairs extracted
- `FAILURE` - Input key not found

**Example**:
```yaml
parse_keyvalue:
  input_key: env_file
  delimiter: "="
  output_key: env_vars
```

---

### CheckExitCode

**Semantic**: Validates exit code matches expected value.

**Parameters**:
- `input_key` (default: "shell_exit_code") - Blackboard key containing exit code
- `expected` (default: 0) - Expected exit code

**Returns**:
- `SUCCESS` - Exit code matches expected
- `FAILURE` - Exit code mismatch or key not found

**Example**:
```yaml
check_exit_code:
  input_key: shell_exit_code
  expected: 0
```

---

### WaitEvent

**Semantic**: Waits for event to be signaled.

**Parameters**:
- `event` (required) - Event name
- `timeout` (default: 30000) - Timeout in milliseconds

**Returns**:
- `SUCCESS` - Event signaled
- `FAILURE` - Timeout

**Example**:
```yaml
wait_event:
  event: deployment_complete
  timeout: 60000
```

---

### Sleep

**Semantic**: Delays execution.

**Parameters**:
- `duration` (required) - Duration (number in ms or string with unit)
  - Units: `ms`, `s`, `m`, `h`

**Returns**:
- `SUCCESS` - Always (after delay)

**Example**:
```yaml
sleep:
  duration: 5000  # 5 seconds
```

**Example with units**:
```yaml
sleep:
  duration: "5s"
```

---

### FileExists

**Semantic**: Checks if file or directory exists.

**Parameters**:
- `path` (required) - File/directory path
- `output_key` (optional) - Store result as "true"/"false"
- `fail_if_missing` (default: true) - Return FAILURE if not exists

**Returns**:
- `SUCCESS` - File exists (or doesn't exist and fail_if_missing=false)
- `FAILURE` - File doesn't exist and fail_if_missing=true

**Example**:
```yaml
file_exists:
  path: /tmp/ready.flag
  fail_if_missing: true
```

---


### SetVariable

**Semantic**: Writes value to blackboard.

**Parameters**:
- `key` (required) - Blackboard key
- `value` (optional) - Value to set
- `from` (optional) - Copy from another key

**Returns**:
- `SUCCESS` - Value set
- `FAILURE` - Missing required parameters or source key not found

**Example with value**:
```yaml
set_variable:
  key: deployment_env
  value: "production"
```

**Example with copy**:
```yaml
set_variable:
  key: backup_value
  from: original_value
```

---

### SubTree

**Semantic**: Executes a registered subtree.

**Parameters**:
- `tree` (required) - Name of registered tree

**Returns**:
- Subtree's root status

**Example**:
```yaml
subtree:
  tree: "common_setup"
```

---

## Execution Semantics

### Node Status

All nodes return one of three statuses:

- `SUCCESS` - Operation completed successfully
- `FAILURE` - Operation failed
- `RUNNING` - Operation in progress (async/stateful nodes)

### Status Propagation Rules

**Control Flow**:
- Sequence: Stops on first FAILURE or RUNNING
- Fallback: Stops on first SUCCESS or RUNNING
- Parallel: Waits for all, prioritizes RUNNING > FAILURE > SUCCESS

**Decorators**:
- Retry: Retries on FAILURE, returns on SUCCESS/RUNNING
- Inverter: Swaps SUCCESS ↔ FAILURE, preserves RUNNING
- ForceSuccess/ForceFailure: Converts to SUCCESS/FAILURE, preserves RUNNING
- Timeout: Returns FAILURE if time exceeded, otherwise child's status

### State Management

**Stateless Execution** (default):
- No memory between executions
- Each execution starts fresh
- Suitable for simple workflows

**Stateful Execution** (with ExecutionContext):
- Preserves state between executions
- Enables resumable workflows
- Required for: RunOnce, EntryUpdated, stateful Sequence/Fallback

**State Reset**:
- Automatic on completion (SUCCESS/FAILURE)
- Manual via `context.reset(node_id)`
- Per-node isolation

### Blackboard

**Purpose**: Shared data storage for all nodes

**Operations**:
- `set(key, value)` - Write value
- `get(key)` - Read value
- `has(key)` - Check existence

**Scope**:
- Global within single tree execution
- Copied for parallel branches
- Persists across stateful executions

**Variable Substitution**:
```yaml
shell:
  cmd: echo "Hello {user_name}"
# {user_name} replaced with blackboard value
```

---


### Execution Context

**Purpose**: Manages node state for stateful execution

**State Storage**:
- Per-node state identified by unique ID
- Stores: `has_run`, `last_status`, `current_child`, `last_value`
- Automatic cleanup on completion

**Required For**:
- RunOnce - Caches execution result
- EntryUpdated - Tracks value changes
- Stateful Sequence/Fallback - Saves progress

**Usage**:
```cpp
Executor executor;
Blackboard bb;
ExecutionContext ctx;  // Create context

// First execution
executor.execute(tree, bb, ctx);

// Second execution - resumes from saved state
executor.execute(tree, bb, ctx);

// Reset to start fresh
ctx.reset();
```

### Thread Pool

**Configuration**:
- Size: `2 * hardware_concurrency`, clamped to [4, 32]
- Shared across all Executor instances
- Automatic lifecycle management

**Usage**:
- Parallel node uses thread pool automatically
- No configuration needed
- Thread-safe blackboard copies per branch

### Monitoring

**Metrics Collected** (when enabled):
- Execution count per node type
- Success/Failure/Running counts
- Min/Max/Average duration
- Success rate percentage

**Enable Monitoring**:
```cpp
executor.enableMonitoring(true);
executor.execute(tree, bb);

auto metrics = executor.getMonitor().getMetrics("Shell");
std::cout << "Success rate: " << metrics.success_rate() << "%\n";
```

**Performance Impact**: ~1-5% overhead when enabled

---

## Semantic Patterns

### Guard Pattern

Use Precondition for early exit:
```yaml
precondition:
  condition: "{feature_enabled}"
  child:
    sequence:
      - shell: expensive_operation.sh
      - shell: another_operation.sh
# Entire sequence skipped if condition false
```

### Reactive Pattern

Use EntryUpdated for change detection:
```yaml
while:
  condition: "true"
  do:
    - shell:
        cmd: cat config.yaml
        output_key: config
    - entry_updated:
        watch_key: config
        child:
          shell: reload.sh
    - sleep: 1000
# Reloads only when config changes
```

### Pipeline Pattern

Use PipelineSequence for data flow:
```yaml
pipeline_sequence:
  children:
    - shell: fetch_data.sh
    - parse_json: ...
    - shell: transform.sh
# Data flows through stages
```

### Retry Pattern

Use Retry for fault tolerance:
```yaml
retry:
  num_attempts: 3
  child:
    shell: flaky_command.sh
# Retries up to 3 times
```

### Fallback Pattern

Use Fallback for alternatives:
```yaml
fallback:
  - shell: primary_service.sh
  - shell: backup_service.sh
  - shell: echo "All failed"
# Tries each until one succeeds
```

---

## Comparison with BehaviorTree.CPP

| Feature | actions | BT.CPP |
|---------|-------|--------|
| Sequence | ✅ Same | ✅ |
| Fallback | ✅ Same | ✅ (Selector) |
| Parallel | ✅ Thread pool | ✅ |
| ReactiveSequence | ✅ Same | ✅ |
| ReactiveFallback | ✅ Same | ✅ |
| Retry | ✅ Same | ✅ |
| Timeout | ✅ Process-aware | ✅ |
| KeepRunningUntilFailure | ✅ Same | ✅ |
| RunOnce | ✅ Context-based | ✅ |
| Precondition | ✅ actions extension | ❌ |
| EntryUpdated | ✅ actions extension | ❌ |
| PipelineSequence | ✅ actions extension | ❌ |
| ConsumeQueue | ✅ actions extension | ❌ |

---

## Best Practices

1. **Use Precondition for guards** - More efficient than IfThenElse when no else needed
2. **Use EntryUpdated for reactive behavior** - Avoids unnecessary re-execution
3. **Use PipelineSequence for data flow** - Clear data dependencies
4. **Use RunOnce for initialization** - Ensures one-time setup
5. **Use ConsumeQueue for batch processing** - Sequential item processing
6. **Always set max_iterations** - Prevents infinite loops
7. **Use ExecutionContext for stateful workflows** - Enables resumable execution
8. **Enable monitoring in production** - Track performance and reliability
9. **Use Parallel for independent tasks** - Automatic thread management
10. **Use Fallback for error recovery** - Graceful degradation

---

## Troubleshooting

**Node always returns FAILURE**:
- Check blackboard keys exist
- Verify parameter types (string vs number)
- Enable monitoring to see execution counts

**Infinite loop**:
- Check max_iterations on While/KeepRunningUntilFailure
- Verify loop exit conditions
- Use monitoring to detect stuck nodes

**State not preserved**:
- Ensure using ExecutionContext
- Check if state reset on completion
- Verify node IDs are stable

**Parallel node hangs**:
- Check for deadlocks in child nodes
- Verify thread pool size sufficient
- Use timeout decorator on parallel children

**Variable substitution not working**:
- Verify syntax: `{key}` not `${key}`
- Check key exists in blackboard
- Ensure value is string-compatible

---

## Summary

actions provides a complete action orchestration implementation with:

- **Standard control flow**: Sequence, Fallback, Parallel, Reactive variants
- **Rich decorators**: Retry, Timeout, Inverter, Force*, Repeat, Delay
- **Advanced decorators**: KeepRunningUntilFailure, RunOnce, ConsumeQueue
- **Extended nodes**: Precondition, EntryUpdated, PipelineSequence
- **Comprehensive leaf nodes**: Shell, Parse*, Check*, File*, Set*
- **Stateful execution**: ExecutionContext for resumable workflows
- **Thread pool**: Automatic parallel execution
- **Monitoring**: Built-in metrics collection

All nodes follow consistent semantics with clear SUCCESS/FAILURE/RUNNING status propagation.
