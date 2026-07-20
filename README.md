# Praktor - A Modern C++ YAML Workflow Engine

Praktor is a high-performance, concurrent workflow engine written in modern C++20. It allows you to define complex task dependencies and execution logic in a clean, simple YAML format. It's designed for orchestrating build pipelines, deployments, data processing jobs, and other multi-step automated processes.

## Core Features

- **🚀 High-Performance YAML Parsing**: Powered by `ryml` for 5-10x faster YAML parsing compared to traditional parsers
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

Praktor has two distinct orchestration levels:

1. **Workflow organization (DAG level)**:
   flow-level task properties such as `depends_on`, `when`, `each`, `retries`, `triggers`, and `script` organize how a task is scheduled and completed.
2. **Task execution (runner level)**:
   each task chooses one runner to do the actual work.

Common runners include:

- **`command`**: call an external process.
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
- **ryml (Rapid YAML)**: Ultra-fast YAML parsing library (5-10x faster than yaml-cpp)
- **jsoncons**: Powers JMESPath queries and advanced JSON context management
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

```bash
# Configure and build a developer package (Windows)
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user

# Install only the CLI and Pistol developer interface
cmake --install build/Msvc --prefix C:/opt/praktor-dev

# Run the configured tests separately when needed
ctest --preset win-dev-user
```

The install contains only the Praktor CLI and Praktor developer interface:
`praktor.exe`, `Praktor.dll` (plus its Windows import library), and
`praktor.h`.

Consumers can use the installed CMake package:

```cmake
find_package(Praktor CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Praktor::Praktor)
```

### 3. Run the workflow

```bash
# Run the workflow (from build directory)
bin/praktor -f my_workflow.yml
```

## CLI Usage

Praktor provides a powerful command-line interface with subcommands for different stages of your workflow lifecycle.

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

Praktor promotes modularity by allowing you to execute external workflow files as single tasks.

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

## Post-Processing Script Engine

Any task can include a `script:` block that runs after the task action completes. A task can also be script-only (no `command:` required). The script uses a custom language built with re2c (lexer) and lemon (parser).

### Built-in Modules

| Module | Functions | Purpose |
|--------|-----------|---------|
| `ctx` | `get(path)`, `output(key, val)`, `set(path, val)` | Read/write workflow context |
| `json` | `parse(str)`, `stringify(val)`, `query(val, jmespath)` | JSON operations via jsoncons |
| `http` | `get(url)`, `post(url, body)`, `put/del/patch/head/options` | Async HTTP client (TurboNet) |
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
      var active = json.query(data, "[?status=='active']");
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
      var summary = map{
        user_count: json.query(users, "length(@)"),
        order_total: json.query(orders, "sum([].amount)")
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

Praktor is designed with performance and modularity in mind:

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│   YAML Files    │───▶│   Task Parser    │───▶│   Enhanced Graph    │
│  (+ imports)    │    │   (ryml-based)   │    │    (DAG Builder)    │
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
- **Task Parser**: High-speed YAML engine using `ryml` for zero-allocation parsing
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
```bash
# Configure, build, test, and install the developer package
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user
ctest --preset win-dev-user
cmake --install build/Msvc --prefix C:/opt/praktor-dev
```

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

- **YAML Parsing**: 5-10x faster than yaml-cpp thanks to ryml
- **Memory Usage**: Minimal allocations with object pools and move semantics
- **Concurrency**: Deadlock-free parallel execution with custom thread pool
- **Scalability**: Tested with workflows containing 100+ tasks and deep dependency chains

## License & Status

Praktor is actively developed and production-ready. The core engine is feature-complete with:
- ✅ Full YAML workflow specification support
- ✅ Cross-file imports and modular design
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

**Join the Journey!** Praktor is more than a tool - it's a community building the future of workflow orchestration. Every contribution, big or small, makes a difference.
