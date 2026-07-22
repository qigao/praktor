# Praktor Workflow DSL Specification

**Version:** 9.1
**Status:** Authoritative
**Last Updated:** April 2026

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
    - [4.1. Native Modules (DLL/SO)](#41-native-modules-dllso)
  - [5. Task Definition](#5-task-definition)
    - [5.1. Core Attributes](#51-core-attributes)
    - [5.2. Control Flow \& Resilience](#52-control-flow--resilience)
    - [5.3. Execution Context](#53-execution-context)
  - [6. Task Composition: `uses`](#6-task-composition-uses)
    - [6.1. Executing a Reusable Workflow](#61-executing-a-reusable-workflow)
    - [6.2. Passing Data with `vars` and `env`](#62-passing-data-with-vars-and-env)
  - [7. Task Runner Reference](#7-task-runner-reference)
    - [7.1. Runner: `command` (Shell Command)](#71-runner-command-shell-command)
    - [7.2. Runner: `program` (Raw Process)](#72-runner-program-raw-process)
    - [7.3. Post-Processing: `script` (Context Manipulation)](#73-post-processing-script-context-manipulation)
    - [7.4. Runner: `dynamic_tasks` (Runtime Task Generation)](#74-runner-dynamic_tasks-runtime-task-generation)
    - [7.5. HTTP Requests via Script](#75-http-requests-via-script)
    - [7.6. Runner: Actions (Declarative Control Flow)](#76-runner-actions-declarative-control-flow)
  - [8. Event-Driven Triggers](#8-event-driven-triggers)
    - [8.1. Simple Trigger References](#81-simple-trigger-references)
    - [8.2. Failure Context Variables](#82-failure-context-variables)
  - [9. Templating and Expressions](#9-templating-and-expressions)
    - [9.1. Mustache Templating](#91-mustache-templating)
    - [9.2. Expression Language (for `when`)](#92-expression-language-for-when)
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
    - [Script Built-in Modules](#script-built-in-modules)

---

## 1. Introduction

### 1.1. Purpose

The Praktor Workflow Domain Specific Language (DSL) provides a declarative, YAML-based syntax for defining, managing, and executing complex workflows. This specification defines the grammar, data model, and execution semantics required to author interoperable, scalable, and maintainable automated processes.

### 1.2. Design Philosophy

* **Declarative Graph:** Define tasks and their dependencies as a Directed Acyclic Graph (DAG). The runtime handles execution order and parallelization.
- **Context-Driven:** Data flows implicitly through a shared context. Tasks read from and write to this context, eliminating rigid input/output contracts.
- **Composable & Reusable:** Workflows are built from smaller components using `uses`. Composition relies on context and environment inheritance, not function-like signatures.
- **Post-Processing Scripts:** Any task can attach an inline `script` block for in-memory data transformation after the runner completes. Scripts use a lightweight built-in language with access to context, JSON, HTTP, logging, and native modules.
- **Minimal & Expressive:** The DSL provides only essential primitives. Complex behavior emerges from composition, not built-in constructs.

## 2. Core Concepts

* **Workflow:** The complete automated process defined in a YAML file.
- **Task:** The fundamental unit of execution.
- **Runner:** The execution engine for a task (`command`, `program`, `uses`, `dynamic_tasks`, or orchestration nodes).
- **Script:** An optional post-processing block attached to any task for in-memory data transformation.
- **Context:** The runtime data object holding variables, environment settings, and task outputs.
- **Trigger:** A lightweight, event-driven action (e.g., an HTTP call) that fires upon task completion.

### Two-Level Orchestration

Praktor defines orchestration at two separate levels:

1. **Workflow-level orchestration (DAG):**
   decides when a task runs, how many times it runs, what it depends on, and what happens after it finishes.
   This includes flow-level task properties such as `depends_on`, `when`, `each`, `triggers`, and `script`.
2. **Runner-level execution:**
   defines which executor performs the task's work.
   This is where `command`, `program`, orchestration nodes (`sequence`, `parallel`, etc.), `uses`, and `dynamic_tasks` live.

Therefore:

- Flow-level task properties apply to the whole task, regardless of runner.
- `command` is a shell-command runner. `program` is a raw-process runner that passes an argument vector without shell parsing.
- Orchestration nodes (like `sequence`) are runners that execute complex logic inside one task.
- `command` is not a BT node and is not part of BT grammar.
- YAML is the canonical and only workflow authoring grammar; runtimes should not require a separate text BT language.
- BT nodes themselves are not DAG tasks and do not carry their own workflow-level `when` or `each`.

**Model Diagram**

```text
Workflow / DAG layer
  - depends_on
  - when
  - each
  - triggers
  - script
            │
            ▼
  - command
  - program
  - uses
  - dynamic_tasks
  - sequence / parallel / ... (orchestration)
            │
            ▼
Internal execution (if orchestration node)
  - shell / parse_json / parse_regex / ...
```

**Canonical Reading Rule**

Read a task in this order:

1. Apply workflow-level controls to the task as a whole.
2. Select exactly one runner for the task.
3. If the runner is an orchestration node (like `sequence`), execute the internal nodes inside that one task instance.

**Examples**

External-process runner:

```yaml
tasks:
  - name: build
    when: "{{ ENV }} == 'prod'"
    command: "./build.sh"
```

Raw-process runner:

```yaml
tasks:
  - name: run_tool
    program: ./tool
    args: ["--input", "{{ tasks.prepare.outputs.path }}"]
    stdin: "{{ tasks.prepare.outputs.payload }}"
```

Action runner:

```yaml
tasks:
  - name: deploy
    each:
      items: ["api", "worker"]
      as: service
    sequence:
      - shell:
          cmd: "./deploy.sh {{ service }}"
      - parse_json:
          path: "$.status"
          output_key: "status"
```

In the second example, `each` repeats the whole `deploy` task. It does not repeat one action node in isolation. The action structure is internal to each generated task execution.

## 3. Execution Model: Context and Data Flow

### 3.1. The Workflow Context

The runtime maintains a global context that is accessible to all tasks. This context is a tree-like data structure holding `variables` and the `tasks` object, which stores the outputs of all completed tasks.

### 3.2. Variable and Environment Precedence

For any given task, variables are resolved in the following order (higher numbers override lower ones):

1. Global `variables` / `env` from the workflow file.
2. Variables / environment inherited from a parent `uses` task.
3. Task-level `vars` / `env` defined directly on the task.

### 3.3. Data Flow Between Tasks

State is passed between tasks by writing to and reading from the context.
- A `command` or `program` task produces output by writing to `stdout`, which is captured into the context.
- Any task with a `script` block can both read from and write to any part of the context tree using the `ctx` module.

The outputs for a completed task `my-task` are stored at `tasks.my-task.outputs`.

When a task uses `each`, `tasks.my-task.outputs.iterations` contains one entry per iteration with `index`, `item`, `status`, and `outputs`. For compatibility, the top-level outputs still mirror the last non-skipped iteration.

## 4. Workflow Structure

A Praktor workflow is a YAML map with the following top-level keys:

| Key | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | No | Human-readable workflow identifier. |
| `description` | String | No | Optional workflow summary. |
| `variables` | Map<String, String> | No | Global constants available to all tasks. |
| `env` | Map<String, String> | No | Global environment variables exported to all tasks. |
| `dotEnv` | String or Array<String> | No | Paths to `.env` files to load into the global environment. |
| `defaults` | Map | No | Default `timeout` applied to all tasks. |
| `tasks` | Array<Task> | **Yes** | The list of task definitions. |

## 5. Task Definition

Every task must have a `name` and exactly one runner (`command`, `program`, `uses`, `dynamic_tasks`, or an orchestration node root key). Any task can optionally include flow-level properties such as `when`, `each`, `triggers`, and `script`.

Task attributes such as `when`, `each`, `triggers`, and `script` are evaluated at the workflow layer before or around runner execution. If the runner is an orchestration node, those controls still apply to the whole task, not to individual internal nodes.

### 5.1. Core Attributes

| Key | Type | Required | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | **Yes** | A workflow-wide unique identifier for the task. |
| `description` | String | No | Human-readable summary of the task. |
| `depends_on` | String or Array<String> | No | Task names that must complete before this task runs. |
| `vars` | Map<String, String> | No | Task-scoped variables. Override globals and are inherited by `uses` tasks. |
| `env` | Map<String, String> | No | Task-scoped environment variables. |
| `dotEnv` | String or Array<String> | No | Paths to `.env` files to load for this task. |

**Runner (exactly one required):**

- `command` (String or Array): Execute a shell command. See Section 7.1.
- `program` (String): Execute a raw process with optional `args` and `stdin`. See Section 7.2.
- `uses` (String): Execute a reusable workflow file. See Section 6.
- `dynamic_tasks` (Object): Execute tasks generated at runtime. See Section 7.4.

**Post-Processing (optional):**

- `script` (String): Inline script for data transformation after the runner completes. See Section 7.3.

### 5.2. Control Flow & Resilience

| Key | Type | Description |
| :--- | :--- | :--- |
| `when` | String | An expression that must evaluate to `true` for the task to run. |
| `each` | Object | Execute the task multiple times over a list or matrix. |
| `timeout` | String or Number | Maximum execution time. String format: `"30s"`, `"5m"`, `"1h"`. Number format: milliseconds. See below for dual semantics. |
| `triggers` | Object | Event-driven actions to execute on task completion. See Section 8. |

**IMPORTANT: `timeout` Dual Semantics**

The `timeout` key has **two different interpretations** depending on context:

1. **Task-level timeout** (applies to the entire task):
   - Format: String with unit (`"30s"`, `"5m"`, `"1h"`) or plain number (interpreted as milliseconds)
   - Applies when: `timeout` is a top-level task attribute **without** a `child:` key
   - Example:
     ```yaml
     - name: build
       command: "npm run build"
       timeout: "5m"  # Task-level: entire build must complete in 5 minutes
     ```

2. **Action Timeout decorator** (wraps a single child node):
   - Format: Object with `timeout_ms` (number) and `child` keys, OR flat syntax with `timeout` (number) and `child` keys
   - Applies when: Used as an orchestration node with a `child:` key
   - Example:
     ```yaml
     - name: guarded_shell
       timeout: 5000  # Action decorator: child node must complete in 5000ms
       child:
         shell: "long_task.sh"
     ```

The parser distinguishes between these two cases by checking for the presence of `child:` in the same map as `timeout`. This dual usage can cause confusion if not carefully documented.

### 5.3. Execution Context

| Key | Type | Description |
| :--- | :--- | :--- |
| `working_dir` | String | Working directory for command or program execution. Relative paths resolve from workflow file location. |
| `silent` | Boolean | Suppress command output from logs. Default: `false`. |

**Example:**

```yaml
tasks:
  - name: deploy
    depends_on: [build, test]
    command: "./deploy.sh"
    when: "{{ env.ENVIRONMENT }} == 'production'"
    timeout: "15m"

  - name: test_unit
    command: "npm run test:unit"

  - name: test_integration
    command: "npm run test:integration"

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
- The `vars` map of the calling task is inherited as the `variables` of the reusable workflow.
- The `env` map of the calling task is merged into the environment of the reusable workflow.

The reusable workflow can set its own outputs using `ctx.output()` in a `script` block. These outputs are namespaced and available to the parent workflow at `{{ tasks.<calling_task_name>.outputs.<key> }}`. The parent also receives `tasks.<calling_task_name>.outputs.nested_tasks`, which contains per-task status and outputs for the nested workflow.

## 7. Task Runner Reference

### 7.1. Runner: `command` (Shell Command)

Executes a shell command and captures its output.

Implementation note: `command` is user-facing shell syntax. Runtimes may lower it internally, but it remains distinct from BT `shell` nodes and from the raw `program` runner.

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

### 7.2. Runner: `program` (Raw Process)

Executes one program directly. It does not invoke `cmd.exe`, `/bin/sh`, or shell expansion unless you explicitly choose a shell as the `program`.

**Attributes:**

- `program` (String, **Required**): Executable name, absolute path, or relative path. Relative paths resolve from the workflow file location.
- `args` (Array<String>, Optional): Argument vector passed as-is after Mustache substitution.
- `stdin` (String, Optional): Data written to the process standard input.
- `output_format` (String, Optional): How to interpret stdout. Values: `text` (default) or `json`.

**Output Capture:**

- Text output: `{{ tasks.<task_name>.outputs.stdout }}`
- JSON output: `{{ tasks.<task_name>.outputs.data }}`
- Exit code: `{{ tasks.<task_name>.outputs.exit_code }}`
- Stderr: `{{ tasks.<task_name>.outputs.stderr }}`

**Example:**

```yaml
tasks:
  - name: inspect_config
    program: python
    args: ["./tools/read_config.py", "--format", "json"]
    stdin: "{{ tasks.fetch_config.outputs.stdout }}"
    output_format: json
```

### 7.3. Post-Processing: `script` (Context Manipulation)

The `script` field is an optional inline string that runs after a task's runner completes. It uses a custom built-in scripting language for in-memory data transformation and context manipulation. Any task type (`command`, `program`, `uses`, `dynamic_tasks`) can attach a `script` block.

External script capabilities are loaded exclusively through TurboScript plugins with `import("name")`, for example `import("net")`, `import("parser")`, or `import("rules_forge")`. Workflow files do not accept native library paths or define a separate plugin ABI.

**Syntax:**

- `script` (String, Optional): Inline script code as a YAML block scalar.

**Built-in Modules:**

The script language provides 8 built-in modules available in every script:

| Module | Description |
| :--- | :--- |
| `ctx` | Read/write workflow context |
| `json` | JSON parsing, serialization, and querying |
| `http` | Async HTTP client (TurboNet) |
| `fs` | File system operations via turbo_fs |
| `shell` | Execute shell commands (limited) |
| `base64` | Base64 encoding/decoding |
| `log` | Logging at various levels |
| `math` | Math functions + exprtk expression evaluator |

**`ctx` — Context Access:**

- `ctx.get(path)`: Retrieves a value from the context (e.g., `"tasks.build.outputs.stdout"`).
- `ctx.output(key, value)`: Writes a value to the current task's outputs.
- `ctx.set(path, value)`: Writes a value to an arbitrary context path.

**`json` — JSON Operations:**

- `json.parse(str)`: Parse a JSON string into a value.
- `json.stringify(val)`: Serialize a value to a JSON string.
- `json.query(val, expr)`: Query a value using a JSONPath expression.

**`http` — Async HTTP Client (TurboNet):**

- `http.get(url [, options])`: Send a GET request.
- `http.post(url, body [, options])`: Send a POST request.
- `http.put(url, body [, options])`: Send a PUT request.
- `http.del(url [, options])`: Send a DELETE request.
- `http.patch(url, body [, options])`: Send a PATCH request.
- `http.head(url [, options])`: Send a HEAD request.
- `http.options(url [, options])`: Send an OPTIONS request.

Options object (all optional):
- `headers`: `{"Authorization": "Bearer xxx"}` — custom headers
- `timeout`: `5000` — timeout in milliseconds
- `follow_redirects`: `true` — follow HTTP redirects
- `basic_auth`: `{"user": "admin", "pass": "secret"}` — basic auth
- `bearer_token`: `"xxx"` — bearer token auth

Compatibility:
- `http.get(url)` and `http.post(url, body)` keep the legacy behavior and return only the response body string.
- `http.get(url, options)` and `http.post(url, body, options)` return `{status, body, headers, data, error}`.
- `body` must be a string; use `json.stringify(...)` when sending JSON.

**`fs` — File System (turbo_fs):**

- `fs.read(path)`: Read a file and return its contents as a string.
- `fs.write(path, data)`: Write data to a file (creates or overwrites).
- `fs.append(path, data)`: Append data to a file.
- `fs.exists(path)`: Returns `true` if the file exists.
- `fs.stat(path)`: Returns `{size: N, is_dir: bool}`.
- `fs.mkdir(path)`: Create a directory (and parents).
- `fs.remove(path)`: Delete a file.

**`shell` — Shell Command Execution:**

- `shell.exec(command [, input [, working_dir [, timeout_ms]]])`: Execute a shell command and return `{exit_code, stdout, stderr}`.

**IMPORTANT LIMITATIONS:**
- Shell commands executed via `shell.exec()` **do NOT** respect the task-level `timeout:` setting. The `timeout_ms` parameter must be explicitly provided in the script call.
- Commands executed via `shell.exec()` **do NOT** respect the task-level `silent:` setting. Output is not streamed to the console by default.
- For consistent behavior with task configuration, prefer using `command:` or `shell:` nodes over `shell.exec()` in scripts.

**`base64` — Base64 Encoding:**

- `base64.encode(str)`: Encode a string to base64.
- `base64.decode(str)`: Decode a base64 string.

**`log` — Logging:**

- `log.info(msg)`: Log at info level.
- `log.warn(msg)`: Log at warning level.
- `log.error(msg)`: Log at error level.
- `log.debug(msg)`: Log at debug level.

**Language Features:**

The script language supports the following constructs:

- **Variables:** `var name = value`
- **Control flow:** `if`/`else`, `for..in`, `while`, `break`, `continue`, `return`
- **Failure:** `fail("message")` — immediately fails the task with the given message.
- **Literals:** string, number, boolean (`true`/`false`), `null`, array (`[1, 2, 3]`), map (`map{key: "value"}`)
- **Operators:** arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), logical (`and`, `or`, `not`)
- **Member access:** dot notation (`obj.field`)
- **Function calls:** `module.function(args)`

**Example:**

```yaml
tasks:
  - name: fetch_config
    command: "curl -s https://api.example.com/config"
    output_format: json
    script: |
      var config = ctx.get("tasks.fetch_config.outputs.data")
      var version = config.version
      ctx.output("app_version", version)
      log.info("Fetched config version: " + version)

  - name: transform_data
    command: "curl -s https://api.example.com/users"
    output_format: json
    script: |
      var users = ctx.get("tasks.transform_data.outputs.data")
      var active = json.query(users, "$[@.status == \"active\"]")
      ctx.output("active_users", active)
      ctx.output("count", json.query(active, "length(@)"))

  - name: write_report
    depends_on: [transform_data]
    script: |
      var active = ctx.get("tasks.transform_data.outputs.active_users")
      var report = json.stringify(active)
      fs.write("./active_users.json", report)
      log.info("Wrote " + json.query(active, "length(@)") + " users")
      ctx.output("file_written", true)

  - name: notify
    depends_on: [write_report]
    script: |
      import("net");
      var count = ctx.get("tasks.transform_data.outputs.count")
      var body = json.stringify(map{status: "deployed", active_users: count})
      http.post("https://hooks.example.com/webhook", body)
```

Note: `write_report` and `notify` are **script-only tasks** — they have no `command:` action. The `script:` block IS the action.

### 7.3.1. LLM Templates (Prompt + Template Pattern)

Praktor provides built-in templates for calling AI API gateways. Each template wraps the provider-specific request format, auth, and response parsing — you just pass a prompt.

**Available Templates:**

| Template | Provider | Default Model |
| :--- | :--- | :--- |
| `llm-openai.yml` | OpenAI | `gpt-4o` |
| `llm-claude.yml` | Anthropic Claude | `claude-sonnet-4-20250514` |
| `llm-gemini.yml` | Google Gemini | `gemini-2.0-flash` |

**Usage:**

```yaml
tasks:
  - name: ask_ai
    uses: ./praktor/templates/llm-claude.yml
    vars:
      ANTHROPIC_API_KEY: "{{ env.ANTHROPIC_API_KEY }}"
      CLAUDE_SYSTEM_PROMPT: "You are a senior code reviewer."
      PROMPT: "Review this code:\n\n{{ tasks.read_code.outputs.stdout }}"
```

**Common Variables (all templates):**

| Variable | Description | Required |
| :--- | :--- | :--- |
| `PROMPT` | The user prompt to send | Yes |
| `*_API_KEY` | Provider API key (`OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `GEMINI_API_KEY`) | Yes |
| `*_MODEL` | Model name override | No |
| `*_MAX_TOKENS` | Max output tokens (default: `1024`) | No |
| `*_TEMPERATURE` | Temperature (default: `0.7`) | No |
| `*_SYSTEM_PROMPT` | System prompt (default: empty) | No |

**Outputs (all templates):**

| Output | Description |
| :--- | :--- |
| `answer` | The LLM's text response |
| `model` | The model that was used |
| `usage` | Token usage metadata |

Access via `tasks.<task_name>.outputs.answer`.

**Multi-Provider Comparison:**

```yaml
tasks:
  - name: review_openai
    uses: ./praktor/templates/llm-openai.yml
    vars:
      OPENAI_API_KEY: "{{ env.OPENAI_API_KEY }}"
      PROMPT: "{{ REVIEW_PROMPT }}"

  - name: review_claude
    uses: ./praktor/templates/llm-claude.yml
    vars:
      ANTHROPIC_API_KEY: "{{ env.ANTHROPIC_API_KEY }}"
      PROMPT: "{{ REVIEW_PROMPT }}"

  - name: compare
    depends_on: [review_openai, review_claude]
    script: |
      var a = ctx.get("tasks.review_openai.outputs.answer");
      var b = ctx.get("tasks.review_claude.outputs.answer");
      fs.write("./comparison.md", "# OpenAI\n" + a + "\n\n# Claude\n" + b);
```

**Batch Translation with `each`:**

```yaml
tasks:
  - name: translate
    each:
      items: ["Chinese", "Japanese", "Spanish"]
      as: LANG
    uses: ./praktor/templates/llm-claude.yml
    vars:
      ANTHROPIC_API_KEY: "{{ env.ANTHROPIC_API_KEY }}"
      CLAUDE_SYSTEM_PROMPT: "Translate accurately. Output ONLY the translation."
      PROMPT: "Translate to {{ LANG }}:\n\n{{ tasks.read_source.outputs.stdout }}"
```

See `examples/ai-chat.yml`, `examples/ai-code-review.yml`, `examples/ai-translation.yml` for complete workflows.

### 7.4. Runner: `dynamic_tasks` (Runtime Task Generation)

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
```

The parent `dynamic_tasks` task records an aggregate summary at `tasks.<name>.outputs.generated_tasks`, where each entry contains `index`, `item`, `name`, `status`, and `outputs` for the generated task. `tasks.<name>.outputs.generated_count` records how many generated tasks actually ran, and `success_count`, `failed_count`, `skipped_count` summarize the outcome distribution. Generated task names must be unique within the fan-out and must not collide with any existing workflow task. By default, fan-out stops at the first failed generated task.

**Use Cases:**

- Deploy to multiple environments discovered at runtime
- Process files found by a previous task
- Run tests for dynamically discovered modules
- Fan-out operations based on API responses

### 7.5. HTTP Requests via Script

Execute HTTP requests using TurboScript's built-in `http` module.

**Available Methods:**

- `http.get(url, options)` - GET request
- `http.post(url, body, options)` - POST request
- `http.put(url, body, options)` - PUT request
- `http.delete(url, options)` - DELETE request
- `http.patch(url, body, options)` - PATCH request

**Options:**

- `headers` (Map): Request headers
- `timeout` (Number): Request timeout in milliseconds
- `follow_redirects` (Boolean): Follow HTTP redirects

**Response Object:**

- `status` (Number): HTTP status code
- `body` (String): Response body as string
- `data` (Object): Parsed JSON response (if Content-Type is JSON)

**Example:**

```yaml
tasks:
  # Basic GET request
  - name: fetch_users
    script: |
      import("net");
      var resp = http.get("https://api.example.com/users", map{
        headers: map{
          Authorization: "Bearer " + ctx.get("secrets.api_token")
        }
      });
      if (resp.status != 200) {
        fail("Failed to fetch users: " + resp.status);
      }
      ctx.output("users", resp.data.users);

  # POST with authentication
  - name: create_user
    script: |
      import("net");
      var headers = json.parse(
        "{\"Authorization\":" + json.stringify("Bearer " + ctx.get("secrets.api_token")) +
        ",\"Content-Type\":\"application/json\"}"
      )
      var body = json.stringify(map{
        name: ctx.get("user_name"),
        email: ctx.get("user_email")
      })
      var resp = http.post("https://api.example.com/users",
        body,
        map{
          headers: headers
        }
      );
      if (resp.status != 201) {
        fail("Failed to create user: " + resp.status);
      }
      ctx.output("user_id", resp.data.id);

  # Chained requests with context
  - name: login
    script: |
      import("net");
      var body = json.stringify(map{
        username: ctx.get("env.API_USER"),
        password: ctx.get("env.API_PASS")
      });
      var resp = http.post("https://api.example.com/auth/login", body, map{});
      if (resp.status != 200 || !resp.data.token) {
        fail("Login failed");
      }
      ctx.output("token", resp.data.token);

  - name: get_protected_data
    depends_on: [login]
    script: |
      import("net");
      var token = ctx.get("tasks.login.outputs.token");
      var resp = http.get("https://api.example.com/protected/data", map{
        headers: map{
          Authorization: "Bearer " + token
        }
      });
      ctx.output("data", resp.data);
```

### 7.6. Runner: Orchestration Nodes (Declarative Control Flow)

Orchestration nodes provide declarative control flow for complex task execution. Instead of defining only linear task dependencies, you can express sophisticated control policies using composable tree structures directly within a task.

Praktor intentionally does not expose an HTTP action leaf node. Network I/O belongs in `script` via the built-in `http` module, or in `command`/`shell` via external tools such as `curl`.

**Key Concepts:**

- **Control Nodes**: Define execution flow (sequence, parallel, conditionals, loops)
- **Leaf Nodes**: Perform actions (shell commands, parsing, validation)
- **Context Bridge**: Access workflow data using `{ctx.*}` syntax
- **Simplified Syntax**: Clean YAML without unnecessary braces

**Supported Node Types:**

| Node Type | Category | Description |
| :--- | :--- | :--- |
| `sequence` | Control | Execute children in order until one fails |
| `parallel` | Control | Execute all children concurrently |
| `reactive_sequence` | Control | Sequence with continuous re-evaluation |
| `repeat` / `timeout` / `delay` | Decorator | Wrap exactly one child plus optional parameters |
| `if` | Control | Conditional branch with `if`, `then`, and optional `else` |
| `while` | Control | Loop while a condition is true |
| `switch` | Control | Select one case by numeric index |
| `shell` | Leaf | Execute shell command |
| `parse_json` | Leaf | Extract data from JSON using JSONPath |
| `parse_regex` | Leaf | Extract data using regular expressions |
| `check_exit_code` | Leaf | Validate command exit code |
| `wait_event` | Leaf | Wait for external event |

#### 7.6.1. Control Nodes

**Sequence:**

Executes children in order. Succeeds if all children succeed. Fails on first failure.

```yaml
tasks:
  - name: build
    sequence:
      - shell: cmake -B build
      - shell: cmake --build build
      - shell: ctest --test-dir build
```

**Parallel:**

Executes all children concurrently. Succeeds if all children succeed.

```yaml
tasks:
  - name: test
    parallel:
      - shell: pytest unit/
      - shell: pytest integration/
      - shell: npm run lint
```

**Reactive Sequence:**

Executes children in order with re-evaluation semantics.

```yaml
tasks:
  - name: reactive_check
    reactive_sequence:
      - file_exists: ./config.yml
      - shell: ./build-with-config.sh
```

#### 7.6.1.a. Decorators and Advanced Control Nodes

Action YAML uses one uniform rule: a node may contain scalar parameters and child branches in the same map.

Supported branch keys:

- `child`: one child node
- `children`: sequence of child nodes
- `then` / `else`: branch lists for `if`
- `do`: loop body for `while`
- `cases`: ordered branch list for `switch`

Some scalar leaf nodes also use flat sibling keys instead of nested parameter maps:

- `set_variable: target_key` with exactly one of sibling `value:` or `from:`
- `subtree: build_inner`

**Timeout:**

`timeout` fails the node if the child takes longer than `timeout_ms`. When the immediate child is `shell`, Praktor also pushes that limit into the shell runner so the process can be interrupted. For larger subtrees, timeout is detected when control returns from the child subtree.

```yaml
tasks:
  - name: guarded_build
    timeout: 5000
    child:
      shell: cmake --build build
```

**Repeat:**

Praktor's task runner is synchronous. Therefore `repeat` must declare a finite `num_cycles`; omitting it is rejected at runtime instead of hanging forever.

```yaml
tasks:
  - name: warm_cache
    repeat: 3
    child:
      shell: ./warm-cache.sh
```

**If:**

The `if` condition may be a scalar truthy/falsey value or a child node/sequence. Truthy strings are anything other than empty, `0`, `false`, `no`, `off`, or `null`.

```yaml
tasks:
  - name: verify_status
    if: "{ctx.tasks.fetch.outputs.api_status}"
    then:
      - shell: echo "Service is healthy"
    else:
      - shell: echo "Service is down"
```

**While:**

```yaml
tasks:
  - name: poll_until_ready
    while: "{ctx.variables.keep_polling}"
    do:
      - shell: ./poll.sh
      - sleep: "1s"
```

**Switch:**

`switch` must resolve to a zero-based numeric index into `cases`.
You may provide a numeric literal, a `{blackboard_key}` reference, or a bare blackboard key name.

```yaml
tasks:
  - name: route_case
    switch: selected_case
    cases:
      - shell: echo "primary"
      - shell: echo "secondary"
```

#### 7.6.2. Leaf Nodes

**Shell (Single Parameter - Simplified Syntax):**

```yaml
tasks:
  - name: hello
    shell: echo "Hello, World!"
```

**Shell (Multiple Parameters - Map Syntax):**

```yaml
tasks:
  - name: build
    shell:
      cmd: make all
      output_key: build_log
      timeout: 300
```

**SetVariable (Flat Syntax):**

```yaml
tasks:
  - name: state_update
    sequence:
      - set_variable: deployment_status
        value: ready
      - set_variable: deployed_version
        from: build_version
```

`set_variable` writes to the BT blackboard. Use `value` for a literal or substituted scalar, or `from` to copy another blackboard key.

**SubTree (Flat Syntax):**

```yaml
tasks:
  - name: run_nested_tree
    subtree: build_inner
```

**Parameters:**
- `cmd` (String, **Required**): Command to execute
- `output_key` (String, Optional): Blackboard key for stdout
- `stderr_key` (String, Optional): Blackboard key for stderr
- `exit_code_key` (String, Optional): Blackboard key for exit code
- `working_dir` (String, Optional): Working directory for the command
- `timeout` (Number, Optional): Timeout in milliseconds
- `stream_output` (Boolean, Optional): Stream stdout/stderr to the console while running

**Parse JSON:**

Extract data from JSON using JSONPath expressions.

```yaml
tasks:
  - name: extract_version
    sequence:
      - shell:
          cmd: curl -s https://api.example.com/config
          output_key: api_response
      - parse_json:
          input_key: api_response
          path: $.version
          output_key: version
```

**Parameters:**
- `input_key` (String, **Required**): Blackboard key containing JSON
- `path` (String, Optional): JSONPath expression. Defaults to `$` when omitted.
- `output_key` (String, Optional): Blackboard key for extracted data

**Parse Regex:**

Extract data using regular expressions.

```yaml
tasks:
  - name: extract_version
    sequence:
      - shell:
          cmd: git describe --tags
          output_key: version_raw
      - parse_regex:
          input_key: version_raw
          pattern: v([0-9]+\.[0-9]+\.[0-9]+)
          capture_group: 1
          output_key: version
```

**Parameters:**
- `input_key` (String, **Required**): Blackboard key containing text to parse
- `pattern` (String, **Required**): Regular expression pattern
- `capture_group` (Number, Optional): Which capture group to extract (default: 0)
- `output_key` (String, Optional): Blackboard key for extracted data

**IMPORTANT:** When `parse_regex` is used as a **top-level post-processor** for `command:` tasks (simplified syntax), `input_key` defaults to `stdout` and may be omitted. However, when used **inside orchestration nodes**, `input_key` is always required.

Example of simplified syntax (top-level post-processor):
```yaml
- name: extract_version
  command: "git describe --tags"
  parse_regex:
    pattern: "v([0-9]+\\.[0-9]+\\.[0-9]+)"
    output_key: "version"
    # input_key omitted: defaults to stdout
```

**Check Exit Code:**

Validate command exit codes.

```yaml
tasks:
  - name: verify_build
    sequence:
      - shell: make test
      - check_exit_code: 0
```

**Parameters:**
- `expected` (Number, **Required**): Expected exit code

#### 7.6.3. Context Bridge

The context bridge allows action orchestration nodes to access workflow data using `{ctx.*}` syntax.

**Syntax:**

- `{ctx.variables.NAME}` - Access global variable
- `{ctx.env.NAME}` - Access environment variable
- `{ctx.tasks.TASK.outputs.KEY}` - Access task output

**Example:**

```yaml
variables:
  VERSION: "1.0.0"
  ENVIRONMENT: "production"

tasks:
  - name: build
    sequence:
      - shell: echo "Building version {ctx.variables.VERSION}"
      - shell:
          cmd: make VERSION={ctx.variables.VERSION}
          output_key: artifact

  - name: deploy
    depends_on: [build]
    sequence:
      - shell: echo "Deploying {ctx.tasks.build.outputs.artifact}"
      - shell: kubectl set image deployment/app app={ctx.tasks.build.outputs.artifact}
      - shell: echo "Deployed to {ctx.variables.ENVIRONMENT}"
```

**Nested Access:**

```yaml
tasks:
  - name: process_api_data
    sequence:
      - shell:
          cmd: curl -s https://api.example.com/data
          output_key: api_response
      - parse_json:
          input_key: api_response
          path: $.data.items[0].id
          output_key: item_id
      - shell: echo "Processing item {ctx.tasks.process_api_data.outputs.item_id}"
```

#### 7.6.4. Nested Structures

Action orchestration can be nested to create complex control flow:

```yaml
tasks:
  - name: deploy_with_checks
    sequence:
      - shell: echo "Starting deployment"
      - shell: kubectl apply -f deployment.yml --context=prod
      - check_exit_code: 0
      - shell: echo "Deployment complete"
```

#### 7.6.5. Output Mapping

Action orchestration outputs are automatically mapped to task outputs in the workflow context.

**Reserved Output Keys:**

| Key | Description |
| :--- | :--- |
| `shell_exit_code` | Exit code of last shell command |
| `shell_stdout` | Standard output of last shell command |
| `shell_stderr` | Standard error of last shell command |
| `execution_time_ms` | Total execution time in milliseconds |
| `node_status` | Final status (SUCCESS/FAILURE/RUNNING) |

**Custom Output Keys:**

Use the `output_key` parameter to specify custom output names:

```yaml
tasks:
  - name: extract_data
    sequence:
      - shell:
          cmd: git describe --tags
          output_key: version_raw
      - parse_regex:
          input_key: version_raw
          pattern: v([0-9.]+)
          capture_group: 1
          output_key: version

  - name: use_data
    depends_on: [extract_data]
    command: echo "Version is {{ tasks.extract_data.outputs.version }}"
```

#### 7.6.6. Complete Example

```yaml
name: Build and Deploy Pipeline

variables:
  VERSION: "1.0.0"
  REGISTRY: "docker.io/myapp"

tasks:
  - name: build_and_test
    parallel:
      - sequence:
          - shell: echo "Building application"
          - shell:
              cmd: docker build -t {ctx.variables.REGISTRY}:{ctx.variables.VERSION} .
              output_key: build_output
          - check_exit_code: 0
      - sequence:
          - shell: echo "Running tests"
          - shell: pytest tests/
          - check_exit_code: 0

  - name: deploy
    depends_on: [build_and_test]
    sequence:
      - shell: echo "Deploying to production"
      - shell: kubectl set image deployment/app app={ctx.variables.REGISTRY}:{ctx.variables.VERSION}
      - check_exit_code: 0
      - shell: echo "Deployment successful"
```

**See Also:**
- Context bridge guide: `docs/CONTEXT_BRIDGE_GUIDE.md`
- Example workflows: `examples/orch_*.yml`

## 8. Event-Driven Triggers

A `triggers` block defines event-driven actions that execute after a task completes. Triggers simply reference other tasks by name, allowing any task type to be executed as a trigger action.

Tasks referenced only by triggers are treated as trigger handlers, not as regular DAG roots. They execute only when the trigger fires, unless explicitly invoked via single-task execution.

**Events:**

- `on_success`: Fires when the task completes successfully.
- `on_failure`: Fires when the task fails.
- `on_complete`: Fires regardless of success or failure.

**Trigger Actions:**

Triggers are specified as an array of task names (strings). When a trigger event fires, the referenced tasks are looked up and executed.

For tasks that use `each`, triggers fire once after the logical task completes, using the aggregate result of all iterations. They do not fire once per item or matrix combination.

**Example:**

```yaml
tasks:
  # Define notification tasks
  - name: notify_slack
    command: "curl -s -X POST {{ env.SLACK_WEBHOOK }}"
    script: |
      import("net");
      var version = ctx.get("tasks.deploy.outputs.stdout")
      var body = json.stringify(map{text: "Deployed " + version})
      http.post(ctx.get("env.SLACK_WEBHOOK"), body)

  - name: rollback
    command: "./rollback.sh --reason 'Deploy failed'"

  # Main task with triggers
  - name: deploy
    command: "./deploy.sh --env production"
    triggers:
      on_success:
        - notify_slack
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

### 8.2. Failure Context Variables

When a task fails, the following variables are available in triggered tasks to provide diagnostic information:

| Variable | Description |
| :--- | :--- |
| `failed_task_name` | Name of the failed task. |
| `failed_task_type` | Type of runner used (`command`, `program`, `uses`, `dynamic_tasks`). |
| `failed_task_exit_code` | Process exit code (available for `command` and `program` runners). |
| `failed_task_stdout` | The captured standard output of the failed task. |
| `failed_task_stderr` | The captured standard error of the failed task. |
| `failed_task_error` | The primary error message describing the failure. |
| `failed_inner_task_name` | For `uses` and `dynamic_tasks`, the first nested task that actually failed. |
| `failed_inner_task_type` | Runner type of that nested task. |
| `failed_inner_task_exit_code` | Nested task exit code when available. |
| `failed_inner_task_stdout` | Nested task stdout when available. |
| `failed_inner_task_stderr` | Nested task stderr when available. |
| `failed_inner_task_error` | Nested task error message when available. |

These variables can be accessed in triggered tasks using the standard `{{ variable_name }}` syntax or via `ctx.get()` in scripts.
Structured objects are also available at `failed_task`, `failed_task_outputs`, `failed_inner_task`, and `failed_inner_task_outputs`.

**Example:**

```yaml
tasks:
  - name: alert_on_failure
    command: "echo 'Task failed'"
    script: |
      import("net");
      var taskName = ctx.get("failed_task_name")
      var error = ctx.get("failed_task_error")
      var stderr = ctx.get("failed_task_stderr")
      var message = "Task '" + taskName + "' failed: " + error + "\n\nStderr:\n" + stderr
      http.post(ctx.get("env.ALERT_WEBHOOK"), json.stringify(map{text: message}))

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

**Scope:** Mustache templating with double braces `{{ }}` is used in:
- Praktor task-level fields (`command:`, `program:`, `args:`, `vars:`, etc.)
- Task-level `env:` and `vars:` values
- Script blocks

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
command: "echo '{{^tasks.test.outputs.data.results}}No tests were executed.{{/tasks.test.outputs.data.results}}'"
```

**Object Access:**
You can access nested fields using dot notation: `{{ tasks.fetch.outputs.data.user.id }}`.

**Action Layer Context Bridge:**

When using orchestration nodes (actions), a **separate** syntax is available for accessing workflow context from within action node parameters:

- **Single braces** `{ctx.*}` are used in action node parameters (e.g., `shell:`, `parse_regex:`, etc.)
- `{ctx.variables.VAR}` - Access global variable
- `{ctx.env.ENV_VAR}` - Access environment variable
- `{ctx.tasks.TASK_NAME.outputs.KEY}` - Access task output

**IMPORTANT DISTINCTION:**
- **Praktor task level** (e.g., `command:`, `vars:`) uses **double braces** `{{ }}` with full Mustache features (sections, loops, inverted sections).
- **Action node parameters** (e.g., inside `shell:`, `parse_regex:`) use **single braces** `{ctx.*}` for simple variable lookup only (no sections/loops).

**Example showing both syntaxes:**
```yaml
tasks:
  - name: extract_version
    command: "cat version.txt"  # Praktor command task
    
  - name: deploy
    vars:
      app_name: "MyApp"  # Task-level variable
      version: "{{ tasks.extract_version.outputs.stdout }}"  # Double braces: Mustache
    actions:
      sequence:
        - shell: "echo Deploying {ctx.variables.app_name} v{ctx.tasks.extract_version.outputs.version}"
          # Single braces: BT context bridge
        - shell: "kubectl apply -f deployment.yaml"
          working_dir: "{ctx.env.DEPLOY_DIR}"  # Single braces: access env
```

Mixing syntaxes in the wrong layer will cause errors:
- Using `{{ }}` in BT node parameters will be treated as literal text (Mustache runs at task parse time, BT nodes are evaluated at execution time).
- Using `{ctx.*}` in Praktor task-level fields will be treated as literal text.

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
```

### 11.2. Reusable Workflow with Context Inheritance

**File: `reusable/docker-build.yml`**

```yaml
# This reusable workflow expects IMAGE_NAME, TAG, and REGISTRY_URL
# to be provided via 'vars' by the calling task.

tasks:
  - name: set_full_tag
    command: "echo {{ REGISTRY_URL }}/{{ IMAGE_NAME }}:{{ TAG }}"
    script: |
      var stdout = ctx.get("tasks.set_full_tag.outputs.stdout")
      ctx.output("full_image_tag", stdout)

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
    command: "echo 'Notifying...'"
    script: |
      import("net");
      var image = ctx.get("tasks.build_image.outputs.full_image_tag")
      var body = json.stringify(map{text: "Deployment failed for " + image})
      http.post(ctx.get("env.SLACK_WEBHOOK_URL"), body)

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
    script: |
      var config = ctx.get("tasks.fetch_config.outputs.data")
      ctx.output("app_version", config.version)
      var enabled = json.query(config, "$.features[@.enabled == true].name")
      ctx.output("enabled_features", enabled)

  - name: deploy_with_features
    depends_on: [fetch_config]
    command: |
      ./deploy.sh \
        --version {{ tasks.fetch_config.outputs.app_version }} \
        --features {{ tasks.fetch_config.outputs.enabled_features }}
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
    command: "echo 'Saving log...'"
    script: |
      var output = ctx.get("tasks.deploy_app.outputs.stdout")
      log.info("Deploy output: " + output)

  - name: notify_team
    command: "echo 'Notifying...'"
    script: |
      import("net");
      var webhook = ctx.get("env.SLACK_WEBHOOK")
      var error = ctx.get("failed_task_error")
      if (not error) {
        error = "Success"
      }
      http.post(webhook, json.stringify(map{text: "Deploy status: " + error}))

  - name: rollback
    command: "./rollback.sh --reason 'Deploy failed'"

  - name: deploy_app
    command: "./deploy.sh --env production"
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
    timeout: "15m"

    # Exactly one runner:
    command: "echo hello"
    # OR
    program: "python"
    args: ["./tool.py", "--json"]
    stdin: "{{ payload }}"
    # OR
    uses: "./path/to/workflow.yml"
    # OR
    dynamic_tasks:
      items_variable: "{{ my_items }}"
      template:
        name: "subtask_{{ item }}"
        command: "echo {{ item }}"

    # Optional post-processing script (works with any runner)
    script: |
      var data = ctx.get("tasks.task_name.outputs.data")
      ctx.output("key", "value")

    # Optional triggers
    triggers:
      on_success: []
      on_failure: []
      on_complete: []
```

### Task Runner Summary

| Runner | Purpose | Output Location |
| :--- | :--- | :--- |
| `command` | Execute shell commands | `tasks.<name>.outputs.stdout` |
| `program` | Execute raw process with explicit args | `tasks.<name>.outputs.stdout` |
| `uses` | Execute reusable workflow | `tasks.<name>.outputs.*` (from nested tasks) |
| `dynamic_tasks` | Runtime task generation | `tasks.<name>.outputs.generated_tasks[*]` plus each generated task's own outputs |

### Script Built-in Modules

| Module | Functions |
| :--- | :--- |
| `ctx` | `get(path)`, `output(key, value)`, `set(path, value)` |
| `json` | `parse(str)`, `stringify(val)`, `query(val, jsonpath_expr)` |
| `http` | `get(url [,opts])`, `post(url, body [,opts])`, `put(url, body [,opts])`, `del(url [,opts])`, `patch(url, body [,opts])`, `head(url [,opts])`, `options(url [,opts])` |
| `fs` | `read(path)`, `write(path, data)`, `append(path, data)`, `exists(path)`, `stat(path)`, `mkdir(path)`, `remove(path)` |
| `base64` | `encode(str)`, `decode(str)` |
| `log` | `info(msg)`, `warn(msg)`, `error(msg)`, `debug(msg)` |
| `math` | `abs(x)`, `ceil(x)`, `floor(x)`, `round(x)`, `sqrt(x)`, `pow(x,y)`, `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan(x)`, `atan2(y,x)`, `log(x)`, `log10(x)`, `exp(x)`, `min(...)`, `max(...)`, `clamp(val,lo,hi)`, `random()`, `eval(expr [, vars])` |

---
