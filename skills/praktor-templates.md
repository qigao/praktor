# Praktor Templates Skill

Create reusable workflow templates and JavaScript modules for Praktor.

## Project Structure

```
project/
├── workflows/
│   ├── main.yml                 # Main workflow
│   ├── templates/               # Reusable workflow templates
│   │   ├── docker-build.yml
│   │   ├── deploy.yml
│   │   └── notify.yml
│   └── lib/                     # JavaScript modules
│       ├── validators.js
│       ├── transformers.js
│       └── api-client.js
├── config/
│   ├── base.json
│   └── production.json
└── .env
```

## Reusable Workflow Templates

### Template: docker-build.yml

```yaml
# workflows/templates/docker-build.yml
# Reusable Docker build template
# Expected vars: IMAGE_NAME, TAG, REGISTRY (optional)

name: Docker Build Template

variables:
  REGISTRY: "docker.io"
  DOCKERFILE: "Dockerfile"
  BUILD_CONTEXT: "."

tasks:
  - name: validate_inputs
    script:
      source: |
        const imageName = context.get("IMAGE_NAME");
        const tag = context.get("TAG");

        if (!imageName) fail("IMAGE_NAME is required");
        if (!tag) fail("TAG is required");

        context.set("validated", true);

  - name: build
    depends_on: [validate_inputs]
    command: |
      docker build \
        -f {{ DOCKERFILE }} \
        -t {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ TAG }} \
        -t {{ REGISTRY }}/{{ IMAGE_NAME }}:latest \
        {{ BUILD_CONTEXT }}
    timeout: "20m"

  - name: push
    depends_on: [build]
    when: "{{ env.PUSH_IMAGE }} == 'true'"
    command: |
      docker push {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ TAG }}
      docker push {{ REGISTRY }}/{{ IMAGE_NAME }}:latest

  - name: output_info
    depends_on: [build]
    script:
      source: |
        const registry = context.get("REGISTRY");
        const imageName = context.get("IMAGE_NAME");
        const tag = context.get("TAG");

        context.set("full_image", `${registry}/${imageName}:${tag}`);
        context.set("latest_image", `${registry}/${imageName}:latest`);
```

### Template: deploy.yml

```yaml
# workflows/templates/deploy.yml
# Reusable deployment template
# Expected vars: SERVICE_NAME, IMAGE, ENVIRONMENT

name: Deploy Template

variables:
  REPLICAS: "2"
  HEALTH_CHECK_PATH: "/health"
  HEALTH_CHECK_TIMEOUT: "60"

tasks:
  - name: validate
    script:
      source: |
        const required = ['SERVICE_NAME', 'IMAGE', 'ENVIRONMENT'];
        const missing = required.filter(k => !context.get(k));

        if (missing.length > 0) {
          fail(`Missing required variables: ${missing.join(', ')}`);
        }

        const validEnvs = ['development', 'staging', 'production'];
        const env = context.get("ENVIRONMENT");
        if (!validEnvs.includes(env)) {
          fail(`Invalid ENVIRONMENT: ${env}. Must be one of: ${validEnvs.join(', ')}`);
        }

  - name: pre_deploy_backup
    depends_on: [validate]
    when: "{{ ENVIRONMENT }} == 'production'"
    command: "./scripts/backup.sh {{ SERVICE_NAME }}"
    timeout: "10m"

  - name: deploy
    depends_on: [validate, pre_deploy_backup]
    command: |
      kubectl set image deployment/{{ SERVICE_NAME }} \
        {{ SERVICE_NAME }}={{ IMAGE }} \
        -n {{ ENVIRONMENT }}

      kubectl rollout status deployment/{{ SERVICE_NAME }} \
        -n {{ ENVIRONMENT }} \
        --timeout=300s

  - name: health_check
    depends_on: [deploy]
    script:
      source: |
        import http from 'turbo:http';
        import timers from 'turbo:timers';

        const serviceName = context.get("SERVICE_NAME");
        const environment = context.get("ENVIRONMENT");
        const healthPath = context.get("HEALTH_CHECK_PATH");
        const timeout = parseInt(context.get("HEALTH_CHECK_TIMEOUT"));

        const serviceUrl = `https://${serviceName}.${environment}.example.com${healthPath}`;
        const startTime = Date.now();

        while (Date.now() - startTime < timeout * 1000) {
          try {
            const response = http.get(serviceUrl);
            if (response.status === 200) {
              print(`Health check passed for ${serviceName}`);
              context.set("health_status", "healthy");
              context.set("health_check_time_ms", Date.now() - startTime);
              return;
            }
          } catch (e) {
            // Retry
          }
          timers.sleep(2000);
        }

        fail(`Health check failed after ${timeout}s`);

  - name: rollback
    command: |
      kubectl rollout undo deployment/{{ SERVICE_NAME }} -n {{ ENVIRONMENT }}
    # This task is triggered on failure, not run normally
