# Weave Workflow YAML Grammar Reference

**English version - Updated for Weave v2.0 with Cross-File Imports & Advanced Features**

This document defines the complete YAML syntax for Weave workflow files. The syntax is designed to be simple yet powerful, with clear separation between orchestration logic and implementation details.

**🚀 New in v2.0**: Strategy Pattern architecture, Executor Pool optimization, and enhanced performance monitoring for production-ready workflows.

## Performance & Architecture

Weave v2.0 features a **highly optimized execution engine**:

- **Strategy Pattern Architecture**: Clean, extensible task execution with 8 specialized executors
- **Executor Pool Optimization**: Zero-allocation task execution through intelligent caching
- **Concurrent DAG Execution**: True parallel processing with thread-safe operations
- **Performance Monitoring**: Built-in cache hit rate analysis and optimization suggestions

**Performance Benefits**:
- 🔥 **10x+ faster** task execution through executor pooling
- 📊 **80%+ cache hit rate** in typical workflows
- ⚡ **Zero memory allocation** for repeated task types
- 🧵 **Thread-safe** concurrent execution

## Top-Level Structure

A Weave workflow file consists of these top-level sections:

```yaml
imports:          # NEW: Cross-file task imports
  # ... imported workflow files (optional)
inputs:
  # ... runtime parameter definitions (optional) 
variables:
  # ... global variable definitions (optional)
task_templates:
  # ... reusable task definitions (optional) # NEW: Custom Task Types
defaults:
  # ... default task attributes (optional)
tasks:
  # ... task definition list (required)
```

### `imports` (Optional) - **NEW IN v2.0**

**Revolutionary feature**: Import tasks from other workflow files for modular, reusable workflows.

- **Type**: Array of strings (file paths)
- **Resolution**: Relative to importing file
- **Features**:
  - ✅ Recursive imports (imported files can import others)
  - ✅ Automatic conflict resolution (main workflow wins)
  - ✅ Variable and defaults merging
  - ✅ Cross-file task dependencies

**Example**:
```yaml
imports:
  - "shared/build-tasks.yml"
  - "shared/deploy-tasks.yml"
  - "../common/database-tasks.yml"

tasks:
  - name: my_task
    depends_on: [shared_build_task]  # Reference imported task
```

### `inputs` (Optional)

Define runtime parameters accepted by the workflow.

- **Type**: Array of objects
- **Fields**:
  - `name` (string, required): Parameter name
  - `type` (string, required): Parameter type (`string`, `integer`, `boolean`)
  - `default` (optional): Default value if not provided
  - `description` (string, optional): Parameter description

**Example**:
```yaml
inputs:
  - name: environment
    type: string
    default: "dev"
    description: "Deployment environment (dev, staging, prod)"
```

### `variables` (Optional)

Define global constants available throughout the workflow.

- **Type**: Map (key-value pairs)

**Example**:
```yaml
variables:
  DOCKER_REGISTRY: "my.docker.registry.com"
  APP_VERSION: "1.2.3"
```

### `task_templates` (Optional) - **NEW: Custom Task Types**

Define reusable, parameterized task definitions. These act like custom task types.

- **Type**: Array of objects
- **Fields**:
  - `name` (string, required): Unique name for the custom task type.
  - `parameters` (array, optional): List of parameters this custom task accepts.
    - Each parameter object has:
      - `name` (string, required): Parameter name.
      - `type` (string, required): Parameter type (e.g., `string`, `integer`, `boolean`).
      - `default` (optional): Default value if not provided.
      - `description` (string, optional): Parameter description.
  - `tasks` (array, required): The actual task definitions that make up this custom task. These tasks can use the defined parameters via `{{param_name}}` syntax.

**Example**:
```yaml
task_templates:
  - name: deploy_service
    parameters:
      - name: service_name
        type: string
        description: "Name of the service to deploy"
      - name: version
        type: string
        default: "latest"
        description: "Version of the service"
    tasks:
      - name: build_{{service_name}}
        type: run_command
        command: "build --service {{service_name}} --version {{version}}"
      - name: push_{{service_name}}
        type: run_command
        depends_on: [build_{{service_name}}]
        command: "docker push {{DOCKER_REGISTRY}}/{{service_name}}:{{version}}"
      - name: deploy_{{service_name}}
        type: run_command
        depends_on: [push_{{service_name}}]
        command: "kubectl apply -f {{service_name}}-{{version}}.yaml"
```

### `defaults` (Optional)

