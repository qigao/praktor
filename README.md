# TurboWeave — A YAML Workflow Runtime for Automation and AI Agents

TurboWeave is a high-performance C++20 workflow runtime for describing deterministic automation in YAML. A workflow is parsed into a dependency graph and executed with explicit inputs, conditions, retries, concurrency, reusable sub-workflows, scripts, system actions, and structured outputs.

TurboWeave can run as a standalone automation engine through the current `praktor` CLI, or it can be embedded through the stable C API and used as a workflow/tool runtime underneath an LLM agent harness such as TurboAgent.

## Naming Direction

This repository is moving toward one public product name: **TurboWeave**.

- **Repository target name:** `TurboWeave` instead of the generic `weave`.
- **`pistol/` target name:** `sdk/`. The directory is an embedding/API boundary, not a separate product, so it should use a descriptive name rather than another brand.
- **Compatibility names:** the current `praktor` CLI, `Praktor` shared library, CMake package, and `praktor.h` API remain the executable/API compatibility surface until a separate migration is performed.

The naming cleanup is intentionally separate from runtime semantics: existing YAML workflows and the current C ABI do not need to change just because the repository and source-directory names are clarified.

## What Can a YAML Workflow Do?

A TurboWeave YAML file is an executable orchestration contract. It can describe both the graph of work and the actions executed inside each task.

| Use case | What the workflow can express |
|---|---|
| **Build & test automation** | Configure, compile, run tests, collect outputs, gate later tasks on earlier results, and fan independent work out concurrently. |
| **CI/release pipelines** | Build artifacts, download verified packages, run validation stages, package releases, and trigger success/failure handling. |
| **Deployment & operations** | Run deployment commands, restart or inspect services, manage long-lived processes, retry transient operations, and perform rollback/notification flows. |
| **Data/API automation** | Call HTTP endpoints, parse JSON/YAML/XML/CSV data, transform values in scripts, and pass structured results between tasks. |
| **Reusable automation modules** | Package common flows as reusable YAML workflows with `uses`, inherited inputs, isolated task names, and namespaced outputs. |
| **Dynamic workflows** | Expand tasks from runtime data, loop with `each`, branch with `when`, and use task-internal sequence/fallback/parallel/retry/if/while/switch control flow. |
| **AI-assisted workflows** | Invoke OpenAI, Claude, or Gemini through reusable LLM templates and feed their structured outputs into deterministic downstream tasks. |
| **Agent tool execution** | Expose a bounded, pre-defined YAML workflow as one high-level tool to an agent harness instead of giving the model many low-level shell operations. |

A workflow can therefore replace a collection of ad-hoc shell scripts with one explicit, inspectable execution graph:

```text
inputs
  │
  ▼
YAML workflow
  │
  ├── depends_on / when / each / retries / triggers
  │
  ▼
DAG scheduler
  │
  ├── command
  ├── download
  ├── actions
  ├── service / managed_process
  ├── uses / dynamic_tasks
  └── script
  │
  ▼
structured task outputs
  │
  ▼
next tasks / caller / agent
```

### Example: build, test, and package

```yaml
variables:
  PRESET: linux-release

tasks:
  - name: configure
    command: "cmake --preset {{ PRESET }}"

  - name: build
    depends_on: [configure]
    command: "cmake --build --preset {{ PRESET }}"

  - name: test
    depends_on: [build]
    retries:
      count: 2
      delay: "2s"
    command: "ctest --preset {{ PRESET }} --output-on-failure"

  - name: package
    depends_on: [test]
    when: "{{ tasks.test.status }} == 'success'"
    command: "cmake --build --preset {{ PRESET }} --target package"
```

The important property is that **the YAML defines execution policy, not just a list of commands**. Dependencies, branching, retries, parallelism, outputs, error handling, and composition are part of the workflow contract.

## TurboAgent Integration

TurboWeave and TurboAgent have complementary responsibilities:

```text
OpenAI / Codex / other model
            │
            ▼
      TurboAgent Harness
            │
      policy / approval
      tool registry
      thread / turn
      memory / checkpoint
            │
            ▼
   TurboWeave workflow tool
            │
     stable C embedding API
            │
            ▼
        YAML workflow
            │
            ▼
 deterministic execution
```