```

### Template: notify.yml

```yaml
# workflows/templates/notify.yml
# Reusable notification template
# Expected vars: CHANNEL, MESSAGE, STATUS (success/failure/info)

name: Notify Template

tasks:
  - name: build_payload
    script:
      source: |
        import os from 'turbo:os';

        const channel = context.get("CHANNEL") || "general";
        const message = context.get("MESSAGE");
        const status = context.get("STATUS") || "info";

        const colors = {
          success: "#36a64f",
          failure: "#dc3545",
          info: "#17a2b8",
          warning: "#ffc107"
        };

        const icons = {
          success: ":white_check_mark:",
          failure: ":x:",
          info: ":information_source:",
          warning: ":warning:"
        };

        const payload = {
          channel,
          attachments: [{
            color: colors[status] || colors.info,
            blocks: [
              {
                type: "section",
                text: {
                  type: "mrkdwn",
                  text: `${icons[status]} ${message}`
                }
              },
              {
                type: "context",
                elements: [
                  {
                    type: "mrkdwn",
                    text: `*Host:* ${os.hostname()} | *Time:* ${new Date().toISOString()}`
                  }
                ]
              }
            ]
          }]
        };

        context.set("slack_payload", JSON.stringify(payload));

  - name: send_slack
    depends_on: [build_payload]
    when: "{{ env.SLACK_WEBHOOK }}"
    command: |
      curl -X POST {{ env.SLACK_WEBHOOK }} \
        -H "Content-Type: application/json" \
        -d '{{ tasks.build_payload.outputs.slack_payload }}'

  - name: send_teams
    depends_on: [build_payload]
    when: "{{ env.TEAMS_WEBHOOK }}"
    script:
      source: |
        import http from 'turbo:http';

        const message = context.get("MESSAGE");
        const status = context.get("STATUS") || "info";

        const colors = {
          success: "00FF00",
          failure: "FF0000",
          info: "0078D7"
        };

        const teamsPayload = {
          "@type": "MessageCard",
          themeColor: colors[status],
          summary: message,
          sections: [{
            activityTitle: message,
            facts: [
              { name: "Status", value: status.toUpperCase() },
              { name: "Time", value: new Date().toISOString() }
            ]
          }]
        };

        const webhook = context.get("env.TEAMS_WEBHOOK");
        http.post(webhook, teamsPayload);
```

## JavaScript Module Templates

### Module: validators.js

```javascript
// workflows/lib/validators.js
// Reusable validation functions

export function validateEmail(email) {
  if (!email) return false;
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
}

export function validatePhone(phone) {
  if (!phone) return false;
  return /^\+?[\d\s\-()]{10,}$/.test(phone.replace(/\s/g, ''));
}

export function validateUrl(url) {
  if (!url) return false;
  try {
    new URL(url);
    return true;
  } catch {
    return false;
  }
}

export function validateRequired(value) {
  return value !== null && value !== undefined && value !== '';
}

export function validateMinLength(value, min) {
  return value && value.length >= min;
}

export function validateMaxLength(value, max) {
  return !value || value.length <= max;
}

export function validatePattern(value, pattern) {
  if (!value) return false;
  return new RegExp(pattern).test(value);
}

export function validateRange(value, min, max) {
  const num = parseFloat(value);
  return !isNaN(num) && num >= min && num <= max;
}

