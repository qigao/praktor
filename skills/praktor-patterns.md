---
name: Praktor Orchestration Patterns
description: Advanced task orchestration patterns for building complex workflows in Praktor
---

# Praktor Orchestration Patterns

This guide covers advanced orchestration patterns for building robust, scalable workflows in Praktor.

## Pattern Categories

1. **Control Flow Patterns** - Conditional execution, branching, loops
2. **Data Flow Patterns** - Data transformation, aggregation, propagation
3. **Error Handling Patterns** - Retries, fallbacks, recovery
4. **Parallelization Patterns** - Concurrent execution, fan-out/fan-in
5. **Integration Patterns** - API orchestration, service composition
6. **State Management Patterns** - Context sharing, checkpointing

---

## 1. Control Flow Patterns

### Pattern 1.1: Conditional Branching (Switch/Case)

Execute different tasks based on a variable value.

```yaml
tasks:
  # Determine environment
  - name: detect_environment
    script:
      source: |
        const branch = context.get("env.GIT_BRANCH");
        let env = "dev";
        if (branch === "main") env = "production";
        else if (branch === "staging") env = "staging";
        context.set("environment", env);

  # Branch 1: Development
  - name: deploy_dev
    depends_on: [detect_environment]
    when: "{{ tasks.detect_environment.outputs.environment }} == 'dev'"
    command: "./deploy.sh --env dev --debug --skip-tests"

  # Branch 2: Staging
  - name: deploy_staging
    depends_on: [detect_environment]
    when: "{{ tasks.detect_environment.outputs.environment }} == 'staging'"
    command: "./deploy.sh --env staging --verify"

  # Branch 3: Production
  - name: deploy_production
    depends_on: [detect_environment]
    when: "{{ tasks.detect_environment.outputs.environment }} == 'production'"
    command: "./deploy.sh --env production --strict --backup"
```

### Pattern 1.2: Early Exit (Guard Clauses)

Skip entire workflow branches based on conditions.

```yaml
tasks:
  - name: check_prerequisites
    script:
      source: |
        const hasDocker = context.get("env.DOCKER_AVAILABLE") === "true";
        const hasK8s = context.get("env.K8S_AVAILABLE") === "true";
        context.set("can_deploy", hasDocker && hasK8s);

  - name: build
    depends_on: [check_prerequisites]
    when: "{{ tasks.check_prerequisites.outputs.can_deploy }}"
    command: "docker build -t myapp ."

  - name: deploy
    depends_on: [build]
    when: "{{ tasks.check_prerequisites.outputs.can_deploy }}"
    command: "kubectl apply -f deployment.yml"

  - name: skip_notification
    depends_on: [check_prerequisites]
    when: "not {{ tasks.check_prerequisites.outputs.can_deploy }}"
    script:
      source: |
        console.log("Deployment skipped: prerequisites not met");
```

### Pattern 1.3: Iterative Processing (For-Each)

Process a list of items sequentially or in parallel.

```yaml
tasks:
  - name: discover_services
    command: "kubectl get services -o json"
    output_format: json

  - name: health_check_all
    depends_on: [discover_services]
    dynamic_tasks:
      items_variable: "{{ tasks.discover_services.outputs.data.items }}"
      template:
        name: "health_check_{{ item.metadata.name }}"
        http:
          url: "http://{{ item.spec.clusterIP }}:{{ item.spec.ports[0].port }}/health"
          method: GET
          timeout_ms: 5000
          test: |
            if (response.status !== 200) {
                fail("Service unhealthy: " + response.status);
            }
```

### Pattern 1.4: Matrix Build

Execute tasks across multiple dimensions.

```yaml
tasks:
  - name: build_matrix
    each:
      matrix:
        os: ["linux", "windows", "macos"]
        arch: ["amd64", "arm64"]
        go_version: ["1.21", "1.22"]
      as: "config"
    command: |
      docker run --rm \
        -e GOOS={{ config.os }} \
        -e GOARCH={{ config.arch }} \
        golang:{{ config.go_version }} \
        go build -o dist/app-{{ config.os }}-{{ config.arch }}
    timeout: "15m"
```

---

## 2. Data Flow Patterns

### Pattern 2.1: Pipeline (Chain of Transformations)

Transform data through multiple stages.