TurboAgent decides **what capability to invoke**; TurboWeave defines **how that capability is executed**. The current embedding boundary in `pistol/api/praktor.h` already accepts a workflow path plus JSON inputs and returns canonical JSON containing workflow status and task results. This makes a registered TurboWeave workflow a natural high-level TurboAgent tool.

For agent-facing use, prefer a trusted workflow registry such as `build`, `test`, or `deploy` over allowing the model to provide arbitrary filesystem paths.


## Core Features

- **High-Performance YAML Parsing**: Powered by SaltsUtils Parser with owned documents, structured diagnostics, and YPATH queries
- **📝 Declarative YAML Syntax**: Define your workflows in a simple, human-readable YAML format
- **⚡ Concurrent Execution**: DAG-based executor runs independent tasks in parallel to maximize performance
- **📦 Reusable Workflows**: Compose complex pipelines using the `uses` keyword to execute external workflow files
- **🔧 Built-in Script Engine**: MIR/JIT-backed post-processing scripts with `ctx`, `json`, `http`, `fs`, `base64`, `math`, `log`, and `dll` modules (re2c + lemon parser)
- **🎯 Advanced Control Flow**:
    - `depends_on`: Define a Directed Acyclic Graph (DAG) of task dependencies
    - `when`: Use powerful conditional expressions (e.g., `"{{env}} == 'prod' and {{tag}} != 'latest'"`) to control task execution
    - `each`: Loop over lists or matrices and run tasks for each item
    - `retries`: Automatic retry with configurable delays and backoff
- **🔌 Integrated Runners**:
    - `command`: Native execution of external programs and shell scripts
    - `actions`: Explicit action orchestration for shell/script driven workflows
    - `dynamic_tasks`: Generate and execute tasks at runtime based on data from the context
- **📊 Output Capture**: Intelligent capture of stdout, stderr, and JSON data into the shared context
- **🛡️ Resilience & Triggers**: Event-driven actions (`on_success`, `on_failure`) for notifications and automated recovery

## Two-Level Orchestration Model

TurboWeave has two distinct orchestration levels:

1. **Workflow organization (DAG level)**:
   flow-level task properties such as `depends_on`, `when`, `each`, `retries`, `triggers`, and `script` organize how a task is scheduled and completed.
2. **Task execution (runner level)**:
   each task chooses one runner to do the actual work.

Common runners include:

- **`command`**: call an external process.
- **`download`**: stream an HTTPS response to a local file with optional SHA-256 verification.
- **`actions`**: define internal action control flow for one task.
- **`uses` / `dynamic_tasks`**: compose or generate more tasks at runtime.

Important semantic rule:

- `when`, `each`, `retries`, `triggers`, and `script` are flow-level task properties. They apply to all task kinds, including action tasks.
- `command` is just a runner for external process execution. It is not part of action grammar.
- YAML is the only authoring grammar for workflows and action tasks.
- Action node syntax is task-internal. Action nodes do not have their own DAG-level `when` or `each`.

Visual summary:

```text
Workflow (DAG)
  ├─ task properties: depends_on / when / each / retries / triggers / script
  └─ Task
      ├─ runner: command
      ├─ runner: actions
      ├─ runner: uses
      └─ runner: dynamic_tasks

Inside actions only:
  sequence / fallback / parallel / retry / if / while / switch / shell / parse_*
```

Minimal examples:

```yaml
tasks:
  - name: build_once
    when: "{{ ENV }} == 'prod'"
    command: "./build.sh"

  - name: deploy_many
    each:
      items: ["api", "worker"]
      as: service
    actions:
      sequence:
        - shell:
            cmd: "./deploy.sh {{ service }}"
        - parse_json:
            path: "$.status"
            output_key: "deploy_status"
```

Native package download:

```yaml
tasks:
  - name: fetch_package
    download:
      url: "{{ DOWNLOAD_URL }}"
      path: ./packages/release.zip
      sha256: "{{ SHA256 }}"
      overwrite: true
      timeout_ms: 300000
```

`download` accepts only absolute `https://` URLs. It streams to a temporary file and
atomically replaces `path` only after the response and optional checksum validation succeed.