// Validate an object against a schema
export function validateObject(obj, schema) {
  const errors = [];

  for (const [field, rules] of Object.entries(schema)) {
    const value = obj[field];

    if (rules.required && !validateRequired(value)) {
      errors.push({ field, error: 'is required' });
      continue;
    }

    if (value === null || value === undefined) continue;

    if (rules.type === 'email' && !validateEmail(value)) {
      errors.push({ field, error: 'must be a valid email' });
    }
    if (rules.type === 'phone' && !validatePhone(value)) {
      errors.push({ field, error: 'must be a valid phone number' });
    }
    if (rules.type === 'url' && !validateUrl(value)) {
      errors.push({ field, error: 'must be a valid URL' });
    }
    if (rules.minLength && !validateMinLength(value, rules.minLength)) {
      errors.push({ field, error: `must be at least ${rules.minLength} characters` });
    }
    if (rules.maxLength && !validateMaxLength(value, rules.maxLength)) {
      errors.push({ field, error: `must be at most ${rules.maxLength} characters` });
    }
    if (rules.pattern && !validatePattern(value, rules.pattern)) {
      errors.push({ field, error: `must match pattern ${rules.pattern}` });
    }
    if (rules.min !== undefined && rules.max !== undefined) {
      if (!validateRange(value, rules.min, rules.max)) {
        errors.push({ field, error: `must be between ${rules.min} and ${rules.max}` });
      }
    }
  }

  return {
    valid: errors.length === 0,
    errors
  };
}

export default {
  validateEmail,
  validatePhone,
  validateUrl,
  validateRequired,
  validateMinLength,
  validateMaxLength,
  validatePattern,
  validateRange,
  validateObject
};
```

### Module: transformers.js

```javascript
// workflows/lib/transformers.js
// Data transformation utilities

export function pick(obj, keys) {
  return keys.reduce((acc, key) => {
    if (obj.hasOwnProperty(key)) {
      acc[key] = obj[key];
    }
    return acc;
  }, {});
}

export function omit(obj, keys) {
  const result = { ...obj };
  keys.forEach(key => delete result[key]);
  return result;
}

export function rename(obj, mapping) {
  const result = { ...obj };
  for (const [oldKey, newKey] of Object.entries(mapping)) {
    if (result.hasOwnProperty(oldKey)) {
      result[newKey] = result[oldKey];
      delete result[oldKey];
    }
  }
  return result;
}

export function flatten(obj, prefix = '') {
  return Object.keys(obj).reduce((acc, key) => {
    const pre = prefix ? `${prefix}.` : '';
    if (typeof obj[key] === 'object' && obj[key] !== null && !Array.isArray(obj[key])) {
      Object.assign(acc, flatten(obj[key], pre + key));
    } else {
      acc[pre + key] = obj[key];
    }
    return acc;
  }, {});
}

export function unflatten(obj) {
  const result = {};
  for (const [key, value] of Object.entries(obj)) {
    const keys = key.split('.');
    let current = result;
    for (let i = 0; i < keys.length - 1; i++) {
      current[keys[i]] = current[keys[i]] || {};
      current = current[keys[i]];
    }
    current[keys[keys.length - 1]] = value;
  }
  return result;
}

export function groupBy(arr, key) {
  return arr.reduce((acc, item) => {
    const group = typeof key === 'function' ? key(item) : item[key];
    (acc[group] = acc[group] || []).push(item);
    return acc;
  }, {});
}

export function sortBy(arr, key, order = 'asc') {
  return [...arr].sort((a, b) => {
    const aVal = typeof key === 'function' ? key(a) : a[key];
    const bVal = typeof key === 'function' ? key(b) : b[key];
    const cmp = aVal < bVal ? -1 : aVal > bVal ? 1 : 0;
    return order === 'desc' ? -cmp : cmp;
  });
}

export function unique(arr, key) {
  if (!key) return [...new Set(arr)];
  const seen = new Set();
  return arr.filter(item => {
    const val = typeof key === 'function' ? key(item) : item[key];
    if (seen.has(val)) return false;
    seen.add(val);
    return true;
  });
}

export function chunk(arr, size) {
  const chunks = [];
  for (let i = 0; i < arr.length; i += size) {
    chunks.push(arr.slice(i, i + size));
  }
  return chunks;
}

export function mapValues(obj, fn) {
  return Object.fromEntries(
    Object.entries(obj).map(([k, v]) => [k, fn(v, k)])
  );
}