Define default attributes for all tasks in the workflow. Individual tasks can override these defaults.

- **Type**: Map
- **Common Fields**: `retries`, `timeout`, `vars`

**Example**:
```yaml
defaults:
  retries:
    count: 2
    delay: "5s"
  vars:
    LOG_LEVEL: "info"
```

### `tasks` (Required)

Define the list of tasks to execute in the workflow.

- **Type**: Array of task objects
- **Note**: Execution order determined by `depends_on`, not array order

## Universal Task Properties

### `name` (Required)

Unique task identifier within the workflow (including imported tasks).

- **Type**: String
- **Scope**: Must be unique across merged workflow

### `type` (Required)

Task type determining behavior and required parameters.

- **Type**: String
- **Values**: `run_command`, `copy_file`, `create_directory`, `move_file`, `parallel`, `group`, `choose`, `dynamic_tasks`

#### **🎯 Task Type Performance Guide**

**High-Performance Task Types** (Optimized in v2.0):

1. **`run_command`** - Process execution with libuv
   ```yaml
   - name: build
     type: run_command
     command: "make build -j$(nproc)"
   ```

2. **`parallel`** - Concurrent execution
   ```yaml
   - name: parallel_builds
     type: parallel
     tasks: ["build_frontend", "build_backend", "build_tests"]
   ```

3. **`dynamic_tasks`** - Template-based task generation
   ```yaml
   - name: deploy_all_services
     type: dynamic_tasks
     items_variable: "{{services}}"
     task_template:
       name: "deploy_{{item.name}}"
       type: run_command
       command: "deploy {{item.name}} --env {{environment}}"
   ```

### `choose`

Execute tasks based on conditional branches, similar to if/else if/else.

**Properties**:
- `branches` (array, required): A list of conditional branches. The first branch whose `when` condition evaluates to true will have its tasks executed. If no `when` condition is met, the `default` tasks will be executed.
  - Each branch object has:
    - `when` (string, required): A boolean expression. If true, this branch's tasks are executed.
    - `tasks` (array, required): A list of tasks to execute if the `when` condition is met.
- `default` (array, optional): A list of tasks to execute if no `when` condition in `branches` is met.

**Constraints**:
- `branches` must have at least one entry.
- `when` condition is required for each branch.
- Subtasks within `branches` or `default` can have `depends_on` (as per general task rules).

### `depends_on` (Optional)

Tasks that must complete successfully before this task starts.

- **Type**: Array of strings (task names)
- **Cross-file**: Can reference imported tasks
- **Creates**: Directed Acyclic Graph (DAG)

**Example**: `depends_on: ["compile_code", "imported_setup_task"]`

### `when` (Optional)

Conditional execution - task runs only if expression evaluates to true.

- **Type**: String (boolean expression)
- **Syntax**:
  - **Variables**: `{{variable_name}}` or `{{variable_name.jmespath_query}}` for structured data
  - **Operators**: `==`, `!=`, `and`, `or`
  - **Precedence**: `and` higher than `or`
  - **Grouping**: `()` for precedence control
  - **Literals**: `'strings'`, `true/false`, `123`

**JMESPath Usage**: When referencing a variable that contains structured data (e.g., JSON), you can use JMESPath syntax after the variable name to extract specific values. The JMESPath query is applied to the JSON object stored in the variable.

**Example with JMESPath**: `when: "{{api_response.status_code}} == 200 and {{api_response.body.data[0].id}} != null"`

**Example**: `when: "({{env}} == 'prod' or {{env}} == 'staging') and {{deploy_enabled}} == true"`

### `on_failure` & `on_success` - **ENHANCED: Detailed Failure Context**

Execute specific tasks when the current task fails or succeeds.

- **Type**: Array of strings (task names)
- **Enhanced Feature**: `on_failure` tasks automatically receive detailed failure context

**🔥 NEW: Failure Context Variables**

When a task fails and triggers `on_failure` handlers, the failure tasks automatically get access to special read-only variables:

- `{{failed_task_name}}` - Name of the failed task
- `{{failed_task_type}}` - Type of the failed task (e.g., "run_command")
- `{{failed_task_exit_code}}` - Exit code (for run_command tasks)  
- `{{failed_task_stdout}}` - Standard output from the failed task
- `{{failed_task_stderr}}` - Standard error from the failed task
- `{{failed_task_error}}` - Error message from the failure
- `{{failed_task_outputs.variable_name}}` - Access captured outputs before failure