In the example above:

- `when` belongs to the workflow layer and decides whether `build_once` runs.
- `each` belongs to the workflow layer and repeats the whole `deploy_many` task.
- `command` is an external-process runner, not an action node.
- `sequence`, `shell`, and `parse_json` belong to the action layer inside one task execution.
- Advanced action nodes that need both parameters and branches use YAML maps such as `{ if: ..., then: [...], else: [...] }`, `{ while: ..., do: [...] }`, `{ switch: ..., cases: [...] }`, `{ timeout: 5000, child: ... }`, or `{ set_variable: status, value: ready }`.
- `repeat` in Praktor must use a finite `num_cycles`; the synchronous runner intentionally rejects unbounded loops.
- `timeout` now fails overruns reliably; if it wraps an immediate `shell`, the time budget is also pushed into the shell runner for real process timeout.
- `switch` resolves to a zero-based case index and may be a numeric literal, a `{blackboard_key}` reference, or a bare blackboard key name.

## Technology Stack

- **C++20**: Modern C++ features for performance and safety
- **SaltsUtils Parser**: Shared YAML parsing, structured diagnostics, node traversal, and YPATH support
- **Turbo Parser**: Owns JSON/YAML/XML/CSV parsing and JSONPath/YPATH/XPath/CSVPath queries
- **re2c + lemon**: Lexer and parser generator for the built-in script engine
- **exprtk**: Expression evaluation for `when` conditions
- **vcpkg**: Modern C++ package management for easy dependency resolution

## Quick Start

### 1. Write your workflow file

Create a file named `my_workflow.yml`:

```yaml
variables:
  PROJECT: "Praktor-Demo"
  ENVIRONMENT: "staging"

tasks:
  - name: setup
    command: "echo Setting up {{PROJECT}} in {{ENVIRONMENT}}..."

  - name: fetch_config
    depends_on: [setup]
    command: "curl -s https://api.example.com/config"
    output_format: json

  - name: process_config
    depends_on: [fetch_config]
    command: "echo Processing config"
    script: |
      var cfg = ctx.get("tasks.fetch_config.outputs.data");
      ctx.output("api_endpoint", cfg.endpoint);
      ctx.output("is_secure", cfg.port == 443);

  - name: build
    depends_on: [process_config]
    command: "./build.sh --url {{ tasks.process_config.outputs.api_endpoint }}"
    when: "{{ tasks.process_config.outputs.is_secure }} == true"
```

### 2. Build the project

Praktor uses CMake with vcpkg for dependency management:

Windows builds must run in an x64 Visual Studio developer environment. Python 3
is required when tests are enabled: it runs the schema validation test and the
temporary HTTP server used by the script engine tests. The default workflow
also expects these installed SDK directories relative to the repository:

- `../external/pkgs/salts/bin`
- `../external/pkgs/salts-utils/bin`
- `../external/pkgs/turbo_script/bin`

With `praktor` installed or available on `PATH`, configure, build, and run all
tests with:

```powershell
praktor --file cmake_build.yml
```

The workflow runs this dependency chain:

```text
configure -> build -> test -> finish_report
```

The selected preset supplies runtime search paths directly from the installed
Salts, SaltsUtils, and TurboScript package roots. The workflow never copies
DLLs; a missing runtime dependency fails immediately.

For a Release build, override the preset:

```powershell
praktor --file cmake_build.yml `
  --input CMAKE_PRESET=win-release-user