export function filterValues(obj, fn) {
  return Object.fromEntries(
    Object.entries(obj).filter(([k, v]) => fn(v, k))
  );
}

export default {
  pick, omit, rename, flatten, unflatten,
  groupBy, sortBy, unique, chunk,
  mapValues, filterValues
};
```

### Module: api-client.js

```javascript
// workflows/lib/api-client.js
// Reusable API client with retry and error handling

import http from 'turbo:http';
import timers from 'turbo:timers';

export class ApiClient {
  constructor(baseUrl, options = {}) {
    this.baseUrl = baseUrl.replace(/\/$/, '');
    this.headers = options.headers || {};
    this.timeout = options.timeout || 30000;
    this.retries = options.retries || 3;
    this.retryDelay = options.retryDelay || 1000;
  }

  setHeader(key, value) {
    this.headers[key] = value;
    return this;
  }

  setAuth(token, type = 'Bearer') {
    this.headers['Authorization'] = `${type} ${token}`;
    return this;
  }

  async request(method, path, options = {}) {
    const url = `${this.baseUrl}${path}`;
    const headers = { ...this.headers, ...options.headers };

    let lastError;

    for (let attempt = 1; attempt <= this.retries; attempt++) {
      try {
        const response = http.request(method, url, {
          body: options.body ? JSON.stringify(options.body) : undefined,
          headers: {
            'Content-Type': 'application/json',
            ...headers
          }
        });

        const result = {
          status: response.status,
          headers: response.headers,
          body: response.body,
          data: null
        };

        // Try to parse JSON
        try {
          result.data = JSON.parse(response.body);
        } catch {
          result.data = response.body;
        }

        // Check for errors
        if (response.status >= 400) {
          const error = new Error(`HTTP ${response.status}: ${response.body}`);
          error.status = response.status;
          error.response = result;

          // Don't retry client errors (4xx)
          if (response.status < 500) {
            throw error;
          }

          lastError = error;
        } else {
          return result;
        }
      } catch (e) {
        lastError = e;
      }

      // Wait before retry
      if (attempt < this.retries) {
        timers.sleep(this.retryDelay * attempt);
      }
    }

    throw lastError;
  }

  get(path, options) {
    return this.request('GET', path, options);
  }

  post(path, body, options = {}) {
    return this.request('POST', path, { ...options, body });
  }

  put(path, body, options = {}) {
    return this.request('PUT', path, { ...options, body });
  }

  patch(path, body, options = {}) {
    return this.request('PATCH', path, { ...options, body });
  }

  delete(path, options) {
    return this.request('DELETE', path, options);
  }
}

export function createClient(baseUrl, options) {
  return new ApiClient(baseUrl, options);
}

export default { ApiClient, createClient };
```

### Module: logger.js

```javascript
// workflows/lib/logger.js
// Structured logging utility

const LOG_LEVELS = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3
};

let currentLevel = LOG_LEVELS.info;

export function setLevel(level) {
  currentLevel = LOG_LEVELS[level] ?? LOG_LEVELS.info;
}

function formatMessage(level, message, data) {
  const timestamp = new Date().toISOString();
  const entry = {
    timestamp,
    level: level.toUpperCase(),
    message
  };

  if (data) {
    entry.data = data;
  }

  return JSON.stringify(entry);
}

export function debug(message, data) {
  if (currentLevel <= LOG_LEVELS.debug) {
    print(formatMessage('debug', message, data));
  }
}

export function info(message, data) {
  if (currentLevel <= LOG_LEVELS.info) {
    print(formatMessage('info', message, data));
  }
}

export function warn(message, data) {
  if (currentLevel <= LOG_LEVELS.warn) {
    print(formatMessage('warn', message, data));
  }
}

export function error(message, data) {
  if (currentLevel <= LOG_LEVELS.error) {
    print(formatMessage('error', message, data));
  }
}

export function time(label) {
  return {
    label,
    start: Date.now(),
    end() {
      const duration = Date.now() - this.start;
      info(`${this.label} completed`, { duration_ms: duration });
      return duration;
    }
  };
}

export default { setLevel, debug, info, warn, error, time };
```

## Complete Example: CI/CD Pipeline

### Main Workflow

```yaml
# workflows/main.yml
name: CI/CD Pipeline

