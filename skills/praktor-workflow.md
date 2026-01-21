# Praktor Workflow Skill

Generate Praktor workflow YAML files based on user requirements.

## Workflow Structure

```yaml
name: "Workflow Name"           # Optional
description: "Description"      # Optional

variables:                      # Global constants
  KEY: "value"

env:                           # Global environment variables
  ENV_VAR: "value"

dotEnv:                        # Load .env files
  - ".env"

defaults:                      # Default task settings
  retries:
    count: 2
    delay: "5s"
  timeout: "10m"

embedded:                      # Embedded JS modules
  module_name:
    source: |
      export function helper() { return "value"; }

tasks:                         # Required: task list
  - name: task_name
    # ... task definition
```

## Task Definition

Every task requires `name` and exactly one runner: `command`, `script`, `uses`, or `dynamic_tasks`.

### Core Attributes

```yaml
- name: my_task                 # Required: unique identifier
  description: "Task summary"   # Optional
  depends_on: [task1, task2]    # Dependencies
  vars:                         # Task-scoped variables
    VAR: "value"
  env:                          # Task-scoped environment
    ENV: "value"
  when: "{{ condition expression }}" # Conditional execution
  timeout: "5m"                 # Max execution time
  retries:
    count: 3
    delay: "10s"
  continue_on_error: true       # Don't fail workflow on error
  working_dir: "./subdir"       # Working directory
  parameters:                   # For 'uses' tasks
    key: "value"
```

## Runners

### 1. Command Runner

```yaml
- name: build
  command: "npm run build"
  output_format: json           # Parse stdout as JSON
```

**Outputs:**
- `tasks.<name>.outputs.stdout` - Standard output
- `tasks.<name>.outputs.stderr` - Standard error
- `tasks.<name>.outputs.exit_code` - Exit code
- `tasks.<name>.outputs.data` - Parsed JSON (if output_format: json)

### 2. Script Runner

```yaml
- name: process_data
  script:
    source: |
      const data = context.get("tasks.fetch.outputs.data");
      const result = data.filter(x => x.active);
      context.set("filtered", result);
    language: javascript        # Optional
    modules: [helper_module]    # Load embedded modules
```

**ES6 Imports (auto-detected):**

```yaml
- name: with_imports
  script:
    source: |
      import fs from 'turbo:fs';
      import { hostname } from 'turbo:os';
      import { get } from 'turbo:http';
      import myLib from './lib/helpers.js';

      const config = fs.readJson('./config.json');
      context.set("host", hostname());
```

**Built-in Modules:**
- `turbo:fs` - readFile, writeFile, stat, readdir, mkdir, join, readJson, writeJson
- `turbo:os` - hostname, homedir, cwd, chdir, getenv, setenv, pid, ppid, uptime
- `turbo:dns` - resolve, resolveAsync, setServers, getServers
- `turbo:http` - get, post, request
- `turbo:timers` - setTimeout, setInterval, clearTimeout, clearInterval, sleep
- `turbo:utils` - base64Encode, base64Decode
- `turbo:net` - TcpClient, WebSocket
- `turbo:signal` - watch, SIGINT, SIGTERM
- `turbo:proc` - spawn, kill

**Legacy Global Object:**
```javascript
turbo.fs.stat(".");
turbo.os.hostname();
```

### 3. Uses Runner (Reusable Workflows)

```yaml
- name: deploy
  uses: ./workflows/deploy.yml
  vars:
    IMAGE: "{{ tasks.build.outputs.tag }}"
```

### 4. Dynamic Tasks Runner

```yaml
- name: deploy_services
  dynamic_tasks:
    items_variable: "tasks.discover.outputs.data"
    template:
      name: "deploy_{{ item.name }}"
      command: "./deploy.sh {{ item.name }}"
```

## Templating (Mustache)

Praktor uses **Mustache** for string interpolation in commands, vars, env, and script sources.

```yaml
# Basic substitution
command: "echo {{ VERSION }}"

# Object Access
command: "echo {{ tasks.build.outputs.data.version.tag }}"

# Sections and Loops
command: |
  echo "Assets:"
  {{#tasks.build.outputs.data.assets}}
  echo "- {{ name }} ({{ size }})"
  {{/tasks.build.outputs.data.assets}}

# Inverted Sections (Conditional)
command: |
  {{^env.SKIP_NOTIFY}}
  curl -X POST {{ env.WEBHOOK }} -d '{"text": "Done"}'
  {{/env.SKIP_NOTIFY}}
```

## Loop Execution (each)