```

`BUILD_TARGET` is a workflow variable and can be overridden with `--input`.
All runtime dependencies are supplied through the selected preset's `PATH`;
none are copied into the build or install directory.

To run the CMake stages directly, use the matching presets:

```powershell
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user
ctest --preset win-dev-user
```

Direct `ctest` uses the same preset-defined runtime paths as the workflow.

To install the developer package after a successful build:

```powershell
# Install only the CLI and Praktor developer interface
cmake --install build/Msvc --prefix C:/opt/praktor-dev
```

The install contains only the Praktor CLI and developer interface:
`praktor.exe`, `Praktor.dll` (plus its Windows import library), and
`praktor.h`.

Consumers can use the installed CMake package:

```cmake
find_package(Praktor CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Praktor::Praktor)
```

### 3. Run the workflow

```powershell
build/Msvc/bin/praktor.exe --file my_workflow.yml
```

## CLI Usage

The current `praktor` CLI provides subcommands for different stages of the workflow lifecycle.

| Command | Description | Example Usage |
|:---|:---|:---|
| (default) | Execute a workflow | `praktor -f workflow.yml --concurrent` |
| `init` | Scaffold a new workflow | `praktor init --template=cpp-library` |
| `validate` | Verify YAML and DAG sanity | `praktor validate -f workflow.yml` |
| `export` | Translate to other platforms | `praktor export -f workflow.yml -o ci.yml` |
| `visualize` | Generate DAG architecture | `praktor visualize -f workflow.yml` |

**Common Options:**
- `-f, --file <path>`: Path to the YAML workflow file.
- `-c, --concurrent`: Enable parallel task execution.
- `-j, --jobs <n>`: Set maximum concurrency (default: 4).
- `-v, --verbose`: Enable debug logging.
- `-i, --input <k=v>`: Pass input parameters to the workflow.
- `-t, --task <name>`: Run only a specific task and its dependencies.
- `--benchmark`: Show execution benchmark results (execution time and status).

## Reusable Workflows with `uses`

TurboWeave promotes modularity by allowing you to execute external workflow files as single tasks.

### Create a reusable module (`modules/docker-build.yml`):
```yaml
tasks:
  - name: build
    command: "docker build -t {{ IMAGE_NAME }}:{{ TAG }} ."
```

### Reference it in your main pipeline (`workflow.yml`):
```yaml
tasks:
  - name: get_version
    command: "git describe --tags"

  - name: push_image
    depends_on: [get_version]
    uses: ./modules/docker-build.yml
    vars:
      IMAGE_NAME: "myapp"
      TAG: "{{ tasks.get_version.outputs.stdout }}"
```

**Key Benefits:**
- ✅ **Encapsulation**: Reusable workflows have their own private task names
- ✅ **Context Inheritance**: Inherit variables and environment from the calling task
- ✅ **Namespaced Outputs**: Module results available via `{{ tasks.<task_name>.outputs.<key> }}`
- ✅ **Clean Pipelines**: Keep your main workflow high-level and readable

## System Actions

Praktor has two typed system runners. Both accept exactly four operations:
`status`, `start`, `stop`, and `restart`. Invalid YAML (including unknown keys,
unsupported operations, non-positive timeouts, or a `managed_process` start
without an executable) is rejected during parsing, before a process or operating
system boundary is accessed.

### Process-backed services

```yaml
tasks:
  - name: restart_camera
    service:
      operation: restart
      name: RetroCamera
      profile: windows_scm
      arguments: [--literal, "argument with spaces"]
      timeout_ms: 30000
      poll_interval_ms: 200
```

`service` is process-backed. The built-in `windows_scm` profile executes
`sc.exe` directly with an argument vector; it does not concatenate arguments
into a shell command. Service state is parsed from the numeric `STATE` value in
`sc.exe query` output: `1` is `stopped`, `2` is `start_pending`, `3` is
`stop_pending`, and `4` is `running`. Missing, malformed, or unsupported state
values fail explicitly. `timeout_ms` is the overall query/mutation/poll deadline,
and `poll_interval_ms` controls state-query polling.

Stable service outputs are `name` (string), `operation` (string), `state`
(string), `changed` (boolean), and `duration_ms` (integer milliseconds).

### Managed processes

```yaml
tasks:
  - name: start_saver
    managed_process:
      operation: start
      executable: RetroScreenSaver.exe
      arguments: [--fullscreen]
      working_directory: C:/Retro
      identity:
        image_name: RetroScreenSaver.exe
      startup_timeout_ms: 5000
      stop_timeout_ms: 5000
      force_terminate: true