variables:
  APP_NAME: "myapp"
  REGISTRY: "ghcr.io/myorg"

env:
  NODE_ENV: "production"

tasks:
  # Get version from git
  - name: version
    command: "git describe --tags --always"
    script:
      source: |
        const stdout = context.get("tasks.version.outputs.stdout");
        const version = stdout.trim();
        context.set("version", version);
        context.set("tag", `v${version}`);

  # Run tests
  - name: test
    command: "npm test"
    retries:
      count: 2
      delay: "5s"

  # Build Docker image using template
  - name: build
    depends_on: [version, test]
    uses: ./templates/docker-build.yml
    vars:
      IMAGE_NAME: "{{ APP_NAME }}"
      TAG: "{{ tasks.version.outputs.tag }}"
      REGISTRY: "{{ REGISTRY }}"
    env:
      PUSH_IMAGE: "true"

  # Deploy to staging
  - name: deploy_staging
    depends_on: [build]
    uses: ./templates/deploy.yml
    vars:
      SERVICE_NAME: "{{ APP_NAME }}"
      IMAGE: "{{ tasks.build.outputs.full_image }}"
      ENVIRONMENT: "staging"

  # Notify staging success
  - name: notify_staging
    depends_on: [deploy_staging]
    uses: ./templates/notify.yml
    vars:
      CHANNEL: "deployments"
      MESSAGE: "{{ APP_NAME }} deployed to staging"
      STATUS: "success"

  # Deploy to production (manual approval via env var)
  - name: deploy_production
    depends_on: [deploy_staging]
    when: "{{ env.DEPLOY_PROD }} == 'true'"
    uses: ./templates/deploy.yml
    vars:
      SERVICE_NAME: "{{ APP_NAME }}"
      IMAGE: "{{ tasks.build.outputs.full_image }}"
      ENVIRONMENT: "production"
    triggers:
      on_failure: [rollback_production]
      on_success: [notify_production]

  # Rollback production
  - name: rollback_production
    uses: ./templates/deploy.yml
    vars:
      SERVICE_NAME: "{{ APP_NAME }}"
      ENVIRONMENT: "production"
      ROLLBACK: "true"

  # Notify production success
  - name: notify_production
    uses: ./templates/notify.yml
    vars:
      CHANNEL: "deployments"
      MESSAGE: "{{ APP_NAME }} deployed to production :rocket:"
      STATUS: "success"
```

### Data Processing Workflow

```yaml
# workflows/data-pipeline.yml
name: Data Processing Pipeline

variables:
  INPUT_DIR: "./data/input"
  OUTPUT_DIR: "./data/output"

