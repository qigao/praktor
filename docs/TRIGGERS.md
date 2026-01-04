# Prakter Trigger System

## Overview

Triggers are event-driven actions that execute automatically after a task completes. They enable workflows to respond to success, failure, or completion events without adding complexity to the main task graph.

## Trigger Events

### `on_success`
Fires when a task completes successfully (exit code 0 or script returns without error).

```yaml
tasks:
  - name: deploy
    command: "./deploy.sh"
    triggers:
      on_success:
        - write_file:
            path: "./deploy-success.log"
            content: "Deployment completed at {{ tasks.deploy.outputs.stdout }}"
```

### `on_failure`
Fires when a task fails after all retry attempts are exhausted.

```yaml
tasks:
  - name: critical_task
    command: "./important-operation.sh"
    retries:
      count: 3
      delay: "10s"
    triggers:
      on_failure:
        - http_post:
            url: "{{ env.ALERT_WEBHOOK }}"
            body: '{"error": "{{ failed_task_error }}"}'
        - run_task:
            task_name: rollback
```

### `on_complete`
Fires regardless of success or failure. Useful for cleanup or logging.

```yaml
tasks:
  - name: process_data
    command: "./process.sh"
    triggers:
      on_complete:
        - write_file:
            path: "./process.log"
            content: "Task completed with status: {{ tasks.process_data.status }}"
```

## Trigger Actions

### 1. `http_post` - Send HTTP Webhook

Send an HTTP POST request to a webhook URL.

**Attributes:**
- `url` (required): The webhook URL
- `body` (optional): Request body (supports variable substitution)
- `headers` (optional): HTTP headers as key-value pairs

**Example:**
```yaml
triggers:
  on_success:
    - http_post:
        url: "https://hooks.slack.com/services/YOUR/WEBHOOK"
        body: |
          {
            "text": "Build {{ tasks.build.outputs.version }} completed",
            "channel": "#deployments"
          }
        headers:
          Content-Type: "application/json"
          Authorization: "Bearer {{ env.API_TOKEN }}"
```

**Platform Support:**
- Windows: Uses WinHTTP (built-in)
- Linux/macOS: Uses libcurl

### 2. `write_file` - Create File Artifact

Write content to a file, useful for logs, reports, or artifacts.

**Attributes:**
- `path` (required): File path (relative or absolute)
- `content` (required): Content to write (supports variable substitution)
- `mode` (optional): `overwrite` (default) or `append`

**Example:**
```yaml
triggers:
  on_complete:
    - write_file:
        path: "./logs/build-{{ VERSION }}.log"
        content: |
          Build Log
          =========
          Status: {{ tasks.build.status }}
          Output: {{ tasks.build.outputs.stdout }}
          Timestamp: {{ tasks.build.outputs.timestamp }}
        mode: overwrite
```

**Append Mode Example:**
```yaml
triggers:
  on_success:
    - write_file:
        path: "./deploy-history.log"
        content: "{{ VERSION }} deployed at {{ tasks.deploy.outputs.stdout }}\n"
        mode: append
```

### 3. `run_task` - Execute Recovery Task

Trigger another task for complex recovery or notification logic.

**Attributes:**
- `task_name` (required): Name of the task to execute

**Example:**
```yaml
tasks:
  - name: deploy
    command: "./deploy.sh"
    triggers:
      on_failure: [rollback, notify_team]  # Shorthand syntax

  - name: rollback
    command: "./rollback.sh --reason 'Deploy failed'"

  - name: notify_team
    command: |
      curl -X POST {{ env.SLACK_WEBHOOK }} \
        -d '{"text": "Deploy failed: {{ failed_task_error }}"}'
```

**Shorthand Syntax:**
For `run_task`, you can use a string directly:
```yaml
triggers:
  on_failure: [rollback, alert]  # Equivalent to run_task actions
```

## Failure Context Variables

When a task fails, the following variables are available in `on_failure` triggers:

| Variable | Description |
|----------|-------------|
| `failed_task_name` | Name of the failed task |
| `failed_task_type` | Type of task (command, script, uses) |
| `failed_task_exit_code` | Exit code (for command tasks) |
| `failed_task_stdout` | Standard output captured |
| `failed_task_stderr` | Standard error captured |
| `failed_task_error` | Error message |

**Example:**
```yaml
triggers:
  on_failure:
    - write_file:
        path: "./failure-report.txt"
        content: |
          Task: {{ failed_task_name }}
          Type: {{ failed_task_type }}
          Exit Code: {{ failed_task_exit_code }}
          Error: {{ failed_task_error }}

          STDOUT:
          {{ failed_task_stdout }}

          STDERR:
          {{ failed_task_stderr }}
```

## Multiple Triggers

You can define multiple triggers for the same event:

```yaml
tasks:
  - name: deploy
    command: "./deploy.sh"
    triggers:
      on_success:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: '{"text": "Deploy succeeded"}'
        - write_file:
            path: "./deploy.log"
            content: "Success"
        - run_task:
            task_name: post_deploy_tests
      on_failure:
        - http_post:
            url: "{{ env.PAGERDUTY_WEBHOOK }}"
            body: '{"severity": "critical"}'
        - run_task:
            task_name: rollback
        - run_task:
            task_name: notify_oncall
```

## Best Practices

