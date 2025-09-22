# Weave - A Modern C++ YAML Workflow Engine

Weave is a high-performance, concurrent workflow engine written in modern C++20. It allows you to define complex task dependencies and execution logic in a clean, simple YAML format. It's designed for orchestrating build pipelines, deployments, data processing jobs, and other multi-step automated processes.

## Core Features

- **🚀 High-Performance YAML Parsing**: Powered by `ryml` for 5-10x faster YAML parsing compared to traditional parsers
- **📝 Declarative YAML Syntax**: Define your workflows in a simple, human-readable YAML format
- **⚡ Concurrent Execution**: Thread-pool-based executor runs independent tasks in parallel to maximize performance
- **📦 Cross-File Task Imports**: Modular workflow design with `imports` for reusable task libraries
- **🎯 Advanced Control Flow**:
    - `depends_on`: Define a Directed Acyclic Graph (DAG) of task dependencies
    - `when`: Use powerful conditional expressions (e.g., `"{{env}} == 'prod' and {{tag}} != 'latest'"`) to control task execution
    - `each`: Loop over lists and run tasks for each item
    - `retries`: Automatic retry with configurable delays
- **🏗️ Structural Tasks**:
    - `group`: Sequential task execution with logical grouping
    - `parallel`: Concurrent task blocks with deadlock-safe execution
- **🔧 Built-in Task Types**: Native support for `run_command`, `create_directory`, `copy_file`, and `move_file`
- **📊 Output Capture**: Capture stdout, stderr, and exit codes into variables for task chaining
- **🛡️ Enhanced Error Handling**: Intelligent `on_failure` mechanism with automatic failure context propagation

## Technology Stack

- **C++20**: Modern C++ features for performance and safety
- **ryml (Rapid YAML)**: Ultra-fast YAML parsing library (5-10x faster than yaml-cpp)
- **PEGTL**: Powers the sophisticated `when` clause expression parser
- **Thread Pool**: Custom thread pool implementation for optimal concurrent execution
- **CMake + vcpkg**: Modern C++ package management and build system

## Quick Start

### 1. Write your workflow file

Create a file named `my_workflow.yml`:

```yaml
defaults:
  retries:
    count: 1
    delay: "1s"
  vars:
    GREETING: "Hello from Weave!"

variables:
  PROJECT: "MyApp"

tasks:
  - name: setup
    type: run_command
    command: "echo Setting up {{PROJECT}}: {{GREETING}}"
    outputs:
      stdout_to_variable: "setup_result"

  - name: create_build_dir
    type: create_directory
    path: "./build_output"
    parents: true
    depends_on: [setup]

  - name: build_parallel
    type: parallel
    depends_on: [create_build_dir]
    tasks:
      - name: frontend_build
        type: run_command
        command: "echo Building frontend..."
      - name: backend_build
        type: run_command
        command: "echo Building backend..."

  - name: report
    type: run_command
    command: "echo Build completed! Setup: {{setup_result}}"
    depends_on: [build_parallel]
```

### 2. Build the project

Weave uses CMake with vcpkg for dependency management:

```bash
# Configure the project (vcpkg will install dependencies)
cmake --preset=default

# Build the executable
cmake --build build/Ninja/Msvc
```

### 3. Run the workflow

```bash
# Run the workflow (from build directory)
bin/weave.exe my_workflow.yml
```

## Cross-File Task Imports

One of Weave's most powerful features is the ability to create modular, reusable workflows:

### Create shared tasks (`shared.yml`):
```yaml
name: "Shared Build Tasks"
defaults:
  vars:
    BUILD_TYPE: "release"

tasks:
  - name: lint_code
    type: run_command
    command: "echo Linting code in {{BUILD_TYPE}} mode"

  - name: run_tests
    type: run_command
    depends_on: [lint_code]
    command: "echo Running tests"
    outputs:
      stdout_to_variable: "test_result"
```

### Import and use (`main.yml`):
```yaml
imports:
  - "shared.yml"

tasks:
  - name: build_app
    type: run_command
    command: "echo Building application"

  - name: deploy
    type: run_command
    depends_on: [build_app, run_tests]  # run_tests is from imported file
    command: "echo Deploying. Tests: {{test_result}}"
```

**Key Import Features:**
- ✅ **Recursive imports**: Imported files can import other files
- ✅ **Relative path resolution**: Import paths relative to the importing file
- ✅ **Conflict resolution**: Main workflow tasks take precedence over imported ones
- ✅ **Variable merging**: Variables and defaults are intelligently merged