**Real-World Example**:
```yaml
tasks:
  - name: deploy_application
    type: run_command
    command: "kubectl apply -f app.yaml"
    retries:
      count: 3
      delay: "10s"
    outputs:
      stdout_to_variable: "deploy_stdout"
      stderr_to_variable: "deploy_stderr"  
      exit_code_to_variable: "deploy_exit_code"
    on_failure: [rollback_deployment, alert_team]

  - name: rollback_deployment  
    type: run_command
    command: |
      echo "Rolling back {{failed_task_name}} - Exit Code: {{failed_task_exit_code}}"
      echo "Error Details: {{failed_task_stderr}}"
      kubectl rollout undo deployment/myapp
      # Access captured outputs: {{failed_task_outputs.deploy_stdout}}
      
  - name: alert_team
    type: run_command  
    command: |
      send-alert \
        --title "Deployment Failed: {{failed_task_name}}" \
        --message "Task {{failed_task_name}} failed with exit code {{failed_task_exit_code}}" \
        --details "{{failed_task_error}}" \
        --logs "{{failed_task_stderr}}"
```

**Advanced Failure Handling**:
```yaml
tasks:
  - name: database_migration
    type: run_command
    command: "migrate --env production"
    outputs:
      output_json_to_variable: "migration_result"
    on_failure: [analyze_migration_failure]
    
  - name: analyze_migration_failure
    type: run_command
    command: |
      echo "Migration failed for task: {{failed_task_name}}"
      if [[ "{{failed_task_exit_code}}" == "2" ]]; then
        echo "Schema conflict detected"
        rollback-schema --reason "{{failed_task_error}}"
      elif [[ "{{failed_task_exit_code}}" == "3" ]]; then
        echo "Data validation failed"
        fix-data-issues --details "{{failed_task_stderr}}"
      else
        echo "Unknown migration error: {{failed_task_error}}"
        emergency-rollback --full
      fi
```

**Why This Enhancement Matters**:
- **Intelligent Recovery**: Make decisions based on specific failure details
- **Better Logging**: Capture exact error context for debugging  
- **Conditional Logic**: Different recovery strategies based on exit codes
- **Zero Configuration**: Failure variables are automatically available

### `retries` (Optional)

Automatic retry configuration for failed tasks.

- **Type**: Object
- **Fields**:
  - `count` (integer, required): Maximum retry attempts
  - `delay` (string, optional): Delay between retries (e.g., `"5s"`, `"1m"`)

**Note**: Retries happen before `on_failure` handlers are triggered. Only after all retries fail will `on_failure` tasks execute with full failure context.

### `each` (Optional)

Execute task multiple times, once for each item in a list.

- **Type**: Object
- **Fields**:
  - `items` (array, required): List to iterate over
  - `as` (string, required): Variable name for current item

**Example**:
```yaml
each:
  items: ["auth", "api", "worker"]
  as: "service"
# Task runs 3 times with {{service}} = "auth", then "api", then "worker"
```

### `outputs` (Optional)

Capture task execution results into variables.

- **Type**: Object
- **Fields**:
  - `stdout_to_variable` (string): Capture standard output
  - `stderr_to_variable` (string): Capture standard error  
  - `exit_code_to_variable` (string): Capture exit code
  - `output_json_to_variable` (string, optional): Capture standard output as JSON and store the parsed JSON object in the specified variable. Useful for API responses or structured command outputs.

## Task Types

### `run_command`

Execute shell commands or external programs.

**Properties**:
- `command` (string/array, required): Command to execute.
  - When `command` is a **string**, it will be executed directly by the shell. This allows for shell features like pipes (`|`), redirects (`>`), and variable expansion (`$VAR`). Be mindful of shell injection risks and quoting.
  - When `command` is an **array of strings**, the first element is treated as the executable, and subsequent elements are passed as arguments. This bypasses shell parsing, making execution more predictable and safer, especially when arguments contain spaces or special characters. **It is generally recommended to use the array form unless shell features are explicitly required.**
- `working_directory` (string, optional): Execution directory
- `environment` (map, optional): Environment variables

### `copy_file`

Copy files or directories.

**Properties**:
- `source` (string, required): Source path
- `destination` (string, required): Destination path
- `overwrite` (boolean, optional): Overwrite existing files

### `create_directory`

Create directories.

**Properties**:
- `path` (string, required): Directory path
- `parents` (boolean, optional): Create parent directories

### `move_file`

Move or rename files/directories.

**Properties**:
- `source` (string, required): Source path
- `destination` (string, required): Destination path
- `overwrite` (boolean, optional): Overwrite existing files