### 1. Use Triggers for Side Effects
Triggers are perfect for actions that don't affect the main workflow logic:
- Sending notifications
- Writing logs
- Creating artifacts
- Triggering external systems

### 2. Keep Triggers Lightweight
Triggers execute synchronously after task completion. Keep them fast:
```yaml
# Good: Quick notification
triggers:
  on_success:
    - http_post:
        url: "{{ env.WEBHOOK }}"
        body: '{"status": "done"}'

# Avoid: Long-running operations
# Use run_task instead for complex logic
```

### 3. Handle Trigger Failures Gracefully
Trigger failures don't fail the task. They're logged as warnings:
```yaml
# If the webhook fails, the task still succeeds
triggers:
  on_success:
    - http_post:
        url: "{{ env.OPTIONAL_WEBHOOK }}"
        body: '{"status": "success"}'
```

### 4. Use Environment Variables for URLs
Keep webhook URLs and sensitive data in environment variables:
```yaml
env:
  SLACK_WEBHOOK: "https://hooks.slack.com/..."
  PAGERDUTY_KEY: "your-key-here"

tasks:
  - name: deploy
    triggers:
      on_failure:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            headers:
              Authorization: "Bearer {{ env.PAGERDUTY_KEY }}"
```

### 5. Combine with Retries
Use triggers with retry policies for robust error handling:
```yaml
tasks:
  - name: flaky_operation
    command: "./flaky.sh"
    retries:
      count: 3
      delay: "5s"
    triggers:
      on_failure:  # Only fires after all retries fail
        - http_post:
            url: "{{ env.ALERT_WEBHOOK }}"
            body: '{"message": "Failed after 3 retries"}'
```

## Complete Example

```yaml
variables:
  APP_NAME: "my-service"
  VERSION: "2.1.0"

env:
  SLACK_WEBHOOK: "https://hooks.slack.com/services/YOUR/WEBHOOK"
  DEPLOY_ENV: "production"

tasks:
  - name: build
    command: "npm run build"
    triggers:
      on_success:
        - write_file:
            path: "./artifacts/build-{{ VERSION }}.log"
            content: "Build completed: {{ tasks.build.outputs.stdout }}"

  - name: test
    depends_on: [build]
    command: "npm test"
    retries:
      count: 2
      delay: "5s"
    triggers:
      on_failure:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: '{"text": "❌ Tests failed for {{ APP_NAME }}"}'
        - write_file:
            path: "./test-failures.log"
            content: |
              Test Failure Report
              ===================
              Exit Code: {{ failed_task_exit_code }}
              Error: {{ failed_task_error }}

              Output:
              {{ failed_task_stderr }}
            mode: append

  - name: deploy
    depends_on: [test]
    command: "./deploy.sh --env {{ env.DEPLOY_ENV }}"
    triggers:
      on_success:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: |
              {
                "text": "✅ {{ APP_NAME }} v{{ VERSION }} deployed to {{ env.DEPLOY_ENV }}",
                "attachments": [{
                  "color": "good",
                  "fields": [
                    {"title": "Version", "value": "{{ VERSION }}"},
                    {"title": "Environment", "value": "{{ env.DEPLOY_ENV }}"}
                  ]
                }]
              }
        - write_file:
            path: "./deploy-history.log"
            content: "{{ VERSION }} deployed at {{ tasks.deploy.outputs.stdout }}\n"
            mode: append
      on_failure:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: '{"text": "🚨 Deployment failed: {{ failed_task_error }}"}'
        - run_task:
            task_name: rollback
        - run_task:
            task_name: notify_oncall
      on_complete:
        - write_file:
            path: "./deploy-status.json"
            content: |
              {
                "app": "{{ APP_NAME }}",
                "version": "{{ VERSION }}",
                "status": "{{ tasks.deploy.status }}",
                "environment": "{{ env.DEPLOY_ENV }}"
              }

  - name: rollback
    command: "./rollback.sh --version {{ VERSION }}"
    triggers:
      on_complete:
        - http_post:
            url: "{{ env.SLACK_WEBHOOK }}"
            body: '{"text": "🔄 Rollback completed"}'

  - name: notify_oncall
    command: |
      curl -X POST {{ env.PAGERDUTY_WEBHOOK }} \
        -H "Content-Type: application/json" \
        -d '{
          "event_action": "trigger",
          "payload": {
            "summary": "Deployment failed for {{ APP_NAME }}",
            "severity": "critical",
            "source": "prakter-workflow"
          }
        }'
```

## Troubleshooting

### Triggers Not Executing
1. Check that the task completed (triggers don't fire for skipped tasks)
2. Verify trigger syntax in YAML
3. Check logs for trigger execution warnings

### HTTP POST Failures
1. Verify the URL is accessible
2. Check network connectivity
3. Validate headers and body format
4. Test the webhook URL manually with curl

### File Write Failures
1. Check file path permissions
2. Verify parent directories exist
3. Check disk space
4. Validate file path syntax

### Run Task Not Found
1. Ensure the task name exists in the workflow
2. Check for typos in task_name
3. Verify the task is defined (not in a separate file)

## Implementation Notes

- Triggers execute synchronously after task completion
- Trigger failures are logged as warnings but don't fail the task
- Triggers have access to the full workflow context
- HTTP requests timeout after 30 seconds
- File writes create parent directories if needed (on most systems)