```yaml
- name: build_matrix
  each:
    matrix:
      os: ["linux", "windows"]
      arch: ["amd64", "arm64"]
    as: "cfg"
  command: "./build.sh --os {{ cfg.os }} --arch {{ cfg.arch }}"
```

## Triggers

Triggers execute other tasks when a task completes. They reference tasks by name.

```yaml
tasks:
  - name: build
    command: "npm run build"
    triggers:
      on_success: [notify_slack]
      on_failure: [rollback, notify_slack]

  - name: notify_slack
    script:
      source: |
        import { post } from 'turbo:http';
        // Mustache substitution works inside script sources too!
        const msg = "Task {{ failed_task_name }} failed with: {{ failed_task_error }}";
        post(process.env.SLACK_WEBHOOK, JSON.stringify({text: msg}));

  - name: rollback
    command: "git checkout ."
```

### Failure Context Variables
Available in triggered tasks after a failure:
- `failed_task_name`
- `failed_task_type`
- `failed_task_exit_code`
- `failed_task_stdout`
- `failed_task_stderr`
- `failed_task_error`

## Expression Language

**Syntax:** `{{ expression }}`

**Variable Access:**
- `{{ VERSION }}` - Simple variable
- `{{ tasks.build.outputs.version }}` - Nested path
- `{{ env.NODE_ENV }}` - Environment variable
- `{{ os.name }}`, `{{ os.arch }}` - System info

**Operators (in `when`):**
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `and`, `or`, `not`, `!`
- String: `contains`, `starts_with`, `ends_with`, `in`, `matches`
- Arithmetic: `+`, `-`, `*`, `/`, `%`

**Functions:**
- `len(value)` - Length
- `empty(value)` - Check empty
- `abs(number)` - Absolute value
- `bool(value)` - Convert to boolean

## Examples

### CI/CD Pipeline

```yaml
name: CI/CD Pipeline
variables:
  APP_NAME: my-app

tasks:
  - name: checkout
    command: "git checkout {{ env.BRANCH }}"

  - name: install
    depends_on: [checkout]
    command: "npm ci"

  - name: test
    depends_on: [install]
    command: "npm test"
    continue_on_error: true

  - name: build
    depends_on: [install]
    command: "npm run build"
    when: "{{ tasks.test.outputs.exit_code }} == 0"

  - name: deploy
    depends_on: [build]
    command: "./deploy.sh"
    when: "{{ env.BRANCH }} == 'main'"
    triggers:
      on_failure: [notify_failure]

  - name: notify_failure
    script:
      source: |
        import { post } from 'turbo:http';
        post(process.env.SLACK, JSON.stringify({text: "Deploy failed for {{ APP_NAME }}"}));
```

### Data Processing

```yaml
name: Data Processing
tasks:
  - name: fetch_data
    command: "curl -s https://api.example.com/data"
    output_format: json

  - name: process
    depends_on: [fetch_data]
    script:
      source: |
        import fs from 'turbo:fs';

        const data = context.get("tasks.fetch_data.outputs.data");
        const filtered = data.filter(x => x.status === "active");

        fs.writeJson("./output/processed.json", filtered);
        context.set("count", filtered.length);

  - name: notify
    depends_on: [process]
    command: |
      curl -X POST {{ env.WEBHOOK }} \
        -d '{"processed": {{ tasks.process.outputs.count }}}'
```

## Typical Use Cases

### 1. Build and Test Pipeline

```yaml
name: Build and Test
variables:
  NODE_VERSION: "20"

tasks:
  - name: install
    command: "npm ci"
    timeout: "5m"

  - name: lint
    depends_on: [install]
    command: "npm run lint"
    continue_on_error: true

  - name: test
    depends_on: [install]
    command: "npm test"
    retries:
      count: 2
      delay: "5s"

  - name: build
    depends_on: [test]
    command: "npm run build"
    when: "{{ tasks.test.outputs.exit_code }} == 0"
    triggers:
      on_success: [notify_success]
      on_failure: [notify_failure]

  - name: notify_success
    script:
      source: "print('Build completed successfully')"

  - name: notify_failure
    script:
      source: "print('Build failed: {{ failed_task_error }}')"
```

### 2. Docker Build and Push

```yaml
name: Docker Build
variables:
  REGISTRY: "ghcr.io/myorg"
  IMAGE_NAME: "myapp"

tasks:
  - name: get_version
    command: "git describe --tags --always"

  - name: build_image
    depends_on: [get_version]
    command: |
      docker build \
        -t {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ tasks.get_version.outputs.stdout }} \
        -t {{ REGISTRY }}/{{ IMAGE_NAME }}:latest \
        .
    timeout: "15m"

  - name: push_image
    depends_on: [build_image]
    command: |
      docker push {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ tasks.get_version.outputs.stdout }}
      docker push {{ REGISTRY }}/{{ IMAGE_NAME }}:latest
    when: "{{ env.BRANCH }} == 'main'"
```