## Architecture Overview

Weave is designed with performance and modularity in mind:

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
- **Task Parser**: Handles YAML parsing with `ryml` and recursive import resolution
- **Enhanced Graph**: Builds and validates task dependency DAGs
- **Workflow Executor**: Orchestrates concurrent task execution with proper synchronization and intelligent error handling
- **Thread Pool**: Custom implementation to avoid deadlocks in nested parallel tasks
- **Expression Evaluator**: PEGTL-based parser for `when` conditions

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
    type: run_command
    command: "echo {{GLOBAL_VAR}} and {{LOCAL_VAR}}"
```

### Conditional Execution
```yaml
tasks:
  - name: prod_only_task
    type: run_command
    command: "echo Deploying to production"
    when: "{{environment}} == 'prod' and {{approval}} == true"
```

### Loop Execution
```yaml
tasks:
  - name: test_multiple_services
    type: run_command
    command: "echo Testing service: {{service}}"
    each:
      items: ["auth", "api", "worker"]
      as: "service"
```

### Error Handling and Recovery
```yaml
tasks:
  - name: critical_deployment
    type: run_command
    command: "deploy --service myapp --env production"
    retries:
      count: 3
      delay: "10s"
    outputs:
      stdout_to_variable: "deploy_log"
      exit_code_to_variable: "deploy_status"
    on_failure: [rollback_deployment, alert_team]

  - name: rollback_deployment
    type: run_command
    command: |
      echo "Deployment failed: {{failed_task_name}}"
      echo "Exit code: {{failed_task_exit_code}}"
      echo "Error: {{failed_task_stderr}}"
      rollback --reason "{{failed_task_error}}"

  - name: alert_team
    type: run_command
    command: |
      send-alert --severity critical \
        --task "{{failed_task_name}}" \
        --details "{{failed_task_error}}"
```

### Output Chaining
```yaml
tasks:
  - name: get_version
    type: run_command
    command: "git describe --tags"
    outputs:
      stdout_to_variable: "version"

  - name: tag_image
    type: run_command
    command: "docker tag myapp:latest myapp:{{version}}"
    depends_on: [get_version]
```

## Documentation

TBD

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
weave/
├── include/           # Public headers
├── src/              # Implementation
│   ├── yml/          # YAML parsing (ryml)
│   ├── dag/          # Graph and execution logic
│   └── util/         # Utilities (expressions, variables)
├── test/             # Unit tests (Catch2)
└── docs/             # Documentation
```

## Performance Characteristics

- **YAML Parsing**: 5-10x faster than yaml-cpp thanks to ryml
- **Memory Usage**: Minimal allocations with object pools and move semantics
- **Concurrency**: Deadlock-free parallel execution with custom thread pool
- **Scalability**: Tested with workflows containing 100+ tasks and deep dependency chains

## License & Status

Weave is actively developed and production-ready. The core engine is feature-complete with:
- ✅ Full YAML workflow specification support
- ✅ Cross-file imports and modular design
- ✅ Thread-safe concurrent execution
- ✅ Comprehensive error handling and logging
- ✅ Windows/Linux/macOS compatibility

## 🚀 What's Next - Community Roadmap

**Help us prioritize!** Vote on features you want most by starring ⭐ issues or contributing PRs.

### 🛠️ Developer Experience
- [ ] **Advanced CLI Tools**
  - [ ] `weave init --template=cpp-library` - Project scaffolding
  - [ ] `weave validate workflow.yml` - Workflow validation
  - [ ] `weave export --github-actions` - Platform integration
  - [ ] `weave benchmark workflow.yml` - Performance profiling
  - [ ] `weave visualize workflow.yml` - DAG visualization

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
- [ ] **Weave Hub - Community Platform**
  - [ ] Collection marketplace with ratings/reviews
  - [ ] Easy sharing and contribution workflows
  - [ ] Semantic versioning for collections
  - [ ] Enterprise collection licensing
- [ ] **Developer Community**
  - [ ] Community Discord/Slack
  - [ ] Monthly community calls
  - [ ] Contribution recognition program
  - [ ] Weave certification program

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

**Join the Journey!** Weave is more than a tool - it's a community building the future of workflow orchestration. Every contribution, big or small, makes a difference.

**Get Started Contributing**: Check our [Contributing Guide](CONTRIBUTING.md) and pick your first issue!