```yaml
tasks:
  - name: extract
    http:
      url: "https://api.example.com/raw-data"
      method: GET

  - name: transform
    depends_on: [extract]
    script:
      source: |
        const raw = context.get("tasks.extract.outputs.data");
        
        const transformed = raw.items.map(item => ({
          id: item.external_id,
          name: item.title.toUpperCase(),
          timestamp: new Date().toISOString(),
          tags: item.categories.join(",")
        }));
        
        context.set("records", transformed);
        context.set("count", transformed.length);

  - name: validate
    depends_on: [transform]
    script:
      source: |
        const records = context.get("tasks.transform.outputs.records");
        
        const invalid = records.filter(r => 
          !r.id || !r.name || r.name.length < 3
        );
        
        if (invalid.length > 0) {
          fail(`Found ${invalid.length} invalid records`);
        }
        
        context.set("validated", records);

  - name: load
    depends_on: [validate]
    http:
      url: "https://api.internal.com/data"
      method: POST
      headers:
        Content-Type: "application/json"
      body: "{{ tasks.validate.outputs.validated }}"
```

### Pattern 2.2: Aggregation (Reduce)

Combine results from multiple tasks.

```yaml
tasks:
  - name: fetch_metrics_api1
    http:
      url: "https://api1.example.com/metrics"
      method: GET

  - name: fetch_metrics_api2
    http:
      url: "https://api2.example.com/metrics"
      method: GET

  - name: fetch_metrics_api3
    http:
      url: "https://api3.example.com/metrics"
      method: GET

  - name: aggregate_metrics
    depends_on: [fetch_metrics_api1, fetch_metrics_api2, fetch_metrics_api3]
    script:
      source: |
        const m1 = context.get("tasks.fetch_metrics_api1.outputs.data");
        const m2 = context.get("tasks.fetch_metrics_api2.outputs.data");
        const m3 = context.get("tasks.fetch_metrics_api3.outputs.data");
        
        const total = {
          requests: m1.requests + m2.requests + m3.requests,
          errors: m1.errors + m2.errors + m3.errors,
          latency_avg: (m1.latency + m2.latency + m3.latency) / 3
        };
        
        context.set("aggregated", total);
```

### Pattern 2.3: Scatter-Gather

Distribute work and collect results.

```yaml
tasks:
  - name: split_work
    script:
      source: |
        const data = context.get("input_data");
        const chunkSize = 100;
        const chunks = [];
        
        for (let i = 0; i < data.length; i += chunkSize) {
          chunks.push(data.slice(i, i + chunkSize));
        }
        
        context.set("chunks", chunks);
        context.set("chunk_count", chunks.length);

  # Scatter: Process each chunk
  - name: process_chunks
    depends_on: [split_work]
    dynamic_tasks:
      items_variable: "{{ tasks.split_work.outputs.chunks }}"
      template:
        name: "process_chunk_{{ index }}"
        script:
          source: |
            const chunk = {{ item }};
            const processed = chunk.map(x => x * 2);
            context.set("result", processed);

  # Gather: Combine results
  - name: gather_results
    depends_on: [process_chunks]
    script:
      source: |
        const chunkCount = context.get("tasks.split_work.outputs.chunk_count");
        let allResults = [];
        
        for (let i = 0; i < chunkCount; i++) {
          const result = context.get(`tasks.process_chunk_${i}.outputs.result`);
          allResults = allResults.concat(result);
        }
        
        context.set("final_results", allResults);
```

---

## 3. Error Handling Patterns

### Pattern 3.1: Retry with Exponential Backoff

Retry failed tasks with increasing delays.

```yaml
tasks:
  - name: flaky_api_call
    http:
      url: "https://api.flaky.com/data"
      method: GET
    retries:
      count: 5
      delay: "2s"  # Doubles each time: 2s, 4s, 8s, 16s, 32s
    timeout_ms: 10000
```

### Pattern 3.2: Fallback Chain

Try multiple alternatives until one succeeds.

