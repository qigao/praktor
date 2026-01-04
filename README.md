# Prakter - A Modern C++ YAML Workflow Engine

Prakter is a high-performance, concurrent workflow engine written in modern C++20. It allows you to define complex task dependencies and execution logic in a clean, simple YAML format. It's designed for orchestrating build pipelines, deployments, data processing jobs, and other multi-step automated processes.

## Core Features

- **🚀 High-Performance YAML Parsing**: Powered by `ryml` for 5-10x faster YAML parsing compared to traditional parsers
- **📝 Declarative YAML Syntax**: Define your workflows in a simple, human-readable YAML format
- **⚡ Concurrent Execution**: DAG-based executor runs independent tasks in parallel to maximize performance
- **📦 Reusable Workflows**: Compose complex pipelines using the `uses` keyword to execute external workflow files
- **🎯 Advanced Control Flow**:
    - `depends_on`: Define a Directed Acyclic Graph (DAG) of task dependencies
    - `when`: Use powerful conditional expressions (e.g., `"{{env}} == 'prod' and {{tag}} != 'latest'"`) to control task execution
    - `each`: Loop over lists or matrices and run tasks for each item
    - `retries`: Automatic retry with configurable delays and backoff
- **🔧 Integrated Runners**:
    - `command`: Native execution of external programs and shell scripts
    - `script`: In-memory data transformation using a high-performance JavaScript engine (QuickJS-ng)
    - `dynamic_tasks`: Generate and execute tasks at runtime based on data from the context
- **📊 Output Capture**: Intelligent capture of stdout, stderr, and JSON data into the shared context
- **🛡️ Resilience & Triggers**: Event-driven actions (`on_success`, `on_failure`) for notifications and automated recovery

## Technology Stack

- **C++20**: Modern C++ features for performance and safety
- **ryml (Rapid YAML)**: Ultra-fast YAML parsing library (5-10x faster than yaml-cpp)
- **jsoncons**: Powers JMESPath queries and advanced JSON context management
- **QuickJS-ng**: High-performance, embedded JavaScript engine for scriptable tasks
- **vcpkg**: Modern C++ package management for easy dependency resolution

## Quick Start

### 1. Write your workflow file

Create a file named `my_workflow.yml`:

```yaml
variables:
  PROJECT: "Prakter-Demo"
  ENVIRONMENT: "staging"

tasks:
  - name: setup
    command: "echo Setting up {{PROJECT}} in {{ENVIRONMENT}}..."

  - name: fetch_config
    depends_on: [setup]
    command: "curl -s https://api.example.com/config"
    output_format: json
    # Result stored in tasks.fetch_config.outputs.data

  - name: process_config
    depends_on: [fetch_config]
    script:
      source: |
        const cfg = context.get("tasks.fetch_config.outputs.data");
        context.set("api_endpoint", cfg.endpoint);
        context.set("is_secure", cfg.port === 443);

  - name: build
    depends_on: [process_config]
    command: "./build.sh --url {{ tasks.process_config.outputs.api_endpoint }}"
    when: "{{ tasks.process_config.outputs.is_secure }} == true"
```

### 2. Build the project

Prakter uses CMake with vcpkg for dependency management:

```bash
# Configure the project (vcpkg will install dependencies)
cmake --preset=default

# Build the executable
cmake --build build/Ninja/Msvc
```

### 3. Run the workflow

```bash
# Run the workflow (from build directory)
bin/prakter run my_workflow.yml
```

## CLI Usage

Prakter provides a powerful command-line interface with subcommands for different stages of your workflow lifecycle.

| Command | Description | Example Usage |
|:---|:---|:---|
| `run` | Execute a workflow (default) | `prakter run workflow.yml --concurrent` |
| `init` | Scaffold a new workflow | `prakter init --template=cpp-library` |
| `validate` | Verify YAML and DAG sanity | `prakter validate workflow.yml` |
| `export` | Translate to other platforms | `prakter export workflow.yml --output=ci.yml` |
| `benchmark` | Profile execution time | `prakter benchmark workflow.yml` |
| `visualize` | Generate DAG architecture | `prakter visualize workflow.yml` |