```

`managed_process` is for a long-lived process whose lifecycle continues after
the task returns. This differs from `program`, which starts one raw process,
waits for it to exit, and captures its output. The current managed-process
backend is Windows-only and matches the exact image name in the current session;
it also revalidates the process instance before stop or termination.

`startup_timeout_ms` bounds the wait for a started process to become running.
`stop_timeout_ms` first bounds graceful stop. When `force_terminate` is `true`,
a graceful-stop timeout permits an identity-revalidated termination followed by
one more bounded wait; when it is `false`, that timeout is returned as a failure.

Stable managed-process outputs are `image_name` (string), `operation` (string),
`state` (`not_running` or `running`), `pid` (integer; `0` when not running),
`changed` (boolean), and `duration_ms` (integer milliseconds).

For both runners, `changed=false` means the requested state already held (or the
operation was `status`); `changed=true` means Praktor issued a state-changing
request. It does not claim exactly-once delivery. Failures retain the stable
outputs observed so far and expose `error_code`, `error_phase`, and
`error_details` to failure triggers as `failed_task_error_code`,
`failed_task_error_phase`, and `failed_task_error_details`.

When task caching is enabled through `sources` and `generates`, every system
action parameter participates in the action hash. Changing an operation, name or
identity, profile/executable/path, any argv entry, timeout, polling value, or
`force_terminate` invalidates the cached task.

## Post-Processing Script Engine

Any task can include a `script:` block that runs after the task action completes. A task can also be script-only (no `command:` required). The script uses a custom language built with re2c (lexer) and lemon (parser).

### Built-in Modules

| Module | Functions | Purpose |
|--------|-----------|---------|
| `ctx` | `get(path)`, `output(key, val)`, `set(path, val)` | Read/write workflow context |
| `json` | `parse(str)`, `stringify(val)`, `query(val, jsonpath)` | JSON operations via Turbo Parser |
| `http` | `get(url)`, `post(url, body)`, `put/del/patch/head/options` | Async HTTP client (Salts::CHTTP) |
| `fs` | `read(path)`, `write(path, data)`, `append(path, data)`, `exists/stat/mkdir/remove` | File system via turbo_fs |
| `base64` | `encode(str)`, `decode(str)` | Base64 encoding/decoding |
| `math` | `abs`, `ceil`, `floor`, `round`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`, `min`, `max`, `clamp`, `random`, `eval(expr [, vars])` | Math functions + exprtk expressions |
| `log` | `info(msg)`, `warn(msg)`, `error(msg)`, `debug(msg)` | Logging |
| `dll` | `call(module, function, ...args)` | Call native DLL/SO modules |

### Example

```yaml
tasks:
  - name: fetch_data
    command: "curl -s https://api.example.com/data"
    output_format: json
    script: |
      var data = ctx.get("tasks.fetch_data.outputs.data");
      var active = json.query(data, "$[@.status == \"active\"]");
      ctx.output("active_count", json.query(active, "length(@)"));
      for (item in active) {
        log.info("Active: " + item.name);
      }
```

### Script-Only Task

Tasks can use `script:` as the sole action — no `command:` required:

```yaml
tasks:
  - name: aggregate_data
    depends_on: [fetch_users, fetch_orders]
    script: |
      var users = ctx.get("tasks.fetch_users.outputs.data");
      var orders = ctx.get("tasks.fetch_orders.outputs.data");
      var order_total = 0;
      for (order in orders) {
        order_total = order_total + order.amount;
      }
      var summary = map{
        user_count: json.query(users, "length(@)"),
        order_total: order_total
      };
      fs.write("./report.json", json.stringify(summary));
      ctx.output("summary", summary);
```

### LLM Templates (Prompt + Template)

Built-in templates for calling AI APIs — just pass a prompt:

```yaml
# Call Claude with a prompt
- name: ask_claude
  uses: ./praktor/templates/llm-claude.yml
  vars:
    ANTHROPIC_API_KEY: "{{ env.ANTHROPIC_API_KEY }}"
    CLAUDE_SYSTEM_PROMPT: "You are a helpful assistant."
    PROMPT: "Explain quicksort in 3 sentences"

# Call OpenAI
- name: ask_openai
  uses: ./praktor/templates/llm-openai.yml
  vars:
    OPENAI_API_KEY: "{{ env.OPENAI_API_KEY }}"
    PROMPT: "Write a haiku about C++"

# Call Gemini
- name: ask_gemini
  uses: ./praktor/templates/llm-gemini.yml
  vars:
    GEMINI_API_KEY: "{{ env.GEMINI_API_KEY }}"
    PROMPT: "What is the meaning of life?"
```