tasks:
  - name: fetch_data
    command: "curl -s {{ env.DATA_API }}/export"
    output_format: json

  - name: validate_data
    depends_on: [fetch_data]
    script:
      source: |
        import { validateObject } from './lib/validators.js';
        import logger from './lib/logger.js';

        const data = context.get("tasks.fetch_data.outputs.data");
        const timer = logger.time("Validation");

        const schema = {
          id: { required: true },
          email: { required: true, type: 'email' },
          name: { required: true, minLength: 2 },
          age: { min: 0, max: 150 }
        };

        const valid = [];
        const invalid = [];

        for (const record of data) {
          const result = validateObject(record, schema);
          if (result.valid) {
            valid.push(record);
          } else {
            invalid.push({ record, errors: result.errors });
          }
        }

        timer.end();

        logger.info("Validation complete", {
          total: data.length,
          valid: valid.length,
          invalid: invalid.length
        });

        context.set("valid_records", valid);
        context.set("invalid_records", invalid);

  - name: transform_data
    depends_on: [validate_data]
    script:
      source: |
        import { pick, groupBy, sortBy } from './lib/transformers.js';
        import fs from 'turbo:fs';
        import logger from './lib/logger.js';

        const records = context.get("tasks.validate_data.outputs.valid_records");
        const timer = logger.time("Transformation");

        // Transform records
        const transformed = records.map(r => ({
          ...pick(r, ['id', 'email', 'name']),
          processed_at: new Date().toISOString()
        }));

        // Group by domain
        const byDomain = groupBy(transformed, r => r.email.split('@')[1]);

        // Sort each group
        for (const domain in byDomain) {
          byDomain[domain] = sortBy(byDomain[domain], 'name');
        }

        timer.end();

        // Save outputs
        fs.mkdir("{{ OUTPUT_DIR }}");
        fs.writeJson("{{ OUTPUT_DIR }}/transformed.json", transformed);
        fs.writeJson("{{ OUTPUT_DIR }}/by-domain.json", byDomain);

        context.set("record_count", transformed.length);
        context.set("domain_count", Object.keys(byDomain).length);

  - name: sync_to_api
    depends_on: [transform_data]
    script:
      source: |
        import { createClient } from './lib/api-client.js';
        import fs from 'turbo:fs';
        import os from 'turbo:os';
        import logger from './lib/logger.js';

        const client = createClient(os.getenv("TARGET_API"), {
          retries: 3,
          retryDelay: 2000
        }).setAuth(os.getenv("API_TOKEN"));

        const data = fs.readJson("{{ OUTPUT_DIR }}/transformed.json");
        const timer = logger.time("API Sync");

        let succeeded = 0;
        let failed = 0;

        for (const record of data) {
          try {
            await client.post('/records', record);
            succeeded++;
          } catch (e) {
            logger.error("Failed to sync record", { id: record.id, error: e.message });
            failed++;
          }
        }

        timer.end();

        logger.info("Sync complete", { succeeded, failed });
        context.set("sync_succeeded", succeeded);
        context.set("sync_failed", failed);

  - name: generate_report
    depends_on: [sync_to_api]
    script:
      source: |
        import fs from 'turbo:fs';

        const report = {
          timestamp: new Date().toISOString(),
          input: {
            total: context.get("tasks.fetch_data.outputs.data").length
          },
          validation: {
            valid: context.get("tasks.validate_data.outputs.valid_records").length,
            invalid: context.get("tasks.validate_data.outputs.invalid_records").length
          },
          transformation: {
            records: context.get("tasks.transform_data.outputs.record_count"),
            domains: context.get("tasks.transform_data.outputs.domain_count")
          },
          sync: {
            succeeded: context.get("tasks.sync_to_api.outputs.sync_succeeded"),
            failed: context.get("tasks.sync_to_api.outputs.sync_failed")
          }
        };

        fs.writeJson("{{ OUTPUT_DIR }}/report.json", report);
        context.set("report", report);
    triggers:
      on_complete: [notify_completion]

  - name: notify_completion
    script:
      source: |
        import { post } from 'turbo:http';
        const msg = "Data pipeline complete: {{ tasks.generate_report.outputs.report.sync.succeeded }} synced";
        post(process.env.SLACK, JSON.stringify({text: msg}));
```

## Best Practices

### 1. Template Design

- Keep templates focused on a single responsibility
- Use clear variable naming with documentation
- Provide sensible defaults
- Validate required inputs early

### 2. Module Organization

```javascript
// Good: Single responsibility
// lib/validators.js - validation only
// lib/transformers.js - data transformation only
// lib/api-client.js - HTTP client only

// Bad: Kitchen sink module
// lib/utils.js - everything mixed together
```

### 3. Error Handling in Templates

```yaml
# Always include validation task
- name: validate_inputs
  script:
    source: |
      const required = ['VAR1', 'VAR2'];
      const missing = required.filter(v => !context.get(v));
      if (missing.length) fail(`Missing: ${missing.join(', ')}`);

# Use triggers for cleanup/rollback
- name: risky_operation
  triggers:
    on_failure: [cleanup_task]
```

### 4. Module Versioning

```javascript
// Include version in module
export const VERSION = '1.0.0';

// Check compatibility
import { VERSION } from './lib/validators.js';
if (VERSION < '1.0.0') {
  fail('validators.js version 1.0.0+ required');
}
```

### 5. Testing Templates

```yaml
# Create test workflow for each template
# workflows/tests/test-docker-build.yml
name: Test Docker Build Template

tasks:
  - name: test_build
    uses: ../templates/docker-build.yml
    vars:
      IMAGE_NAME: "test-image"
      TAG: "test"
    env:
      PUSH_IMAGE: "false"  # Don't push in tests

  - name: verify
    depends_on: [test_build]
    command: "docker images | grep test-image"
```
