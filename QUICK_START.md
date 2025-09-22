# Weave Quick Start Guide

**Get productive with Weave workflows in 5 minutes**

## 🚀 Installation & First Run

```bash
# Build Weave
cmake --build build/Ninja/Msvc --target weave

# Run your first workflow
weave examples/hello-world.yml

# With performance monitoring
weave examples/hello-world.yml --concurrent --jobs 4
```

## 📝 Your First Workflow

Create `my-first-workflow.yml`:

```yaml
variables:
  PROJECT_NAME: "MyApp"
  VERSION: "1.0.0"

tasks:
  - name: welcome
    type: run_command
    command: "echo 'Welcome to {{PROJECT_NAME}} v{{VERSION}}!'"
    outputs:
      stdout_to_variable: "welcome_message"

  - name: setup_parallel
    type: parallel
    depends_on: [welcome]
    tasks: [create_dirs, install_deps, configure_env]

  - name: create_dirs
    type: create_directory
    path: "./build"
    parents: true

  - name: install_deps
    type: run_command
    command: "echo 'Installing dependencies...'"

  - name: configure_env
    type: run_command  
    command: "echo 'Configuring environment...'"

  - name: build
    type: run_command
    depends_on: [setup_parallel]
    command: "echo 'Building {{PROJECT_NAME}}...'"

  - name: celebrate
    type: run_command
    depends_on: [build]
    command: "echo '{{welcome_message}} Build complete! 🎉'"
```

Run it:
```bash
weave my-first-workflow.yml --verbose
```

You'll see performance stats at the end:
```
=== Executor Pool Performance Stats ===
Cache hit rate: 85.7%
GOOD: Decent cache efficiency - moderate performance gain
```

## 🎯 Common Patterns

### **CI/CD Pipeline**
```yaml
tasks:
  - name: test_and_build
    type: parallel
    tasks: [run_tests, build_app, security_scan]

  - name: deploy_staging
    type: run_command
    depends_on: [test_and_build]
    command: "deploy --env staging"
    when: "{{branch}} == 'develop'"

  - name: deploy_production  
    type: run_command
    depends_on: [test_and_build]
    command: "deploy --env production"
    when: "{{branch}} == 'main'"
```

### **Error Handling & Recovery**
```yaml
tasks:
  - name: deploy_app
    type: run_command
    command: "kubectl apply -f deployment.yaml"
    retries:
      count: 3
      delay: "5s"
    on_failure: [rollback_deploy, notify_ops]

  - name: rollback_deploy
    type: run_command
    command: |
      echo "Deployment {{failed_task_name}} failed (exit {{failed_task_exit_code}})"
      kubectl rollout undo deployment/myapp
      
  - name: notify_ops
    type: run_command  
    command: |
      alert --message "Deploy failed: {{failed_task_error}}"
```

### **Dynamic Microservices**
```yaml
variables:
  services: '[{"name": "auth"}, {"name": "api"}, {"name": "worker"}]'

tasks:
  - name: deploy_all_services
    type: dynamic_tasks
    items_variable: "{{services}}"
    task_template:
      name: "deploy_{{item.name}}"
      type: run_command
      command: "docker run {{item.name}}:latest"
```

### **Conditional Deployment**
```yaml
tasks:
  - name: health_check
    type: run_command
    command: "curl -f http://api/health"
    outputs:
      exit_code_to_variable: "health_status"

  - name: deploy_strategy
    type: choose
    depends_on: [health_check]
    branches:
      - when: "{{health_status}} == '0' and {{env}} == 'prod'"
        tasks: [rolling_deployment]
      - when: "{{health_status}} == '0'"
        tasks: [standard_deployment]
    default: [emergency_rollback]
```

## 🔧 Command Line Options

```bash
# Basic execution
weave workflow.yml

# Concurrent execution with 8 threads
weave workflow.yml --concurrent --jobs 8

# Run specific task and its dependencies
weave workflow.yml --task deploy_production

# Validate workflow without execution
weave workflow.yml --validate

# Verbose output with performance monitoring
weave workflow.yml --verbose --concurrent
```

## 🎉 Next Steps

1. **Explore Examples**: Check `weave/docs/examples/` for advanced patterns
2. **Modular Workflows**: Use `imports` to share tasks across projects  
3. **Error Recovery**: Leverage `on_failure` with automatic failure context for robust production workflows
4. **Performance Tuning**: Monitor cache hit rates and optimize task types
5. **Production Usage**: Add monitoring and rollback strategies

**Ready for enterprise workflows!** 🚀