All templates output `answer`, `model`, and `usage` — access via `tasks.<name>.outputs.answer`.

Available templates: `llm-openai.yml`, `llm-claude.yml`, `llm-gemini.yml`

See `examples/ai-chat.yml`, `examples/ai-code-review.yml`, `examples/ai-translation.yml` for full workflows.

## Architecture Overview

TurboWeave is designed with performance and modularity in mind:

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│   YAML Files    │───▶│   Task Parser    │───▶│   Enhanced Graph    │
│  (+ includes)   │    │ (Turbo Parser)   │    │    (DAG Builder)    │
└─────────────────┘    └──────────────────┘    └─────────────────────┘
                                                          │
                                                          ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│ Task Execution  │◀───│  Thread Pool     │◀───│  Workflow Executor  │
│   (Commands)    │    │  (Concurrent)    │    │   (Orchestrator)    │
└─────────────────┘    └──────────────────┘    └─────────────────────┘
         │
         ▼
┌─────────────────┐
│  Script Engine  │
│ MIR/JIT backend │
└─────────────────┘
```

**Core Components:**
- **Task Parser**: Turbo Parser-backed YAML adapter with explicit ownership and source diagnostics
- **Enhanced Graph**: Advanced DAG orchestration with cycle detection and parallel scheduling
- **Workflow Executor**: Concurrent runtime that manages thread pools and execution context
- **Script Engine**: Built-in scripting language (re2c lexer + lemon parser + MIR/JIT backend)
- **Expression Engine**: Powerful interpolation engine supporting variables, environment, and task outputs

## Advanced Features

### Task Variables and Scoping
```yaml
defaults:
  vars:
    GLOBAL_VAR: "available everywhere"

tasks:
  - name: scoped_task
    vars:
      LOCAL_VAR: "only in this task"
    command: "echo {{GLOBAL_VAR}} and {{LOCAL_VAR}}"
```

### Conditional Execution
```yaml
tasks:
  - name: prod_only_task
    command: "echo Deploying to production"
    when: "{{environment}} == 'prod' and {{approval}} == true"
```

### Loop Execution
```yaml
tasks:
  - name: test_multiple_services
    each:
      items: ["auth", "api", "worker"]
      as: "service"
    command: "echo Testing service: {{service}}"
```

### Error Handling & Triggers
```yaml
tasks:
  - name: notify_failure
    script: |
      import("net");
      var error = ctx.get("failed_task_error");
      http.post("{{ env.SLACK_WEBHOOK }}", json.stringify(map{text: "Deployment failed: " + error}));

  - name: rollback
    command: "./rollback.sh"

  - name: deploy
    command: "./deploy.sh"
    retries:
      count: 3
      delay: "30s"
    triggers:
      on_failure: [rollback, notify_failure]
      on_success: [notify_success]
```

### Output Chaining
```yaml
tasks:
  - name: get_version
    command: "git describe --tags"

  - name: tag_image
    depends_on: [get_version]
    command: "docker tag myapp:latest myapp:{{ tasks.get_version.outputs.stdout }}"