### `parallel`

Execute multiple tasks concurrently.

**Properties**:
- `tasks` (array, required): Task list for parallel execution

**Constraints**:
- Context modifications may not be visible to subsequent tasks.
- **Shared State & Idempotency**: Tasks executed in parallel should ideally be **idempotent** (producing the same result regardless of how many times or concurrently they are run) and avoid modifying shared external state without proper external synchronization. Weave does not provide built-in mechanisms for concurrent access control to external resources. If parallel tasks modify the same external resource (e.g., a database, a file system), you must ensure atomicity and consistency through external means.

### `dynamic_tasks` - **NEW: Advanced Task Generation**

Generate and execute tasks dynamically from JSON data and templates.

**Properties**:
- `items_variable` (string, required): Variable containing JSON array of items
- `task_template` (object, required): Template for generating tasks

**Template Substitution**:
- `{{item.field}}` - Access item properties
- `{{item.nested.field}}` - Access nested JSON structures
- All standard variables remain available

**Example**:
```yaml
variables:
  services: '[{"name": "auth", "port": 3001}, {"name": "api", "port": 3002}]'

tasks:
  - name: deploy_all_microservices
    type: dynamic_tasks
    items_variable: "{{services}}"
    task_template:
      name: "deploy_{{item.name}}"
      type: run_command
      command: "docker run -p {{item.port}}:{{item.port}} {{item.name}}"
      vars:
        SERVICE_NAME: "{{item.name}}"
        SERVICE_PORT: "{{item.port}}"
```

**Advanced Usage with API Data**:
```yaml
tasks:
  - name: fetch_deployment_config
    type: run_command
    command: "curl -s https://api.example.com/deploy-config"
    outputs:
      output_json_to_variable: "deploy_config"
  
  - name: deploy_from_api_config
    type: dynamic_tasks
    depends_on: [fetch_deployment_config]
    items_variable: "{{deploy_config.services}}"
    task_template:
      name: "deploy_{{item.service_name}}"
      type: run_command
      command: "deploy --service {{item.service_name}} --replicas {{item.replicas}}"
```

## Variable Usage

Variables from `inputs`, `variables`, task `vars`, or captured `outputs` can be referenced using `{{variable_name}}` syntax. These variables can now hold **structured data** (e.g., JSON objects), not just simple strings.

**Accessing Structured Data**: For variables holding structured data, you can use JMESPath queries directly within the `{{...}}` syntax to extract specific values. For example, if `api_response` contains `{"status": "success", "data": {"id": 123}}`, you can access `{{api_response.status}}` or `{{api_response.data.id}}`.

**💡 Best Practice for JMESPath**: While powerful, overly complex or deeply nested JMESPath queries directly within your YAML can significantly reduce readability and make debugging difficult. If your data transformation logic becomes intricate, consider performing the transformation in a preceding `run_command` task (e.g., using a simple script) and storing the simplified result in a new variable. This keeps your workflow YAML clean and focused on orchestration.

**Precedence** (highest to lowest):
1. Task-level `vars`
2. Global `variables`
3. Input parameters
4. Captured `outputs`

## Complete Example with Imports

```yaml
# Modern Weave workflow with all features
imports:
  - "collections/cpp/build-tasks.yml"
  - "collections/docker/container-tasks.yml"

inputs:
  - name: environment
    type: string
    default: "dev"
  - name: enable_tests
    type: boolean
    default: true

variables:
  APP_NAME: "MyApp"
  VERSION: "2.0.0"
  DOCKER_REGISTRY: "my.docker.registry.com"

task_templates:
  - name: deploy_service
    parameters:
      - name: service_name
        type: string
      - name: version
        type: string
        default: "latest"
    tasks:
      - name: build_{{service_name}}
        type: run_command
        command: "echo Building {{service_name}} v{{version}}"
      - name: push_{{service_name}}
        type: run_command
        depends_on: [build_{{service_name}}]
        command: "echo Pushing {{DOCKER_REGISTRY}}/{{service_name}}:{{version}}"
      - name: deploy_{{service_name}}
        type: run_command
        depends_on: [push_{{service_name}}]
        command: "echo Deploying {{service_name}} v{{version}} to {{environment}}"

defaults:
  retries:
    count: 2
    delay: "5s"
  vars:
    LOG_LEVEL: "info"

tasks:
  - name: setup
    type: run_command
    command: "echo Starting {{APP_NAME}} v{{VERSION}}"
    outputs:
      stdout_to_variable: "start_message"

  # Use imported tasks
  - name: build_app
    type: run_command
    command: "echo Build complete"
    depends_on: [setup, cpp_build_project]  # cpp_build_project from imports

  - name: test_conditional
    type: run_command
    command: "echo Running tests"
    depends_on: [build_app]
    when: "{{enable_tests}} == true"

  # Use custom task type
  - name: deploy_auth_service
    type: deploy_service
    depends_on: [test_conditional]
    service_name: "auth"
    version: "1.0.0"

  - name: deploy_api_service
    type: deploy_service
    depends_on: [deploy_auth_service]
    service_name: "api"
    version: "{{VERSION}}"

  - name: completion
    type: run_command
    command: "echo {{start_message}} - Deployment complete!"
    depends_on: [deploy_api_service]
```