### 3. Multi-Environment Deployment

```yaml
name: Deploy to Environments
variables:
  APP_NAME: "myapp"

tasks:
  - name: deploy
    each:
      items: ["staging", "production"]
      as: "env"
    command: "./deploy.sh --env {{ env }} --app {{ APP_NAME }}"
    when: "{{ env }} == 'staging' or {{ env.DEPLOY_PROD }} == 'true'"
    retries:
      count: 3
      delay: "30s"
    triggers:
      on_failure: [notify_failure]

  - name: notify_failure
    script:
      source: |
        import { post } from 'turbo:http';
        const msg = `Deploy to {{ env }} failed!`;
        post(process.env.SLACK_WEBHOOK, JSON.stringify({text: msg}));
```

### 4. Database Migration with Rollback

```yaml
name: Database Migration
tasks:
  - name: backup_db
    command: "pg_dump -h {{ env.DB_HOST }} mydb > backup.sql"
    timeout: "10m"

  - name: run_migrations
    depends_on: [backup_db]
    command: "npm run migrate"
    triggers:
      on_failure: [rollback_db]

  - name: verify_migrations
    depends_on: [run_migrations]
    command: "npm run migrate:verify"
    triggers:
      on_failure: [rollback_db]

  - name: rollback_db
    command: "psql -h {{ env.DB_HOST }} mydb < backup.sql"
    timeout: "10m"
```

### 5. API Data Sync

```yaml
name: API Data Sync
env:
  API_KEY: "{{ env.EXTERNAL_API_KEY }}"

tasks:
  - name: fetch_external
    command: "curl -s -H 'Authorization: Bearer {{ API_KEY }}' https://api.external.com/data"
    output_format: json

  - name: transform_data
    depends_on: [fetch_external]
    script:
      source: |
        import fs from 'turbo:fs';
        import http from 'turbo:http';

        const external = context.get("tasks.fetch_external.outputs.data");

        const transformed = external.items.map(item => ({
          id: item.external_id,
          name: item.title,
          updated: new Date().toISOString()
        }));

        context.set("records", transformed);
        context.set("count", transformed.length);

  - name: sync_to_internal
    depends_on: [transform_data]
    command: |
      curl -X POST {{ env.INTERNAL_API }}/sync \
        -H "Content-Type: application/json" \
        -d '{{ tasks.transform_data.outputs.records }}'
```

### 6. Scheduled Report Generation

```yaml
name: Daily Report
variables:
  REPORT_DIR: "./reports"

tasks:
  - name: collect_metrics
    command: "curl -s {{ env.METRICS_API }}/daily"
    output_format: json

  - name: generate_report
    depends_on: [collect_metrics]
    script:
      source: |
        import fs from 'turbo:fs';

        const metrics = context.get("tasks.collect_metrics.outputs.data");
        const date = new Date().toISOString().split('T')[0];

        const report = {
          date,
          summary: {
            total_requests: metrics.requests,
            error_rate: (metrics.errors / metrics.requests * 100).toFixed(2) + '%',
            avg_response_time: metrics.avg_latency + 'ms'
          },
          details: metrics.endpoints
        };

        fs.mkdir("{{ REPORT_DIR }}");
        fs.writeJson(`{{ REPORT_DIR }}/report-${date}.json`, report);
        context.set("report_path", `{{ REPORT_DIR }}/report-${date}.json`);

  - name: send_report
    depends_on: [generate_report]
    command: |
      curl -X POST {{ env.SLACK_WEBHOOK }} \
        -H "Content-Type: application/json" \
        -d '{"text": "Daily report generated: {{ tasks.generate_report.outputs.report_path }}"}'
```

### 7. Monorepo Build with Dynamic Tasks