```yaml
tasks:
  - name: try_primary_api
    http:
      url: "https://primary-api.com/data"
      method: GET
      timeout_ms: 5000
    continue_on_error: true

  - name: try_secondary_api
    depends_on: [try_primary_api]
    when: "{{ tasks.try_primary_api.outputs.status }} != 200"
    http:
      url: "https://secondary-api.com/data"
      method: GET
      timeout_ms: 5000
    continue_on_error: true

  - name: use_cached_data
    depends_on: [try_secondary_api]
    when: "{{ tasks.try_secondary_api.outputs.status }} != 200"
    script:
      source: |
        import fs from 'turbo:fs';
        const cached = fs.readJson('./cache/data.json');
        context.set("data", cached);
        context.set("source", "cache");
```

### Pattern 3.3: Circuit Breaker

Stop attempting after repeated failures.

```yaml
tasks:
  - name: check_circuit_state
    script:
      source: |
        import fs from 'turbo:fs';
        
        let state = { failures: 0, lastFailure: null };
        try {
          state = fs.readJson('./circuit-state.json');
        } catch (e) {}
        
        const now = Date.now();
        const cooldownMs = 60000; // 1 minute
        
        const isOpen = state.failures >= 3 && 
                      (now - state.lastFailure) < cooldownMs;
        
        context.set("circuit_open", isOpen);
        context.set("failure_count", state.failures);

  - name: call_service
    depends_on: [check_circuit_state]
    when: "not {{ tasks.check_circuit_state.outputs.circuit_open }}"
    http:
      url: "https://api.example.com/data"
      method: GET
    triggers:
      on_failure: [record_failure]
      on_success: [reset_circuit]

  - name: record_failure
    script:
      source: |
        import fs from 'turbo:fs';
        const failures = context.get("tasks.check_circuit_state.outputs.failure_count") + 1;
        fs.writeJson('./circuit-state.json', {
          failures,
          lastFailure: Date.now()
        });

  - name: reset_circuit
    script:
      source: |
        import fs from 'turbo:fs';
        fs.writeJson('./circuit-state.json', { failures: 0, lastFailure: null });
```

### Pattern 3.4: Compensating Transaction (Saga)

Rollback on failure.

```yaml
tasks:
  - name: reserve_inventory
    http:
      url: "https://inventory-api.com/reserve"
      method: POST
      body: '{"item": "{{ item_id }}", "quantity": {{ quantity }}}'
    triggers:
      on_failure: [notify_failure]

  - name: charge_payment
    depends_on: [reserve_inventory]
    http:
      url: "https://payment-api.com/charge"
      method: POST
      body: '{"amount": {{ amount }}, "customer": "{{ customer_id }}"}'
    triggers:
      on_failure: [rollback_inventory, notify_failure]

  - name: ship_order
    depends_on: [charge_payment]
    http:
      url: "https://shipping-api.com/ship"
      method: POST
      body: '{"order": "{{ order_id }}"}'
    triggers:
      on_failure: [refund_payment, rollback_inventory, notify_failure]

  # Compensation tasks
  - name: rollback_inventory
    http:
      url: "https://inventory-api.com/release"
      method: POST
      body: '{"reservation_id": "{{ tasks.reserve_inventory.outputs.data.id }}"}'

  - name: refund_payment
    http:
      url: "https://payment-api.com/refund"
      method: POST
      body: '{"transaction_id": "{{ tasks.charge_payment.outputs.data.id }}"}'

  - name: notify_failure
    script:
      source: |
        import { post } from 'turbo:http';
        const msg = `Order failed: ${context.get("failed_task_error")}`;
        post(process.env.SLACK_WEBHOOK, JSON.stringify({text: msg}));
```

---

## 4. Parallelization Patterns

### Pattern 4.1: Independent Parallel Tasks

Execute tasks concurrently when they don't depend on each other.

```yaml
tasks:
  - name: checkout_code
    command: "git clone {{ repo_url }} ."

  # These run in parallel after checkout
  - name: lint_javascript
    depends_on: [checkout_code]
    command: "npm run lint"

  - name: lint_python
    depends_on: [checkout_code]
    command: "pylint src/"

  - name: lint_go
    depends_on: [checkout_code]
    command: "golangci-lint run"

  - name: security_scan
    depends_on: [checkout_code]
    command: "trivy fs ."

  # Wait for all linters
  - name: report
    depends_on: [lint_javascript, lint_python, lint_go, security_scan]
    script:
      source: |
        console.log("All checks completed");
```

### Pattern 4.2: Staged Pipeline

Execute stages sequentially, tasks within stages in parallel.