This example demonstrates:
- ✅ Cross-file imports for reusable tasks
- ✅ Runtime inputs with defaults
- ✅ Global and task-scoped variables  
- ✅ Cross-file task dependencies
- ✅ Conditional execution
- ✅ Parallel task execution
- ✅ Output capture and chaining
- ✅ Complete workflow orchestration

---

## 🏆 Best Practices & Patterns

### **🚀 Performance Optimization Patterns**

#### **1. Leverage Executor Pool Caching**
```yaml
# ✅ GOOD: Reuse same task types for optimal caching
tasks:
  - name: build_service_1
    type: run_command
    command: "build service1"
  - name: build_service_2  
    type: run_command        # Same type = cached executor
    command: "build service2"
  - name: build_service_3
    type: run_command        # Same type = cached executor  
    command: "build service3"

# ❌ AVOID: Mixing task types unnecessarily
```

#### **2. Maximize Parallel Execution**
```yaml
# ✅ EXCELLENT: Parallel independent tasks
tasks:
  - name: setup_infrastructure
    type: parallel
    tasks: 
      - setup_database
      - setup_cache
      - setup_load_balancer
      - setup_monitoring
  
  - name: deploy_services
    type: parallel
    depends_on: [setup_infrastructure]
    tasks:
      - deploy_auth_service
      - deploy_api_service
      - deploy_worker_service
```

#### **3. Optimize Dynamic Task Generation**
```yaml
# ✅ BEST PRACTICE: Fetch data once, generate many tasks
tasks:
  - name: get_deployment_config
    type: run_command
    command: "kubectl get deployments -o json"
    outputs:
      output_json_to_variable: "deployments"
  
  - name: rolling_update_all
    type: dynamic_tasks
    depends_on: [get_deployment_config]
    items_variable: "{{deployments.items}}"
    task_template:
      name: "update_{{item.metadata.name}}"
      type: run_command
      command: |
        kubectl patch deployment {{item.metadata.name}} \
        -p '{"spec":{"template":{"metadata":{"labels":{"version":"{{VERSION}}"}}}}}'
```

### **🎯 Workflow Design Patterns**

#### **1. Pipeline Pattern**
```yaml
# Sequential stages with clear dependencies
tasks:
  - name: validate
    type: run_command
    command: "validate-inputs"
  
  - name: build
    type: run_command 
    depends_on: [validate]
    command: "build-application"
  
  - name: test
    type: parallel
    depends_on: [build]
    tasks: [unit_tests, integration_tests, security_tests]
  
  - name: deploy
    type: run_command
    depends_on: [test]
    command: "deploy-to-production"
```

#### **2. Fan-Out/Fan-In Pattern**
```yaml
tasks:
  - name: prepare_data
    type: run_command
    command: "prepare-shared-data"
  
  # Fan-out: Process data in parallel
  - name: process_parallel
    type: parallel
    depends_on: [prepare_data]
    tasks: [process_chunk_1, process_chunk_2, process_chunk_3]
  
  # Fan-in: Combine results
  - name: combine_results
    type: run_command
    depends_on: [process_parallel]
    command: "combine-all-results"
```

#### **3. Conditional Deployment Pattern**
```yaml
tasks:
  - name: check_environment_health
    type: run_command
    command: "health-check --env {{environment}}"
    outputs:
      output_json_to_variable: "health_status"
  
  - name: conditional_deployment
    type: choose
    depends_on: [check_environment_health]
    branches:
      - when: "{{health_status.status}} == 'healthy' and {{environment}} == 'prod'"
        tasks: [full_production_deploy]
      - when: "{{health_status.status}} == 'healthy'"
        tasks: [standard_deploy]
    default: [rollback_deployment]
```