```

## Documentation

Complete documentation is available in the [docs/](./docs/README.md) directory:

- 🏗️ **[Architecture Overview](./docs/ARCHITECTURE.md)**: Deep dive into the system design
- 📁 **[Project Structure](./docs/PROJECT_STRUCTURE.md)**: Guide to folders and codebase organization
- ⚡ **[Trigger System](./grammar.md#8-event-driven-triggers)**: Detailed guide on event-driven actions
- 📖 **[DSL Specification](./grammar.md)**: Full reference for the Praktor YAML grammar

## Contributing & Development

### Building from Source

Use the [Quick Start build workflow](#2-build-the-project) for the complete
Windows configure, build, and test sequence. Keep Debug and Release presets
separate so their artifacts and runtime roots are never mixed.

### Project Structure
```
praktor/
├── praktor/           # Core library and CLI
│   ├── include/       # Public headers
│   ├── src/           # Implementation
│   │   └── script/    # Script engine (re2c + lemon)
│   └── test/          # Unit tests (Catch2)
├── docs/              # System & feature documentation
├── examples/          # Sample workflow files
└── grammar.md         # Authoritative DSL specification
```

## Performance Characteristics

- **YAML Parsing**: Shared SaltsUtils Parser implementation across workflows and structured queries
- **Memory Usage**: Minimal allocations with object pools and move semantics
- **Concurrency**: Deadlock-free parallel execution with custom thread pool
- **Scalability**: Tested with workflows containing 100+ tasks and deep dependency chains

## License & Status

TurboWeave is actively developed. The current core engine provides:
- ✅ Full YAML workflow specification support
- ✅ Cross-file includes and modular design
- ✅ Thread-safe concurrent execution
- ✅ Built-in script engine with JSON, HTTP, file system, base64, math (exprtk), and logging modules
- ✅ Script-only tasks (no command action required)
- ✅ Comprehensive error handling and logging
- ✅ Windows/Linux/macOS compatibility

## 🚀 What's Next - Community Roadmap

**Help us prioritize!** Vote on features you want most by starring ⭐ issues or contributing PRs.

### 🛠️ Developer Experience
- [x] **Advanced CLI Tools**
  - [x] `praktor init --template=cpp-library` - Project scaffolding
  - [x] `praktor validate -f workflow.yml` - Workflow validation
  - [x] `praktor export -f workflow.yml` - Platform integration
  - [x] `praktor -f workflow.yml --benchmark` - Performance profiling
  - [x] `praktor visualize -f workflow.yml` - DAG visualization

### 🎨 IDE Integration
- [ ] **VS Code Extension**
  - [ ] Syntax highlighting for `.yml` workflow files
  - [ ] Auto-completion for task types and properties
  - [ ] Import resolution and validation
  - [ ] Interactive DAG visualization
  - [ ] Debug execution with breakpoints
- [ ] **IntelliJ/CLion Plugin**
- [ ] **Vim/Neovim Language Server**

### 🏢 Enterprise Features
- [ ] **Security & Compliance**
  - [ ] Secrets manager integration (Azure KeyVault, AWS Secrets Manager)
  - [ ] Policy enforcement and governance
  - [ ] Audit logging and compliance reporting
- [ ] **Advanced Authentication**
  - [ ] LDAP/Active Directory integration
  - [ ] OAuth2/OIDC support
  - [ ] Role-based access control (RBAC)

### ☁️ Cloud-Native & Scaling
- [ ] **Kubernetes Integration**
  - [ ] Native K8s operator for workflow execution
  - [ ] Distributed task execution across pods
  - [ ] Auto-scaling based on workload
- [ ] **Multi-Cloud Support**
  - [ ] AWS ECS/Fargate execution
  - [ ] Azure Container Instances
  - [ ] Google Cloud Run integration

### ⚡ Performance & Optimization
- [ ] **Advanced Execution**
  - [ ] Result caching between workflow runs
  - [ ] Task pipelining and streaming
  - [ ] Incremental execution (only run changed tasks)
- [ ] **Resource Management**
  - [ ] Dynamic load balancing
  - [ ] Intelligent task scheduling

### 📊 Observability & Analytics
- [ ] **Monitoring Integration**
  - [ ] Prometheus metrics export
  - [ ] Grafana dashboard templates
  - [ ] Jaeger distributed tracing
  - [ ] Custom webhook notifications

### 🎯 Platform Integrations
- [ ] **CI/CD Platforms**
  - [ ] GitHub Actions generator
  - [ ] GitLab CI export
  - [ ] Jenkins pipeline conversion
  - [ ] Azure DevOps integration

## 🤝 How to Contribute to the Roadmap

### Vote on Features
1. **👍 React to issues** with 👍 for features you want
2. **💬 Comment** with your use cases and requirements
3. **⭐ Star the repository** to show general support

### Contribute Code
1. **Pick a checkbox** that interests you
2. **Open an issue** to discuss implementation approach
3. **Submit a PR** with your implementation
4. **Get recognition** in our contributors hall of fame

---

**TurboWeave** aims to keep workflow authoring declarative, execution deterministic, and embedding simple enough for both conventional automation and agent-driven systems.