```yaml
tasks:
  # Stage 1: Build (parallel)
  - name: build_frontend
    command: "npm run build"
    working_dir: "./frontend"

  - name: build_backend
    command: "cargo build --release"
    working_dir: "./backend"

  - name: build_docs
    command: "mkdocs build"
    working_dir: "./docs"

  # Stage 2: Test (parallel, depends on build)
  - name: test_frontend
    depends_on: [build_frontend]
    command: "npm test"
    working_dir: "./frontend"

  - name: test_backend
    depends_on: [build_backend]
    command: "cargo test"
    working_dir: "./backend"

  # Stage 3: Deploy (sequential, depends on tests)
  - name: deploy
    depends_on: [test_frontend, test_backend]
    command: "./deploy.sh"
```

### Pattern 4.3: Rate-Limited Parallel Execution

Control concurrency with batching.

```yaml
tasks:
  - name: get_items
    script:
      source: |
        const items = Array.from({length: 100}, (_, i) => i);
        const batchSize = 10;
        const batches = [];
        
        for (let i = 0; i < items.length; i += batchSize) {
          batches.push(items.slice(i, i + batchSize));
        }
        
        context.set("batches", batches);

  - name: process_batch_0
    depends_on: [get_items]
    dynamic_tasks:
      items_variable: "{{ tasks.get_items.outputs.batches[0] }}"
      template:
        name: "process_item_{{ item }}"
        command: "./process.sh {{ item }}"

  - name: process_batch_1
    depends_on: [process_batch_0]
    dynamic_tasks:
      items_variable: "{{ tasks.get_items.outputs.batches[1] }}"
      template:
        name: "process_item_{{ item }}"
        command: "./process.sh {{ item }}"
```

---

## 5. Integration Patterns

### Pattern 5.1: API Composition

Combine multiple API calls into a single result.

```yaml
tasks:
  - name: get_user
    http:
      url: "https://api.example.com/users/{{ user_id }}"
      method: GET

  - name: get_orders
    http:
      url: "https://api.example.com/orders?user={{ user_id }}"
      method: GET

  - name: get_preferences
    http:
      url: "https://api.example.com/preferences/{{ user_id }}"
      method: GET

  - name: compose_profile
    depends_on: [get_user, get_orders, get_preferences]
    script:
      source: |
        const user = context.get("tasks.get_user.outputs.data");
        const orders = context.get("tasks.get_orders.outputs.data");
        const prefs = context.get("tasks.get_preferences.outputs.data");
        
        const profile = {
          ...user,
          totalOrders: orders.length,
          lastOrder: orders[0],
          preferences: prefs
        };
        
        context.set("profile", profile);
```

### Pattern 5.2: Webhook Orchestration

Chain webhooks with state management.

```yaml
tasks:
  - name: trigger_build
    http:
      url: "https://ci.example.com/build"
      method: POST
      body: '{"repo": "{{ repo }}", "branch": "{{ branch }}"}'

  - name: wait_for_build
    depends_on: [trigger_build]
    script:
      source: |
        import { sleep } from 'turbo:timers';
        import { get } from 'turbo:http';
        
        const buildId = context.get("tasks.trigger_build.outputs.data.id");
        let status = "pending";
        
        while (status === "pending" || status === "running") {
          sleep(5000);
          const resp = get(`https://ci.example.com/builds/${buildId}`);
          const data = JSON.parse(resp.body);
          status = data.status;
        }
        
        if (status !== "success") {
          fail(`Build failed with status: ${status}`);
        }
        
        context.set("build_url", data.url);
```

### Pattern 5.3: Event-Driven Workflow

React to external events.

```yaml
tasks:
  - name: poll_queue
    http:
      url: "https://queue.example.com/messages?limit=10"
      method: GET

  - name: process_messages
    depends_on: [poll_queue]
    when: "len({{ tasks.poll_queue.outputs.data.messages }}) > 0"
    dynamic_tasks:
      items_variable: "{{ tasks.poll_queue.outputs.data.messages }}"
      template:
        name: "process_{{ item.id }}"
        script:
          source: |
            const message = {{ item }};
            // Process message
            console.log("Processing:", message.id);
            context.set("processed", true);

  - name: acknowledge_messages
    depends_on: [process_messages]
    http:
      url: "https://queue.example.com/ack"
      method: POST
      body: '{"ids": {{ tasks.poll_queue.outputs.data.messages | map: "id" }}}'