**Common Options:**
- `-f, --file <path>`: Path to the YAML workflow file.
- `-c, --concurrent`: Enable parallel task execution.
- `-j, --jobs <n>`: Set maximum concurrency (default: 4).
- `-v, --verbose`: Enable debug logging.
- `-i, --input <k=v>`: Pass input parameters to the workflow.
- `-t, --task <name>`: Run only a specific task and its dependencies.

## Reusable Workflows with `uses`

Prakter promotes modularity by allowing you to execute external workflow files as single tasks.

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

## Architecture Overview

Prakter is designed with performance and modularity in mind:

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
```

**Core Components:**
- **Task Parser**: High-speed YAML engine using `ryml` for zero-allocation parsing
- **Enhanced Graph**: Advanced DAG orchestration with cycle detection and parallel scheduling
- **Workflow Executor**: Concurrent runtime that manages thread pools and execution context
- **Script Runner**: Embedded QuickJS runtime for complex data manipulation without external tools
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
      LOCAL_VAR: "only in this task"  # Overrides global if same name
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
  - name: deploy
    command: "./deploy.sh"
    retries:
      count: 3
      delay: "30s"
    triggers:
      on_failure:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: '{"text": "Deployment failed: {{ failed_task_error }}"}'
        - run_task:
            task_name: rollback
      on_success:
        - "@prakter Deployment of {{ VERSION }} succeeded!"
```

### Output Chaining
```yaml
tasks:
  - name: get_version
    command: "git describe --tags"
    outputs:
      stdout_to_variable: "version"

  - name: tag_image
    depends_on: [get_version]
    command: "docker tag myapp:latest myapp:{{version}}"
```

## Documentation

Complete documentation is available in the [docs/](./docs/README.md) directory:

- 🏗️ **[Architecture Overview](./docs/ARCHITECTURE.md)**: Deep dive into the system design
- 📁 **[Project Structure](./docs/PROJECT_STRUCTURE.md)**: Guide to folders and codebase organization
- ⚡ **[Trigger System](./docs/TRIGGERS.md)**: Detailed guide on event-driven actions
- 📖 **[DSL Specification](./grammar.md)**: Full reference for the Prakter YAML grammar

## Contributing & Development

### Building from Source
```bash
# Install vcpkg dependencies
vcpkg install

# Configure with presets
cmake --preset=default

# Build and test
cmake --build build/Ninja/Msvc
ctest --preset=default
```

