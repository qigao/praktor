# Praktor Workflow DSL Specification

**Version:** 7.0
**Status:** Authoritative
**Last Updated:** October 2025

## Table of Contents

- [Praktor Workflow DSL Specification](#praktor-workflow-dsl-specification)
  - [Table of Contents](#table-of-contents)
  - [1. Introduction](#1-introduction)
    - [1.1. Purpose](#11-purpose)
    - [1.2. Design Philosophy](#12-design-philosophy)
  - [2. Core Concepts](#2-core-concepts)
  - [3. Execution Model: Context and Data Flow](#3-execution-model-context-and-data-flow)
    - [3.1. The Workflow Context](#31-the-workflow-context)
    - [3.2. Variable and Environment Precedence](#32-variable-and-environment-precedence)
    - [3.3. Data Flow Between Tasks](#33-data-flow-between-tasks)
  - [4. Workflow Structure](#4-workflow-structure)
    - [4.1. Embedded Modules](#41-embedded-modules)
  - [5. Task Definition](#5-task-definition)
    - [5.1. Core Attributes](#51-core-attributes)
    - [5.2. Control Flow \& Resilience](#52-control-flow--resilience)
    - [5.3. Execution Context](#53-execution-context)
  - [6. Task Composition: `uses`](#6-task-composition-uses)
    - [6.1. Executing a Reusable Workflow](#61-executing-a-reusable-workflow)
    - [6.2. Passing Data with `vars` and `env`](#62-passing-data-with-vars-and-env)
  - [7. Task Runner Reference](#7-task-runner-reference)
    - [7.1. Runner: `command` (Side Effects)](#71-runner-command-side-effects)
    - [7.2. Runner: `script` (Context Manipulation)](#72-runner-script-context-manipulation)
    - [7.3. Runner: `dynamic_tasks` (Runtime Task Generation)](#73-runner-dynamic_tasks-runtime-task-generation)
  - [8. Event-Driven Triggers](#8-event-driven-triggers)
    - [8.1. `http_post` - Send HTTP Webhook](#81-http_post---send-http-webhook)
    - [8.2. `write_file` - Create File Artifact](#82-write_file---create-file-artifact)
    - [8.3. `run_task` - Execute Recovery Task](#83-run_task---execute-recovery-task)
    - [8.5. Failure Context Variables](#85-failure-context-variables)
    - [8.4. `@praktor` - Praktor Notification Shortcut](#84-praktor---praktor-notification-shortcut)
  - [9. Expression Language](#9-expression-language)
  - [10. JSON Schema Contract](#10-json-schema-contract)
  - [11. Comprehensive Examples](#11-comprehensive-examples)
    - [11.1. Basic Workflow with Dependencies](#111-basic-workflow-with-dependencies)
    - [11.2. Reusable Workflow with Context Inheritance](#112-reusable-workflow-with-context-inheritance)
    - [11.3. Data Transformation with Script](#113-data-transformation-with-script)
    - [11.4. Loop Execution with Each](#114-loop-execution-with-each)
    - [11.5. Error Handling with Triggers](#115-error-handling-with-triggers)
  - [12. Security and Secrets Management](#12-security-and-secrets-management)
    - [12.1. Principle of Least Privilege](#121-principle-of-least-privilege)
    - [12.2. Secrets Backend Integration](#122-secrets-backend-integration)
    - [12.3. Log Redaction](#123-log-redaction)
    - [12.4. Best Practices](#124-best-practices)
  - [Appendix A: Complete Grammar Reference](#appendix-a-complete-grammar-reference)
    - [Workflow File Structure](#workflow-file-structure)
    - [Task Runner Summary](#task-runner-summary)

---

## 1. Introduction

### 1.1. Purpose
The Praktor Workflow Domain Specific Language (DSL) provides a declarative, YAML-based syntax for defining, managing, and executing complex workflows. This specification defines the grammar, data model, and execution semantics required to author interoperable, scalable, and maintainable automated processes.

### 1.2. Design Philosophy
*   **Declarative Graph:** Define tasks and their dependencies as a Directed Acyclic Graph (DAG). The runtime handles execution order and parallelization.
*   **Context-Driven:** Data flows implicitly through a shared context. Tasks read from and write to this context, eliminating rigid input/output contracts.
*   **Composable & Reusable:** Workflows are built from smaller components using `uses`. Composition relies on context and environment inheritance, not function-like signatures.
*   **Separation of Concerns:** Tasks that cause side effects (`run_command`) are distinct from tasks that perform in-memory data manipulation (`script`).
*   **Minimal & Expressive:** The DSL provides only essential primitives. Complex behavior emerges from composition, not built-in constructs.

## 2. Core Concepts
*   **Workflow:** The complete automated process defined in a YAML file.
*   **Task:** The fundamental unit of execution.
*   **Runner:** The execution engine for a task (`run_command`, `script`).
*   **Context:** The runtime data object holding variables, environment settings, and task outputs.
*   **Trigger:** A lightweight, event-driven action (e.g., an HTTP call) that fires upon task completion.

## 3. Execution Model: Context and Data Flow

### 3.1. The Workflow Context
The runtime maintains a global context that is accessible to all tasks. This context is a tree-like data structure holding `variables` and the `tasks` object, which stores the outputs of all completed tasks.

### 3.2. Variable and Environment Precedence
For any given task, variables are resolved in the following order (higher numbers override lower ones):
1.  Global `variables` / `env` from the workflow file.
2.  Variables / environment inherited from a parent `uses` task.
3.  Task-level `vars` / `env` defined directly on the task.

### 3.3. Data Flow Between Tasks
State is passed between tasks by writing to and reading from the context.
*   A `run_command` task produces output by writing to `stdout`, which is captured into the context.
*   A `script` task can both read from and write to any part of the context tree using the `context` module.

The outputs for a completed task `my-task` are stored at `tasks.my-task.outputs`.

## 4. Workflow Structure
A Praktor workflow is a YAML map with the following top-level keys:

| Key | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | No | Human-readable workflow identifier. |
| `description` | String | No | Optional workflow summary. |
| `variables` | Map<String, String> | No | Global constants available to all tasks. |
| `env` | Map<String, String> | No | Global environment variables exported to all tasks. |
| `dotEnv` | String or Array<String> | No | Paths to `.env` files to load into the global environment. |
| `defaults` | Map | No | Default `retries` and `timeout` applied to all tasks. |
| `embedded` | Map<String, Module> | No | Embedded script modules that can be loaded by runtimes. |
| `tasks` | Array<Task> | **Yes** | The list of task definitions. |

### 4.1. Embedded Modules
The optional `embedded` map lets a workflow bundle reusable script modules. Each entry's key becomes the module name; its value must provide a `source` string (JavaScript by default) and may set an explicit `language`.

```yaml
embedded:
  string_utils:
    source: |
      export function slugify(value) {
        return value.toLowerCase().replace(/[^a-z0-9]+/g, "-");
      }
```

Runtimes can expose these modules to `script` tasks (for example, by preloading modules listed in `script.modules`).

**Example:**
```yaml
variables:
  APP_NAME: "my-app"
  VERSION: "1.0.0"

env:
  NODE_ENV: "production"

dotEnv:
  - ".env.production"

defaults:
  retries:
    count: 2
    delay: "5s"
  timeout: "10m"

tasks:
  - name: build
    command: "npm run build"
```

## 5. Task Definition

Every task must have a `name` and exactly one runner (`command`, `script`, or `uses`).

### 5.1. Core Attributes

| Key | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | **Yes** | A unique identifier for the task. |
| `description` | String | No | Human-readable summary of the task. |
| `depends_on` | String or Array<String> | No | Task names that must complete before this task runs. |
| `vars` | Map<String, String> | No | Task-scoped variables. Override globals and are inherited by `uses` tasks. |
| `env` | Map<String, String> | No | Task-scoped environment variables. |
| `dotEnv` | String or Array<String> | No | Paths to `.env` files to load for this task. |

**Runner (exactly one required):**
- `command` (String or Array): Execute an external command. See Section 7.1.
- `script` (Object): Execute JavaScript code. See Section 7.2.
- `uses` (String): Execute a reusable workflow file. See Section 6.
- `dynamic_tasks` (Object): Execute tasks generated at runtime. See Section 7.3.

### 5.2. Control Flow & Resilience

| Key | Type | Description |
| :--- | :--- | :--- |
| `when` | String | An expression that must evaluate to `true` for the task to run. |
| `each` | Object | Execute the task multiple times over a list or matrix. |
| `retries` | Object | Retry policy with `count` (integer) and `delay` (duration string). |
| `timeout` | String | Maximum execution time (e.g., `"30s"`, `"5m"`, `"1h"`). |
| `triggers` | Object | Event-driven actions to execute on task completion. See Section 8. |
| `continue_on_error` | Boolean | If `true`, workflow continues even if this task fails. Default: `false`. |

### 5.3. Execution Context

| Key | Type | Description |
| :--- | :--- | :--- |
| `working_dir` | String | Working directory for command execution. Relative paths resolve from workflow file location. |
| `silent` | Boolean | Suppress command output from logs. Default: `false`. |

**Example:**
```yaml
tasks:
  - name: deploy
    depends_on: [build, test]
    command: "./deploy.sh"
    when: "{{ env.ENVIRONMENT }} == 'production'"
    retries:
      count: 3
      delay: "10s"
    timeout: "15m"

  # Run all tests, collect all failures
  - name: test_unit
    command: "npm run test:unit"
    continue_on_error: true

  - name: test_integration
    command: "npm run test:integration"
    continue_on_error: true

  # Monorepo: build each package in its directory
  - name: build_frontend
    working_dir: "./packages/frontend"
    command: "npm run build"

  - name: build_backend
    working_dir: "./packages/backend"
    command: "cargo build --release"
```

## 6. Task Composition: `uses`

### 6.1. Executing a Reusable Workflow
The `uses` keyword executes another workflow file as a task. This creates a nested, scoped execution of the target workflow.

| Key | Type | Description |
| :--- | :--- | :--- |
| `uses` | String | Path to a reusable workflow file. Mutually exclusive with runner keys. |

### 6.2. Passing Data with `vars` and `env`
Instead of a formal contract, data is passed to a reusable workflow via the calling task's context.
*   The `vars` map of the calling task is inherited as the `variables` of the reusable workflow.
*   The `env` map of the calling task is merged into the environment of the reusable workflow.

The reusable workflow can set its own outputs using `context.set()` in a `script` task. These outputs are namespaced and available to the parent workflow at `{{ tasks.<calling_task_name>.outputs.<key> }}`.

## 7. Task Runner Reference

### 7.1. Runner: `command` (Side Effects)

Executes external programs and captures their output.

**Attributes:**
- `command` (String or Array, **Required**): The command to execute.
- `output_format` (String, Optional): How to interpret stdout. Values: `text` (default) or `json`.

**Output Capture:**
- Text output: `{{ tasks.<task_name>.outputs.stdout }}`
- JSON output: `{{ tasks.<task_name>.outputs.data }}` (parsed as JSON object)
- Exit code: `{{ tasks.<task_name>.outputs.exit_code }}`
- Stderr: `{{ tasks.<task_name>.outputs.stderr }}`

**Example:**
```yaml
tasks:
  - name: get_version
    command: "git describe --tags"
    # Output available at: tasks.get_version.outputs.stdout

  - name: fetch_config
    command: "curl -s https://api.example.com/config"
    output_format: json
    # Output available at: tasks.fetch_config.outputs.data.version
```

### 7.2. Runner: `script` (Context Manipulation)

Executes JavaScript code for in-memory data transformation and context manipulation.

**Attributes:**
- `script.source` (String, **Required**): The JavaScript code to execute.
- `script.language` (String, Optional): Execution language. Default: `javascript`.
- `script.modules` (Array<String>, Optional): List of embedded module names to load before execution.
- `script.globals` (Map<String, String>, Optional): Global variables injected into the script environment.
- `script.env` (Map<String, String>, Optional): Environment variables for script execution.

**Context API:**
- `context.get(path)`: Retrieves a value from the context (e.g., `"tasks.build.outputs.version"`).
- `context.set(key, value)`: Writes a JSON-serializable value to the current task's outputs.

**ES6 Module Imports:**

Scripts can use ES6 `import` syntax to load built-in TurboNet modules or external JavaScript files. The runtime automatically detects `import` statements and switches to module mode.

*Built-in Modules:*

| Module | Description |
| :--- | :--- |
| `turbo:fs` | File system operations (readFile, writeFile, stat, readdir, mkdir) |
| `turbo:os` | OS info (hostname, homedir, cwd, getenv, pid) |
| `turbo:dns` | DNS resolution (resolve, resolveAsync) |
| `turbo:http` | HTTP client (get, post, request) |
| `turbo:timers` | Timers (setTimeout, setInterval, sleep) |
| `turbo:utils` | Utilities (base64Encode, base64Decode) |
| `turbo:net` | Networking (TcpClient, WebSocket) |
| `turbo:signal` | Signal handling (watch, SIGINT, SIGTERM) |
| `turbo:proc` | Process management (spawn, kill) |

*Import Syntax:*
```javascript
// Default import
import fs from 'turbo:fs';

// Named imports
import { readFile, writeFile } from 'turbo:fs';
import { hostname, cwd } from 'turbo:os';

// External file modules (absolute or relative path)
import myModule from './lib/helpers.js';
import { myFunction } from '/path/to/module.js';
```

*Global Object (Legacy):*

Scripts without `import` statements can use the global `turbo` object:
```javascript
turbo.fs.stat(".");
turbo.os.hostname();
turbo.http.get("https://api.example.com");
```

**Example:**
```yaml
tasks:
  - name: calculate_tag
    script:
      source: |
        const version = context.get("tasks.get_version.outputs.stdout").trim();
        const env = context.get("ENVIRONMENT");
        const tag = `${version}-${env}`;
        context.set("docker_tag", tag);
    # Output available at: tasks.calculate_tag.outputs.docker_tag

  - name: with_modules
    script:
      modules: [string_utils]  # Load embedded module defined in workflow
      globals:
        PREFIX: "v"
      source: |
        const tag = slugify(context.get("name"));
        context.set("slug", PREFIX + tag);

  - name: with_es6_imports
    script:
      source: |
        import fs from 'turbo:fs';
        import { hostname } from 'turbo:os';

        const config = fs.readJson('./config.json');
        context.set("host", hostname());
        context.set("config_version", config.version);

  - name: with_external_module
    script:
      source: |
        import { processData } from './lib/data-processor.js';

        const input = context.get("tasks.fetch.outputs.data");
        const result = processData(input);
        context.set("processed", result);
```

### 7.3. Runner: `dynamic_tasks` (Runtime Task Generation)

Generates and executes tasks dynamically at runtime based on data from the context. This enables data-driven workflows where the number and configuration of tasks is determined by runtime values.

**Attributes:**
- `dynamic_tasks.items_variable` (String, **Required**): Context path to a JSON array that drives task generation.
- `dynamic_tasks.template` (Object, **Required**): Task template with `{{ item }}` placeholders.

**Template Placeholders:**
- `{{ item }}`: The current item (string or JSON object).
- `{{ item.field }}`: Access a specific field when item is an object.
- `{{ index }}`: The zero-based index of the current item.

**Example:**
```yaml
tasks:
  - name: discover_services
    command: "curl -s https://api.example.com/services"
    output_format: json
    # Output: tasks.discover_services.outputs.data = [{"name": "auth", "port": 8080}, ...]

  - name: deploy_all_services
    depends_on: [discover_services]
    dynamic_tasks:
      items_variable: "{{ tasks.discover_services.outputs.data }}"
      template:
        name: "deploy_{{ item.name }}"
        command: "./deploy.sh --service {{ item.name }} --port {{ item.port }}"
        timeout: "5m"
        retries:
          count: 2
          delay: "10s"
```

**Use Cases:**
- Deploy to multiple environments discovered at runtime
- Process files found by a previous task
- Run tests for dynamically discovered modules
- Fan-out operations based on API responses

## 8. Event-Driven Triggers

A `triggers` block defines event-driven actions that execute after a task completes. Triggers simply reference other tasks by name, allowing any task type (script, command, uses) to be executed as a trigger action.

**Events:**
- `on_success`: Fires when the task completes successfully.
- `on_failure`: Fires when the task fails (after all retries).
- `on_complete`: Fires regardless of success or failure.

**Trigger Actions:**

Triggers are specified as an array of task names (strings). When a trigger event fires, the referenced tasks are looked up and executed.

**Example:**
```yaml
tasks:
  # Define notification tasks
  - name: notify_slack
    script:
      source: |
        import { post } from 'turbo:http';
        const webhook = context.get("env.SLACK_WEBHOOK");
        const message = context.get("tasks.deploy.outputs.version");
        post(webhook, JSON.stringify({text: `Deployed ${message}`}));

  - name: write_log
    script:
      source: |
        import fs from 'turbo:fs';
        const output = context.get("tasks.deploy.outputs.stdout");
        fs.writeFile("./logs/deploy.log", output);

  - name: rollback
    command: "./rollback.sh --reason 'Deploy failed'"

  # Main task with triggers
  - name: deploy
    command: "./deploy.sh --env production"
    retries:
      count: 3
      delay: "10s"
    triggers:
      on_success: 
        - notify_slack
        - write_log
      on_failure:
        - rollback
        - notify_slack
```

### 8.1. Simple Trigger References

The simplest form is a list of task names:
```yaml
triggers:
  on_failure: [rollback_deployment, alert_team]
  on_success: [update_dashboard]
```

### 8.2. Writing Files as Triggers

Instead of specialized `write_file` trigger actions, use a script task:
```yaml
tasks:
  - name: save_build_log
    script:
      source: |
        import fs from 'turbo:fs';
        const buildLog = context.get("tasks.build.outputs.stdout");
        fs.writeFile("./logs/build.log", buildLog);

  - name: build
    command: "npm run build"
    triggers:
      on_complete: [save_build_log]
```

### 8.3. HTTP Notifications as Triggers

Instead of specialized `http_post` trigger actions, use a script task:
```yaml
tasks:
  - name: notify_webhook
    script:
      source: |
        import { post } from 'turbo:http';
        const url = context.get("env.WEBHOOK_URL");
        const status = context.get("tasks.deploy.status");
        const body = JSON.stringify({
          text: `Deployment ${status}`,
          version: context.get("tasks.deploy.outputs.version")
        });
        post(url, body, {"Content-Type": "application/json"});

  - name: deploy
    command: "./deploy.sh"
    triggers:
      on_success: [notify_webhook]
      on_failure: [notify_webhook]
```

### 8.4. Failure Context Variables

When a task fails, the following variables are available in triggered tasks to provide diagnostic information:

| Variable | Description |
| :--- | :--- |
| `failed_task_name` | Name of the failed task. |
| `failed_task_type` | Type of runner used (`command`, `script`, `uses`, `dynamic_tasks`). |
| `failed_task_exit_code` | Process exit code (available for `command` runner). |
| `failed_task_stdout` | The captured standard output of the failed task. |
| `failed_task_stderr` | The captured standard error of the failed task. |
| `failed_task_error` | The primary error message describing the failure. |

These variables can be accessed in triggered tasks using the standard `{{ variable_name }}` syntax or via `context.get()`.

**Example:**
```yaml
tasks:
  - name: alert_on_failure
    script:
      source: |
        import { post } from 'turbo:http';
        const taskName = context.get("failed_task_name");
        const error = context.get("failed_task_error");
        const stderr = context.get("failed_task_stderr");
        
        const message = `Task '${taskName}' failed: ${error}\n\nStderr:\n${stderr}`;
        post(context.get("env.ALERT_WEBHOOK"), JSON.stringify({text: message}));

  - name: deploy
    command: "./deploy.sh"
    triggers:
      on_failure: [alert_on_failure]
```

## 9. Templating and Expressions

Praktor uses a two-tier system for dynamic values:
1. **Mustache Templating**: Used for variable substitution in strings (commands, paths, messages).
2. **Expression Evaluation**: Used for logical conditions (the `when` clause).

### 9.1. Mustache Templating

Standard string substitution uses the [Mustache](https://mustache.github.io/) templating engine. This provides powerful formatting capabilities beyond simple variable replacement.

**Basic Variables:**
- `{{ VERSION }}` - Global variable
- `{{ tasks.build.outputs.stdout }}` - Task output
- `{{ env.HOME }}` - Environment variable

**Sections and Loops:**
Sections can be used to iterate over arrays or conditionally render blocks.
```yaml
# Iterating over a list of artifacts
command: |
  echo "Build Artifacts:"
  {{#tasks.build.outputs.data.artifacts}}
  echo "- {{name}} ({{size}} bytes)"
  {{/tasks.build.outputs.data.artifacts}}
```

**Inverted Sections:**
Only rendered if the value is `false`, `null`, `undefined`, or an empty list.
```yaml
# Show a message if no tests were run
triggers:
  on_success:
    - script:
        source: |
          console.log("{{^tasks.test.outputs.data.results}}No tests were executed.{{/tasks.test.outputs.data.results}}");
```

**Object Access:**
You can access nested fields using dot notation: `{{ tasks.fetch.outputs.data.user.id }}`.

### 9.2. Expression Language (for `when`)

The `when` clause uses a specialized expression evaluator that supports logical and arithmetic operations.

**Syntax:** Expressions are wrapped in `{{ ... }}`.

**Operators:**
- **Comparison**: `==`, `!=`, `<`, `>`, `<=`, `>=`
- **Logical**: `and`, `or`, `not` (also supports `!` for negation)
- **String Operations**: `contains`, `starts_with`, `ends_with`, `in`, `matches` (Regex)
- **Arithmetic**: `+`, `-`, `*`, `/`, `%`
- **Grouping**: `(`, `)`

**Built-in Functions:**
- `len(value)` - Length of string or array
- `empty(value)` - Check if value is empty
- `abs(number)` - Absolute value
- `bool(value)` - Convert to boolean

**Example with both:**
```yaml
tasks:
  - name: deploy
    command: "./deploy.sh --env {{ ENVIRONMENT }}" # Mustache substitution
    when: "{{ ENVIRONMENT }} == 'production' and not empty({{ API_KEY }})" # Expression
```


**Performance Notes:**
- Expression parsing uses optimized PEG grammar for fast evaluation
- Type conversions are cached and unified for better performance
- Operator parsing uses lookup tables instead of linear searches

## 10. JSON Schema Contract
A canonical JSON Schema **MUST** be provided to validate workflow syntax and provide editor support. The authoritative copy lives at [JSON Grammar](./grammar.schema.json).
## 11. Comprehensive Examples

### 11.1. Basic Workflow with Dependencies

```yaml
variables:
  PROJECT_NAME: "my-app"
  BUILD_DIR: "./dist"

tasks:
  - name: clean
    command: "rm -rf {{ BUILD_DIR }}"

  - name: install
    depends_on: [clean]
    command: "npm install"

  - name: build
    depends_on: [install]
    command: "npm run build"
    timeout: "10m"

  - name: test
    depends_on: [build]
    command: "npm test"
    retries:
      count: 2
      delay: "5s"
```

### 11.2. Reusable Workflow with Context Inheritance

**File: `reusable/docker-build.yml`**
```yaml
# This reusable workflow expects IMAGE_NAME, TAG, and REGISTRY_URL
# to be provided via 'vars' by the calling task.

tasks:
  - name: set_full_tag
    script:
      source: |
        const full_tag = `{{ REGISTRY_URL }}/{{ IMAGE_NAME }}:{{ TAG }}`;
        context.set("full_image_tag", full_tag);

  - name: docker_build
    depends_on: [set_full_tag]
    command: "docker build -t {{ tasks.set_full_tag.outputs.full_image_tag }} ."

  - name: docker_push
    depends_on: [docker_build]
    command: "docker push {{ tasks.set_full_tag.outputs.full_image_tag }}"
```

**File: `ci-pipeline.yml`**
```yaml
variables:
  APP_NAME: "web-app"
  REGISTRY: "registry.example.com"

env:
  DOCKER_BUILDKIT: "1"

tasks:
  - name: get_version
    command: "git describe --tags --always"

  - name: build_image
    depends_on: [get_version]
    uses: ./reusable/docker-build.yml
    vars:
      IMAGE_NAME: "{{ APP_NAME }}"
      TAG: "{{ tasks.get_version.outputs.stdout }}"
      REGISTRY_URL: "{{ REGISTRY }}"

  - name: notify_slack
    script:
      source: |
        import { post } from 'turbo:http';
        const url = context.get("env.SLACK_WEBHOOK_URL");
        const image = context.get("tasks.build_image.outputs.full_image_tag");
        post(url, JSON.stringify({text: `Deployment failed for ${image}`}));

  - name: deploy
    depends_on: [build_image]
    command: "./scripts/deploy.sh --image {{ tasks.build_image.outputs.full_image_tag }}"
    when: "{{ ENVIRONMENT }} == 'production'"
    triggers:
      on_failure: [notify_slack]
```

### 11.3. Data Transformation with Script

```yaml
tasks:
  - name: fetch_config
    command: "curl -s https://api.example.com/config"
    output_format: json

  - name: process_config
    depends_on: [fetch_config]
    script:
      source: |
        const config = context.get("tasks.fetch_config.outputs.data");
        const version = config.version;
        const features = config.features.filter(f => f.enabled);

        context.set("app_version", version);
        context.set("enabled_features", features.map(f => f.name).join(","));

  - name: deploy_with_features
    depends_on: [process_config]
    command: |
      ./deploy.sh \
        --version {{ tasks.process_config.outputs.app_version }} \
        --features {{ tasks.process_config.outputs.enabled_features }}
```

### 11.4. Loop Execution with Each

```yaml
tasks:
  - name: test_services
    each:
      items: ["auth", "api", "worker"]
      as: "service"
    command: "./test-service.sh {{ service }}"

  - name: build_matrix
    each:
      matrix:
        os: ["linux", "windows", "macos"]
        arch: ["amd64", "arm64"]
      as: "config"
    command: "./build.sh --os {{ config.os }} --arch {{ config.arch }}"
```

### 11.5. Error Handling with Triggers

```yaml
tasks:
  - name: save_deploy_log
    script:
      source: |
        import fs from 'turbo:fs';
        const output = context.get("tasks.deploy_app.outputs.stdout");
        fs.writeFile("./logs/deploy-success.log", `Deployed at ${output}`);

  - name: notify_team
    script:
      source: |
        import { post } from 'turbo:http';
        const webhook = context.get("env.SLACK_WEBHOOK");
        const error = context.get("failed_task_error") || "Success";
        post(webhook, JSON.stringify({text: `Deploy status: ${error}`}));

  - name: rollback
    command: "./rollback.sh --reason 'Deploy failed'"

  - name: deploy_app
    command: "./deploy.sh --env production"
    retries:
      count: 3
      delay: "10s"
    triggers:
      on_failure: [rollback, notify_team]
      on_success: [save_deploy_log, notify_team]
```

## 12. Security and Secrets Management

### 12.1. Principle of Least Privilege
Use task-scoped `env` and `dotEnv` to expose secrets only to tasks that need them:

```yaml
tasks:
  - name: deploy
    env:
      AWS_ACCESS_KEY_ID: "{{ env.AWS_KEY }}"
      AWS_SECRET_ACCESS_KEY: "{{ env.AWS_SECRET }}"
    command: "./deploy.sh"
```

### 12.2. Secrets Backend Integration
Runtimes **SHOULD** integrate with dedicated secrets managers:
- HashiCorp Vault
- AWS Secrets Manager
- Azure Key Vault
- Environment-specific secret stores

### 12.3. Log Redaction
Runtimes **MUST** automatically redact values from logs when keys match common secret patterns:
- `*_SECRET`, `*_PASSWORD`, `*_TOKEN`, `*_KEY`
- `API_KEY`, `PRIVATE_KEY`, `ACCESS_TOKEN`

### 12.4. Best Practices
- Never commit secrets to version control
- Use `.env` files (gitignored) for local development
- Use secret managers for production environments
- Rotate secrets regularly
- Audit secret access through workflow logs

---

## Appendix A: Complete Grammar Reference

### Workflow File Structure
```yaml
# Optional metadata
name: "Workflow Name"
description: "Workflow description"

# Optional global configuration
variables:
  KEY: "value"

env:
  ENV_VAR: "value"

dotEnv:
  - ".env"

defaults:
  retries:
    count: 2
    delay: "5s"
  timeout: "10m"

# Required: at least one task
tasks:
  - name: "task_name"
    depends_on: ["other_task"]
    vars:
      TASK_VAR: "value"
    env:
      TASK_ENV: "value"
    dotEnv:
      - ".env.task"
    when: "{{ condition }}"
    each:
      items: ["a", "b"]
      as: "item"
    retries:
      count: 3
      delay: "10s"
    timeout: "15m"

    # Exactly one runner:
    command: "echo hello"
    # OR
    script:
      source: "context.set('key', 'value');"
    # OR
    uses: "./path/to/workflow.yml"
    # OR
    dynamic_tasks:
      items_variable: "{{ my_items }}"
      template:
        name: "subtask_{{ item }}"
        command: "echo {{ item }}"

    # Optional triggers
    triggers:
      on_success: []
      on_failure: []
      on_complete: []
```

### Task Runner Summary

| Runner | Purpose | Output Location |
| :--- | :--- | :--- |
| `command` | Execute external programs | `tasks.<name>.outputs.stdout` |
| `script` | JavaScript data transformation | `tasks.<name>.outputs.<key>` (via `context.set`) |
| `uses` | Execute reusable workflow | `tasks.<name>.outputs.*` (from nested tasks) |
| `dynamic_tasks` | Runtime task generation | Outputs from each generated task |

---