```

---

## 6. State Management Patterns

### Pattern 6.1: Checkpointing

Save progress for resumability.

```yaml
tasks:
  - name: load_checkpoint
    script:
      source: |
        import fs from 'turbo:fs';
        let checkpoint = { lastProcessed: 0 };
        try {
          checkpoint = fs.readJson('./checkpoint.json');
        } catch (e) {}
        context.set("last_processed", checkpoint.lastProcessed);

  - name: process_data
    depends_on: [load_checkpoint]
    script:
      source: |
        import fs from 'turbo:fs';
        const start = context.get("tasks.load_checkpoint.outputs.last_processed");
        const data = context.get("input_data");
        
        for (let i = start; i < data.length; i++) {
          // Process item
          console.log("Processing item:", i);
          
          // Save checkpoint every 100 items
          if (i % 100 === 0) {
            fs.writeJson('./checkpoint.json', { lastProcessed: i });
          }
        }
        
        fs.writeJson('./checkpoint.json', { lastProcessed: data.length });
```

### Pattern 6.2: Shared State Across Tasks

Use context for inter-task communication.

```yaml
tasks:
  - name: initialize_state
    script:
      source: |
        context.set("counter", 0);
        context.set("errors", []);
        context.set("results", []);

  - name: task_1
    depends_on: [initialize_state]
    script:
      source: |
        const counter = context.get("tasks.initialize_state.outputs.counter");
        context.set("counter", counter + 1);
        context.set("task1_result", "success");

  - name: task_2
    depends_on: [task_1]
    script:
      source: |
        const counter = context.get("tasks.task_1.outputs.counter");
        const results = context.get("tasks.initialize_state.outputs.results");
        results.push("task2");
        context.set("counter", counter + 1);
        context.set("results", results);
```

### Pattern 6.3: Idempotency

Ensure tasks can be safely retried.

```yaml
tasks:
  - name: check_already_deployed
    http:
      url: "https://api.example.com/deployments/{{ deployment_id }}"
      method: GET
      test: |
        // 404 means not deployed yet
        if (response.status === 404) {
          context.set("already_deployed", false);
        } else if (response.status === 200) {
          context.set("already_deployed", true);
        } else {
          fail("Unexpected status: " + response.status);
        }

  - name: deploy
    depends_on: [check_already_deployed]
    when: "not {{ tasks.check_already_deployed.outputs.already_deployed }}"
    command: "./deploy.sh --id {{ deployment_id }}"
```

---

## Best Practices

1. **Use descriptive task names** - Makes dependency graphs readable
2. **Minimize task dependencies** - Enables maximum parallelization
3. **Use `continue_on_error` wisely** - For optional tasks only
4. **Set appropriate timeouts** - Prevent hanging workflows
5. **Leverage dynamic tasks** - For data-driven workflows
6. **Use triggers for side effects** - Notifications, cleanup, logging
7. **Validate early** - Fail fast on invalid inputs
8. **Make tasks idempotent** - Safe to retry
9. **Use context for state** - Share data between tasks
10. **Document complex workflows** - Add descriptions to tasks

## Anti-Patterns to Avoid

❌ **Tight coupling** - Tasks that know too much about each other  
❌ **Deep nesting** - Too many dependency levels  
❌ **Silent failures** - Always handle errors explicitly  
❌ **Hardcoded values** - Use variables and environment  
❌ **Monolithic tasks** - Break down into smaller, reusable tasks  
❌ **Missing timeouts** - Can cause workflows to hang indefinitely  
❌ **Ignoring retries** - Network calls should always have retry logic  

---

## Pattern Selection Guide

| Use Case | Recommended Pattern |
|----------|-------------------|
| Multiple environments | Conditional Branching (1.1) |
| Process list of items | Iterative Processing (1.3) |
| Multi-stage build | Staged Pipeline (4.2) |
| API data aggregation | Aggregation (2.2) |
| Unreliable external service | Retry + Fallback (3.1 + 3.2) |
| Distributed transaction | Compensating Transaction (3.4) |
| Combine multiple APIs | API Composition (5.1) |
| Long-running process | Checkpointing (6.1) |
| Fan-out work | Scatter-Gather (2.3) |
| Rate-limited API | Rate-Limited Parallel (4.3) |