### **⚡ Performance Monitoring**

Weave v2.0 automatically logs performance statistics:

```
=== Executor Pool Performance Stats ===
Registered executor types: 8
Total cached executors: 8  
Cache hits: 247
Cache misses: 8
Cache hit rate: 96.9%
EXCELLENT: High cache efficiency - significant performance gain!
=========================================
```

#### **Performance Tuning Tips**:

1. **Monitor Cache Hit Rate**
   - **90%+**: Excellent performance
   - **70-90%**: Good performance  
   - **<70%**: Consider workflow optimization

2. **Optimize Task Types**
   - Group similar operations using same task types
   - Use `parallel` for independent operations
   - Leverage `dynamic_tasks` for bulk operations

3. **Concurrent Execution Settings**
   ```bash
   # Adjust based on your system
   weave workflow.yml --concurrent --jobs 8
   ```

### **🔧 Advanced Patterns**

#### **1. Multi-Stage Build Pipeline**
```yaml
imports:
  - "pipelines/build-stages.yml"
  - "pipelines/test-stages.yml"
  - "pipelines/deploy-stages.yml"

variables:
  DOCKER_REGISTRY: "my.registry.com"
  APP_VERSION: "{{CI_COMMIT_SHA | substring(0, 7)}}"

tasks:
  - name: multi_stage_pipeline
    type: group
    tasks:
      - compile_and_package
      - security_scan_parallel
      - integration_tests
      - staging_deployment
      - production_deployment

  - name: security_scan_parallel
    type: parallel
    tasks: [dependency_scan, code_scan, container_scan, secret_scan]
```

#### **2. Microservices Deployment**
```yaml
inputs:
  - name: services_config
    type: string
    default: "config/services.json"

tasks:
  - name: load_services_config
    type: run_command
    command: "cat {{services_config}}"
    outputs:
      output_json_to_variable: "services"
  
  - name: deploy_all_microservices
    type: dynamic_tasks
    depends_on: [load_services_config]
    items_variable: "{{services}}"
    task_template:
      name: "deploy_{{item.name}}"
      type: group
      tasks:
        - name: "build_{{item.name}}"
          type: run_command
          command: "docker build -t {{DOCKER_REGISTRY}}/{{item.name}}:{{APP_VERSION}} ./{{item.path}}"
        - name: "push_{{item.name}}"  
          type: run_command
          command: "docker push {{DOCKER_REGISTRY}}/{{item.name}}:{{APP_VERSION}}"
        - name: "deploy_{{item.name}}"
          type: run_command
          command: "kubectl set image deployment/{{item.name}} {{item.name}}={{DOCKER_REGISTRY}}/{{item.name}}:{{APP_VERSION}}"
```

#### **3. Error Handling & Recovery**
```yaml
defaults:
  retries:
    count: 3
    delay: "5s"

tasks:
  - name: resilient_deployment
    type: choose
    branches:
      - when: "{{deployment_strategy}} == 'blue-green'"
        tasks: [blue_green_deploy]
      - when: "{{deployment_strategy}} == 'canary'" 
        tasks: [canary_deploy]
      - when: "{{deployment_strategy}} == 'rolling'"
        tasks: [rolling_deploy]
    default: [emergency_rollback]
  
  - name: emergency_rollback
    type: run_command
    command: "kubectl rollout undo deployment/{{service_name}}"
    retries:
      count: 5
      delay: "2s"
```

### **📊 Monitoring & Observability**

```yaml
# Add monitoring to critical workflows
tasks:
  - name: deployment_with_monitoring
    type: group
    tasks:
      - name: pre_deploy_health_check
        type: run_command
        command: "health-check --pre-deploy"
        outputs:
          output_json_to_variable: "pre_deploy_status"
      
      - name: actual_deployment
        type: run_command
        command: "deploy --service {{service}} --version {{version}}"
        when: "{{pre_deploy_status.healthy}} == true"
        outputs:
          stdout_to_variable: "deploy_output"
          exit_code_to_variable: "deploy_exit_code"
      
      - name: post_deploy_verification
        type: run_command
        command: "verify-deployment --service {{service}}"
        depends_on: [actual_deployment]
        when: "{{deploy_exit_code}} == '0'"
        
      - name: alert_on_failure
        type: run_command
        command: "send-alert --message 'Deployment failed: {{deploy_output}}'"
        when: "{{deploy_exit_code}} != '0'"
```

---

**Weave v2.0**: Professional workflow orchestration with enterprise-grade performance and monitoring.