```yaml
name: Monorepo Build
tasks:
  - name: detect_changes
    command: "git diff --name-only HEAD~1 | grep -E '^packages/' | cut -d/ -f2 | sort -u"
    script:
      source: |
        const stdout = context.get("tasks.detect_changes.outputs.stdout");
        const packages = stdout.trim().split('\n').filter(p => p);
        context.set("changed_packages", packages);

  - name: build_packages
    depends_on: [detect_changes]
    dynamic_tasks:
      items_variable: "{{ tasks.detect_changes.outputs.changed_packages }}"
      template:
        name: "build_{{ item }}"
        working_dir: "./packages/{{ item }}"
        command: "npm run build"
        timeout: "10m"

  - name: test_packages
    depends_on: [build_packages]
    dynamic_tasks:
      items_variable: "{{ tasks.detect_changes.outputs.changed_packages }}"
      template:
        name: "test_{{ item }}"
        working_dir: "./packages/{{ item }}"
        command: "npm test"
```

### 8. Infrastructure Provisioning

```yaml
name: Infrastructure Setup
variables:
  TERRAFORM_DIR: "./terraform"
  ENVIRONMENT: "{{ env.DEPLOY_ENV }}"

tasks:
  - name: terraform_init
    working_dir: "{{ TERRAFORM_DIR }}"
    command: "terraform init"

  - name: terraform_plan
    depends_on: [terraform_init]
    working_dir: "{{ TERRAFORM_DIR }}"
    command: "terraform plan -var='environment={{ ENVIRONMENT }}' -out=tfplan"

  - name: terraform_apply
    depends_on: [terraform_plan]
    working_dir: "{{ TERRAFORM_DIR }}"
    command: "terraform apply -auto-approve tfplan"
    when: "{{ env.AUTO_APPROVE }} == 'true'"
    triggers:
      on_failure: [notify_failure]

  - name: notify_failure
    script:
      source: |
        import { post } from 'turbo:http';
        post(process.env.SLACK, JSON.stringify({text: "Terraform apply failed for {{ ENVIRONMENT }}"}));

  - name: output_values
    depends_on: [terraform_apply]
    working_dir: "{{ TERRAFORM_DIR }}"
    command: "terraform output -json"
    output_format: json
```

### 9. Health Check and Alerting

```yaml
name: Health Check
variables:
  SERVICES:
    - name: api
      url: "https://api.example.com/health"
    - name: web
      url: "https://www.example.com/health"
    - name: db
      url: "https://db.example.com/health"

tasks:
  - name: check_services
    each:
      items: "{{ SERVICES }}"
      as: "service"
    command: "curl -s -o /dev/null -w '%{http_code}' {{ service.url }}"
    continue_on_error: true

  - name: aggregate_results
    depends_on: [check_services]
    script:
      source: |
        const services = {{ SERVICES }};
        const results = services.map((svc, i) => {
          const output = context.get(`tasks.check_services[${i}].outputs.stdout`);
          return {
            name: svc.name,
            status: output === '200' ? 'healthy' : 'unhealthy',
            code: output
          };
        });

        const unhealthy = results.filter(r => r.status === 'unhealthy');
        context.set("results", results);
        context.set("unhealthy_count", unhealthy.length);

  - name: alert_if_unhealthy
    depends_on: [aggregate_results]
    when: "{{ tasks.aggregate_results.outputs.unhealthy_count }} > 0"
    command: |
      curl -X POST {{ env.PAGERDUTY_WEBHOOK }} \
        -H "Content-Type: application/json" \
        -d '{"message": "{{ tasks.aggregate_results.outputs.unhealthy_count }} services unhealthy"}'
```

### 10. File Processing Pipeline

```yaml
name: File Processing
variables:
  INPUT_DIR: "./input"
  OUTPUT_DIR: "./output"
  ARCHIVE_DIR: "./archive"

tasks:
  - name: list_files
    command: "ls -1 {{ INPUT_DIR }}/*.csv"
    script:
      source: |
        const stdout = context.get("tasks.list_files.outputs.stdout");
        const files = stdout.trim().split('\n').filter(f => f);
        context.set("files", files);

  - name: process_files
    depends_on: [list_files]
    script:
      source: |
        import fs from 'turbo:fs';

        const files = context.get("tasks.list_files.outputs.files");
        const results = [];

        for (const file of files) {
          const content = fs.readFile(file);
          const lines = content.split('\n');
          const processed = lines.filter(l => l.trim()).length;

          const basename = file.split('/').pop();
          fs.writeFile(`{{ OUTPUT_DIR }}/${basename}`, content.toUpperCase());

          results.push({ file: basename, lines: processed });
        }

        context.set("processed", results);
        context.set("total_files", results.length);

  - name: archive_originals
    depends_on: [process_files]
    command: "mv {{ INPUT_DIR }}/*.csv {{ ARCHIVE_DIR }}/"

  - name: summary
    depends_on: [archive_originals]
    command: |
      echo "Processed {{ tasks.process_files.outputs.total_files }} files"
```