### Project Structure
```
prakter/
├── prakter/           # Core library and CLI
│   ├── include/       # Public headers
│   ├── src/           # Implementation
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

Prakter is actively developed and production-ready. The core engine is feature-complete with:
- ✅ Full YAML workflow specification support
- ✅ Cross-file imports and modular design
- ✅ Thread-safe concurrent execution
- ✅ Comprehensive error handling and logging
- ✅ Windows/Linux/macOS compatibility

## 🚀 What's Next - Community Roadmap

**Help us prioritize!** Vote on features you want most by starring ⭐ issues or contributing PRs.

### 🛠️ Developer Experience
- [x] **Advanced CLI Tools**
  - [x] `prakter init --template=cpp-library` - Project scaffolding
  - [x] `prakter validate workflow.yml` - Workflow validation
  - [x] `prakter export --github-actions` - Platform integration
  - [x] `prakter benchmark workflow.yml` - Performance profiling
  - [x] `prakter visualize workflow.yml` - DAG visualization

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
  - [ ] SOX, GDPR, HIPAA compliance workflows
- [ ] **Advanced Authentication**
  - [ ] LDAP/Active Directory integration
  - [ ] OAuth2/OIDC support
  - [ ] Role-based access control (RBAC)

### ☁️ Cloud-Native & Scaling
- [ ] **Kubernetes Integration**
  - [ ] Native K8s operator for workflow execution
  - [ ] Distributed task execution across pods
  - [ ] Auto-scaling based on workload
  - [ ] Resource quotas and limits
- [ ] **Multi-Cloud Support**
  - [ ] AWS ECS/Fargate execution
  - [ ] Azure Container Instances
  - [ ] Google Cloud Run integration

### ⚡ Performance & Optimization
- [ ] **Advanced Execution**
  - [ ] Result caching between workflow runs
  - [ ] Task pipelining and streaming
  - [ ] GPU-accelerated tasks
  - [ ] Incremental execution (only run changed tasks)
- [ ] **Resource Management**
  - [ ] Dynamic load balancing
  - [ ] Intelligent task scheduling
  - [ ] Memory and CPU optimization

### 📊 Observability & Analytics
- [ ] **Monitoring Integration**
  - [ ] Prometheus metrics export
  - [ ] Grafana dashboard templates
  - [ ] Jaeger distributed tracing
  - [ ] Custom webhook notifications
- [ ] **Workflow Analytics**
  - [ ] Execution time analysis
  - [ ] Resource usage profiling
  - [ ] Failure pattern detection
  - [ ] Performance regression tracking

### 📚 Extended Collections Library
- [ ] **Web Development**
  - [ ] React/Vue/Angular build pipelines
  - [ ] Node.js deployment workflows
  - [ ] Static site generation (Gatsby, Next.js)
- [ ] **Machine Learning**
  - [ ] Model training pipelines
  - [ ] MLOps workflows (MLflow, Kubeflow)
  - [ ] Data preprocessing and feature engineering
- [ ] **Data Engineering**
  - [ ] Apache Spark job orchestration
  - [ ] ETL pipeline templates
  - [ ] Data warehouse integration
- [ ] **Mobile Development**
  - [ ] React Native build and deploy
  - [ ] Flutter cross-platform workflows
  - [ ] iOS/Android native builds
- [ ] **Game Development**
  - [ ] Unity build and asset pipelines
  - [ ] Unreal Engine workflows
  - [ ] Multi-platform game deployment
- [ ] **Security & DevSecOps**
  - [ ] Automated vulnerability scanning
  - [ ] Penetration testing workflows
  - [ ] Security compliance automation

### 🌍 Community & Ecosystem
- [ ] **Prakter Hub - Community Platform**
  - [ ] Collection marketplace with ratings/reviews
  - [ ] Easy sharing and contribution workflows
  - [ ] Semantic versioning for collections
  - [ ] Enterprise collection licensing
- [ ] **Developer Community**
  - [ ] Community Discord/Slack
  - [ ] Monthly community calls
  - [ ] Contribution recognition program
  - [ ] Prakter certification program

### 🔧 Runtime Enhancements
- [ ] **Dynamic Workflows**
  - [ ] Runtime workflow modification API
  - [ ] Conditional workflow generation
  - [ ] Template parameterization system
- [ ] **Advanced Control Flow**
  - [ ] Workflow loops and iterations
  - [ ] Dynamic task generation
  - [ ] Event-driven task execution
  - [ ] Workflow composition and nesting

### 🎯 Platform Integrations
- [ ] **CI/CD Platforms**
  - [ ] GitHub Actions generator
  - [ ] GitLab CI export
  - [ ] Jenkins pipeline conversion
  - [ ] Azure DevOps integration
- [ ] **Development Tools**
  - [ ] Docker Compose integration
  - [ ] Helm chart deployment
  - [ ] Terraform workspace management

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

### Suggest New Features
1. **Open an issue** with the "enhancement" label
2. **Describe the problem** you're trying to solve
3. **Propose a solution** with examples
4. **Engage with the community** for feedback

### Priority Levels
- 🟢 **High Priority**: Core functionality, high community demand
- 🟡 **Medium Priority**: Nice-to-have features, moderate demand
- 🟠 **Low Priority**: Experimental features, niche use cases

**Current Focus Areas** (actively being developed):
- 🟢 Enhanced collections library (web, mobile, ML)
- 🟢 VS Code extension for improved developer experience
- 🟢 Kubernetes operator for cloud-native execution

---

**Join the Journey!** Prakter is more than a tool - it's a community building the future of workflow orchestration. Every contribution, big or small, makes a difference.

**Get Started Contributing**: Check our [Contributing Guide](CONTRIBUTING.md) and pick your first issue!
