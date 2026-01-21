# Praktor Script Skill

Generate JavaScript scripts for Praktor workflow `script` tasks.

## Context API

```javascript
// Read from workflow context
const value = context.get("path.to.value");
const taskOutput = context.get("tasks.my_task.outputs.result");
const envVar = context.get("env.API_KEY");

// Write to current task outputs
context.set("key", value);
context.set("result", { count: 10, items: [...] });

// Check if key exists
if (context.has("tasks.fetch.outputs.data")) {
  // ...
}
```

## ES6 Module Imports

Scripts with `import` statements automatically use ES6 module mode.

### File System (turbo:fs)

```javascript
import fs from 'turbo:fs';
import { readFile, writeFile, stat, readdir, mkdir, join, readJson, writeJson } from 'turbo:fs';

// Read file
const content = fs.readFile("./config.txt");
const binary = fs.readFile("./image.png", "binary");  // ArrayBuffer

// Write file
fs.writeFile("./output.txt", "content");
fs.writeFile("./data.bin", arrayBuffer);

// JSON operations
const config = fs.readJson("./config.json");
fs.writeJson("./output.json", { key: "value" });

// File info
const info = fs.stat("./file.txt");
// { size, mode, isFile, isDirectory, mtime }

// Directory operations
const files = fs.readdir("./src");
fs.mkdir("./output/subdir");

// Path utilities
const path = fs.join("dir", "subdir", "file.txt");
```

### Operating System (turbo:os)

```javascript
import os from 'turbo:os';
import { hostname, homedir, cwd, chdir, getenv, setenv, unsetenv, pid, ppid, uptime, loadavg, tmpdir } from 'turbo:os';

const host = os.hostname();
const home = os.homedir();
const tmp = os.tmpdir();
const currentDir = os.cwd();

os.chdir("/path/to/dir");

const apiKey = os.getenv("API_KEY");
os.setenv("MY_VAR", "value");
os.unsetenv("OLD_VAR");

const processId = os.pid();
const parentId = os.ppid();
const up = os.uptime();
const load = os.loadavg();
```

### DNS (turbo:dns)

```javascript
import dns from 'turbo:dns';
import { resolve, resolveAsync, setServers, getServers } from 'turbo:dns';

// Synchronous resolution
const ip = dns.resolve("example.com");
const ipv4 = dns.resolve("example.com", "ipv4");
const ipv6 = dns.resolve("example.com", "ipv6");

// Async resolution (returns Promise)
const ip = await dns.resolveAsync("example.com");

// DNS server configuration
dns.setServers(["8.8.8.8", "8.8.4.4"]);
const servers = dns.getServers();
```

### HTTP Client (turbo:http)

```javascript
import http from 'turbo:http';
import { get, post, request } from 'turbo:http';

// GET request
const response = http.get("https://api.example.com/data");
const withHeaders = http.get("https://api.example.com/data", {
  "Authorization": "Bearer token"
});

// POST request
const result = http.post("https://api.example.com/submit", {
  name: "value"
});
const withHeaders = http.post(url, body, {
  "Content-Type": "application/json"
});

// Generic request
const response = http.request("PUT", "https://api.example.com/resource", {
  body: JSON.stringify(data),
  headers: { "Content-Type": "application/json" }
});
```

### Timers (turbo:timers)

```javascript
import timers from 'turbo:timers';
import { setTimeout, setInterval, clearTimeout, clearInterval, sleep } from 'turbo:timers';

// Timeout
const id = timers.setTimeout(() => {
  console.log("Delayed");
}, 1000);
timers.clearTimeout(id);

// Interval
const intervalId = timers.setInterval(() => {
  console.log("Repeated");
}, 500);
timers.clearInterval(intervalId);

// Synchronous sleep
timers.sleep(1000);  // Sleep 1 second
```

### Utilities (turbo:utils)

```javascript
import utils from 'turbo:utils';
import { base64Encode, base64Decode } from 'turbo:utils';

// Base64 encoding
const encoded = utils.base64Encode("Hello World");
const decoded = utils.base64Decode(encoded);
const binary = utils.base64Decode(encoded, "binary");  // ArrayBuffer
```

### Networking (turbo:net)

```javascript
import { TcpClient, WebSocket } from 'turbo:net';

// TCP Client
const tcp = new TcpClient();
tcp.connect("localhost", 8080);
tcp.write("Hello");
const response = tcp.read();
tcp.close();

// WebSocket
const ws = new WebSocket("wss://echo.websocket.org");
ws.send("Hello");
ws.close();
```

### Signal Handling (turbo:signal)

```javascript
import signal from 'turbo:signal';
import { watch, SIGINT, SIGTERM } from 'turbo:signal';

signal.watch(signal.SIGINT, () => {
  console.log("Received SIGINT");
  // Cleanup...
});

signal.watch(signal.SIGTERM, () => {
  console.log("Received SIGTERM");
});
```

### Process Management (turbo:proc)

```javascript
import proc from 'turbo:proc';
import { spawn, kill } from 'turbo:proc';

// Spawn process
const child = proc.spawn("node", ["script.js"]);

// Kill process
proc.kill(pid);
proc.kill(pid, "SIGTERM");
```

## Global Object (Legacy)

For scripts without imports:

```javascript
// File system
turbo.fs.readFile("./file.txt");
turbo.fs.writeJson("./data.json", obj);
turbo.fs.stat("./path");

// OS
turbo.os.hostname();
turbo.os.getenv("VAR");
turbo.os.cwd();

// DNS
turbo.dns.resolve("example.com");

// HTTP
turbo.http.get("https://api.example.com");
turbo.http.post(url, body);

// Timers
turbo.timers.sleep(1000);

// Utils
turbo.utils.base64Encode(data);
```

## Built-in Functions

```javascript
// Console output (captured to task stdout)
print("message");
console.log("message");
echo("message");

// Fail the task
fail("Error message");
```

## Common Patterns

### Data Transformation

```javascript
import fs from 'turbo:fs';

const input = context.get("tasks.fetch.outputs.data");

const transformed = input
  .filter(item => item.active)
  .map(item => ({
    id: item.id,
    name: item.name.toUpperCase(),
    processed_at: new Date().toISOString()
  }));

fs.writeJson("./output/transformed.json", transformed);
context.set("count", transformed.length);
context.set("ids", transformed.map(x => x.id));
```

### API Integration

```javascript
import http from 'turbo:http';
import os from 'turbo:os';

const apiKey = os.getenv("API_KEY");
const baseUrl = context.get("API_BASE_URL");

const response = http.get(`${baseUrl}/users`, {
  "Authorization": `Bearer ${apiKey}`,
  "Accept": "application/json"
});

if (response.status === 200) {
  const users = JSON.parse(response.body);
  context.set("users", users);
  context.set("user_count", users.length);
} else {
  fail(`API request failed: ${response.status}`);
}
```

### File Processing

```javascript
import fs from 'turbo:fs';

const inputDir = context.get("INPUT_DIR") || "./input";
const outputDir = context.get("OUTPUT_DIR") || "./output";

fs.mkdir(outputDir);

const files = fs.readdir(inputDir);
const results = [];

for (const file of files) {
  if (file.name.endsWith(".json")) {
    const data = fs.readJson(fs.join(inputDir, file.name));
    const processed = processData(data);

    fs.writeJson(fs.join(outputDir, file.name), processed);
    results.push({ file: file.name, records: processed.length });
  }
}

context.set("processed_files", results);
context.set("total_files", results.length);

function processData(data) {
  return data.filter(x => x.valid);
}
```

### Configuration Merging

```javascript
import fs from 'turbo:fs';
import os from 'turbo:os';

const env = os.getenv("ENVIRONMENT") || "development";

// Load base config
const baseConfig = fs.readJson("./config/base.json");

// Load environment-specific config
let envConfig = {};
const envConfigPath = `./config/${env}.json`;
if (fs.stat(envConfigPath)?.isFile) {
  envConfig = fs.readJson(envConfigPath);
}

// Merge configs
const finalConfig = {
  ...baseConfig,
  ...envConfig,
  environment: env,
  generated_at: new Date().toISOString()
};

fs.writeJson("./config/final.json", finalConfig);
context.set("config", finalConfig);
```

### Error Handling

```javascript
import http from 'turbo:http';
import fs from 'turbo:fs';

try {
  const response = http.get(context.get("API_URL"));

  if (response.status !== 200) {
    throw new Error(`API returned ${response.status}: ${response.body}`);
  }

  const data = JSON.parse(response.body);
  fs.writeJson("./cache/api_response.json", data);

  context.set("success", true);
  context.set("data", data);

} catch (error) {
  console.log("Error:", error.message);
  context.set("success", false);
  context.set("error", error.message);

  // Optionally fail the task
  // fail(error.message);
}
```

### External Module Usage

```javascript
// Import from external file
import { validateEmail, formatPhone } from './lib/validators.js';
import dataProcessor from './lib/data-processor.js';

const input = context.get("tasks.fetch.outputs.data");

const validated = input.filter(record => {
  return validateEmail(record.email) && record.phone;
}).map(record => ({
  ...record,
  phone: formatPhone(record.phone)
}));

const result = dataProcessor.process(validated);
context.set("result", result);
```

## Typical Use Cases

### 1. JSON Data Transformation

```javascript
import fs from 'turbo:fs';

// Read input data
const rawData = context.get("tasks.fetch.outputs.data");

// Transform: filter, map, aggregate
const processed = rawData
  .filter(item => item.status === 'active' && item.value > 0)
  .map(item => ({
    id: item.id,
    name: item.name.trim().toUpperCase(),
    value: Math.round(item.value * 100) / 100,
    category: item.tags?.[0] || 'uncategorized',
    processed_at: new Date().toISOString()
  }))
  .sort((a, b) => b.value - a.value);

// Aggregate statistics
const stats = {
  total: processed.length,
  sum: processed.reduce((acc, x) => acc + x.value, 0),
  avg: processed.length > 0
    ? processed.reduce((acc, x) => acc + x.value, 0) / processed.length
    : 0,
  categories: [...new Set(processed.map(x => x.category))]
};

// Output
fs.writeJson("./output/processed.json", processed);
context.set("data", processed);
context.set("stats", stats);
```

### 2. REST API Integration

```javascript
import http from 'turbo:http';
import os from 'turbo:os';

const apiKey = os.getenv("API_KEY");
const baseUrl = context.get("API_BASE_URL") || "https://api.example.com";

// GET request with auth
const usersResponse = http.get(`${baseUrl}/users`, {
  "Authorization": `Bearer ${apiKey}`,
  "Accept": "application/json"
});

if (usersResponse.status !== 200) {
  fail(`Failed to fetch users: ${usersResponse.status}`);
}

const users = JSON.parse(usersResponse.body);

// POST request
const newUser = {
  name: context.get("USER_NAME"),
  email: context.get("USER_EMAIL")
};

const createResponse = http.post(`${baseUrl}/users`, newUser, {
  "Authorization": `Bearer ${apiKey}`,
  "Content-Type": "application/json"
});

if (createResponse.status === 201) {
  const created = JSON.parse(createResponse.body);
  context.set("created_user_id", created.id);
  print(`Created user: ${created.id}`);
} else {
  fail(`Failed to create user: ${createResponse.body}`);
}

context.set("user_count", users.length);
```

### 3. File System Operations

```javascript
import fs from 'turbo:fs';

const inputDir = context.get("INPUT_DIR") || "./data";
const outputDir = context.get("OUTPUT_DIR") || "./output";

// Ensure output directory exists
fs.mkdir(outputDir);

// List and process files
const files = fs.readdir(inputDir);
const results = [];

for (const file of files) {
  if (!file.name.endsWith('.json')) continue;

  const inputPath = fs.join(inputDir, file.name);
  const outputPath = fs.join(outputDir, `processed_${file.name}`);

  try {
    const data = fs.readJson(inputPath);

    // Process data
    const processed = {
      ...data,
      processed: true,
      timestamp: new Date().toISOString()
    };

    fs.writeJson(outputPath, processed);

    results.push({
      file: file.name,
      status: 'success',
      records: Array.isArray(data) ? data.length : 1
    });
  } catch (e) {
    results.push({
      file: file.name,
      status: 'error',
      error: e.message
    });
  }
}

context.set("results", results);
context.set("success_count", results.filter(r => r.status === 'success').length);
context.set("error_count", results.filter(r => r.status === 'error').length);
```

### 4. Configuration Management

```javascript
import fs from 'turbo:fs';
import os from 'turbo:os';

const env = os.getenv("ENVIRONMENT") || "development";

// Load base configuration
const baseConfig = fs.readJson("./config/base.json");

// Load environment-specific overrides
let envConfig = {};
const envConfigPath = `./config/${env}.json`;
try {
  envConfig = fs.readJson(envConfigPath);
} catch (e) {
  print(`No environment config found at ${envConfigPath}, using base only`);
}

// Load local overrides (gitignored)
let localConfig = {};
try {
  localConfig = fs.readJson("./config/local.json");
} catch (e) {
  // Local config is optional
}

// Deep merge configurations
function deepMerge(target, source) {
  for (const key in source) {
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      target[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
}

const finalConfig = deepMerge(
  deepMerge(deepMerge({}, baseConfig), envConfig),
  localConfig
);

// Add runtime metadata
finalConfig._meta = {
  environment: env,
  generated_at: new Date().toISOString(),
  hostname: os.hostname()
};

// Write final config
fs.writeJson("./config/runtime.json", finalConfig);

context.set("config", finalConfig);
context.set("environment", env);
```

### 5. Data Validation and Cleaning

```javascript
const rawData = context.get("tasks.fetch.outputs.data");

// Validation rules
const validators = {
  email: (v) => /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(v),
  phone: (v) => /^\+?[\d\s-]{10,}$/.test(v),
  required: (v) => v !== null && v !== undefined && v !== '',
  minLength: (min) => (v) => v && v.length >= min,
  maxLength: (max) => (v) => v && v.length <= max,
  numeric: (v) => !isNaN(parseFloat(v))
};

// Validate and clean records
const valid = [];
const invalid = [];

for (const record of rawData) {
  const errors = [];

  if (!validators.required(record.name)) {
    errors.push('name is required');
  }
  if (!validators.email(record.email)) {
    errors.push('invalid email format');
  }
  if (record.phone && !validators.phone(record.phone)) {
    errors.push('invalid phone format');
  }

  if (errors.length === 0) {
    // Clean and normalize
    valid.push({
      ...record,
      name: record.name.trim(),
      email: record.email.toLowerCase().trim(),
      phone: record.phone?.replace(/\s/g, '') || null
    });
  } else {
    invalid.push({
      record,
      errors
    });
  }
}

context.set("valid_records", valid);
context.set("invalid_records", invalid);
context.set("validation_summary", {
  total: rawData.length,
  valid: valid.length,
  invalid: invalid.length,
  success_rate: ((valid.length / rawData.length) * 100).toFixed(2) + '%'
});

if (invalid.length > 0) {
  print(`Warning: ${invalid.length} records failed validation`);
}
```

### 6. Report Generation

```javascript
import fs from 'turbo:fs';

const metrics = context.get("tasks.collect_metrics.outputs.data");
const date = new Date();
const dateStr = date.toISOString().split('T')[0];

// Calculate statistics
const stats = {
  requests: {
    total: metrics.total_requests,
    success: metrics.successful_requests,
    failed: metrics.failed_requests,
    success_rate: ((metrics.successful_requests / metrics.total_requests) * 100).toFixed(2)
  },
  latency: {
    avg: metrics.avg_latency_ms,
    p50: metrics.p50_latency_ms,
    p95: metrics.p95_latency_ms,
    p99: metrics.p99_latency_ms
  },
  errors: {
    total: metrics.error_count,
    by_type: metrics.errors_by_type
  }
};

// Generate report
const report = {
  title: `Daily Performance Report - ${dateStr}`,
  generated_at: date.toISOString(),
  period: {
    start: metrics.period_start,
    end: metrics.period_end
  },
  summary: {
    status: stats.requests.success_rate > 99 ? 'HEALTHY' :
            stats.requests.success_rate > 95 ? 'DEGRADED' : 'CRITICAL',
    highlights: [
      `${stats.requests.total.toLocaleString()} total requests`,
      `${stats.requests.success_rate}% success rate`,
      `${stats.latency.p95}ms p95 latency`
    ]
  },
  statistics: stats,
  recommendations: []
};

// Add recommendations based on data
if (stats.latency.p95 > 500) {
  report.recommendations.push('High p95 latency detected. Consider scaling or optimization.');
}
if (parseFloat(stats.requests.success_rate) < 99) {
  report.recommendations.push('Success rate below 99%. Investigate error patterns.');
}

// Save report
const reportDir = "./reports";
fs.mkdir(reportDir);
fs.writeJson(`${reportDir}/report-${dateStr}.json`, report);

context.set("report", report);
context.set("report_path", `${reportDir}/report-${dateStr}.json`);
context.set("status", report.summary.status);
```

### 7. Batch Processing with Progress

```javascript
import fs from 'turbo:fs';
import http from 'turbo:http';

const items = context.get("tasks.prepare.outputs.items");
const batchSize = parseInt(context.get("BATCH_SIZE") || "10");
const apiUrl = context.get("API_URL");

const results = {
  processed: 0,
  succeeded: 0,
  failed: 0,
  errors: []
};

// Process in batches
for (let i = 0; i < items.length; i += batchSize) {
  const batch = items.slice(i, i + batchSize);
  const batchNum = Math.floor(i / batchSize) + 1;
  const totalBatches = Math.ceil(items.length / batchSize);

  print(`Processing batch ${batchNum}/${totalBatches} (${batch.length} items)`);

  for (const item of batch) {
    try {
      const response = http.post(`${apiUrl}/process`, item, {
        "Content-Type": "application/json"
      });

      if (response.status >= 200 && response.status < 300) {
        results.succeeded++;
      } else {
        results.failed++;
        results.errors.push({
          item_id: item.id,
          status: response.status,
          error: response.body
        });
      }
    } catch (e) {
      results.failed++;
      results.errors.push({
        item_id: item.id,
        error: e.message
      });
    }
    results.processed++;
  }

  // Progress update
  const progress = ((results.processed / items.length) * 100).toFixed(1);
  print(`Progress: ${progress}% (${results.succeeded} succeeded, ${results.failed} failed)`);
}

// Final summary
print(`\nCompleted: ${results.succeeded}/${items.length} succeeded`);

if (results.errors.length > 0) {
  fs.writeJson("./errors.json", results.errors);
  print(`Errors saved to ./errors.json`);
}

context.set("results", results);
context.set("success_rate", ((results.succeeded / items.length) * 100).toFixed(2));
```

### 8. Environment Setup and Validation

```javascript
import os from 'turbo:os';
import fs from 'turbo:fs';

// Required environment variables
const required = [
  'API_KEY',
  'DATABASE_URL',
  'REDIS_URL'
];

// Optional with defaults
const optional = {
  'LOG_LEVEL': 'info',
  'MAX_RETRIES': '3',
  'TIMEOUT_MS': '30000'
};

const missing = [];
const config = {};

// Check required variables
for (const key of required) {
  const value = os.getenv(key);
  if (!value) {
    missing.push(key);
  } else {
    config[key] = value;
  }
}

// Apply optional with defaults
for (const [key, defaultValue] of Object.entries(optional)) {
  config[key] = os.getenv(key) || defaultValue;
}

// Validate specific formats
const validations = [];

if (config.DATABASE_URL && !config.DATABASE_URL.startsWith('postgres://')) {
  validations.push('DATABASE_URL must be a PostgreSQL connection string');
}

if (config.REDIS_URL && !config.REDIS_URL.startsWith('redis://')) {
  validations.push('REDIS_URL must be a Redis connection string');
}

// Report results
if (missing.length > 0) {
  fail(`Missing required environment variables: ${missing.join(', ')}`);
}

if (validations.length > 0) {
  fail(`Environment validation failed:\n${validations.join('\n')}`);
}

// Write validated config
fs.writeJson("./config/env.json", {
  ...config,
  API_KEY: '***REDACTED***',  // Don't log secrets
  validated_at: new Date().toISOString()
});

print(`Environment validated successfully`);
print(`Config: ${JSON.stringify({...config, API_KEY: '***'}, null, 2)}`);

context.set("config", config);
context.set("environment_valid", true);
```

### 9. CSV to JSON Conversion

```javascript
import fs from 'turbo:fs';

const inputFile = context.get("INPUT_FILE") || "./data/input.csv";
const outputFile = context.get("OUTPUT_FILE") || "./data/output.json";

// Read CSV
const content = fs.readFile(inputFile);
const lines = content.split('\n').filter(l => l.trim());

if (lines.length === 0) {
  fail("CSV file is empty");
}

// Parse header
const header = lines[0].split(',').map(h => h.trim().toLowerCase().replace(/\s+/g, '_'));

// Parse rows
const records = [];
const errors = [];

for (let i = 1; i < lines.length; i++) {
  const values = lines[i].split(',').map(v => v.trim());

  if (values.length !== header.length) {
    errors.push({ line: i + 1, error: 'Column count mismatch' });
    continue;
  }

  const record = {};
  for (let j = 0; j < header.length; j++) {
    let value = values[j];

    // Auto-convert types
    if (value === '') {
      value = null;
    } else if (value === 'true' || value === 'false') {
      value = value === 'true';
    } else if (!isNaN(value) && value !== '') {
      value = parseFloat(value);
    }

    record[header[j]] = value;
  }

  records.push(record);
}

// Write output
fs.writeJson(outputFile, records);

print(`Converted ${records.length} records`);
if (errors.length > 0) {
  print(`Skipped ${errors.length} invalid rows`);
}

context.set("records", records);
context.set("record_count", records.length);
context.set("error_count", errors.length);
context.set("columns", header);
```

### 10. Webhook Payload Builder

```javascript
import os from 'turbo:os';

const taskName = context.get("TASK_NAME");
const taskStatus = context.get("TASK_STATUS");
const taskOutput = context.get("TASK_OUTPUT");
const environment = os.getenv("ENVIRONMENT") || "unknown";

// Build Slack-compatible payload
const slackPayload = {
  text: `Task ${taskName} ${taskStatus}`,
  attachments: [{
    color: taskStatus === 'success' ? 'good' : 'danger',
    fields: [
      {
        title: "Task",
        value: taskName,
        short: true
      },
      {
        title: "Status",
        value: taskStatus.toUpperCase(),
        short: true
      },
      {
        title: "Environment",
        value: environment,
        short: true
      },
      {
        title: "Timestamp",
        value: new Date().toISOString(),
        short: true
      }
    ]
  }]
};

// Add output details if available
if (taskOutput) {
  slackPayload.attachments[0].fields.push({
    title: "Output",
    value: typeof taskOutput === 'string'
      ? taskOutput.substring(0, 500)
      : JSON.stringify(taskOutput).substring(0, 500),
    short: false
  });
}

// Build PagerDuty payload
const pagerdutyPayload = {
  routing_key: os.getenv("PAGERDUTY_ROUTING_KEY"),
  event_action: taskStatus === 'success' ? 'resolve' : 'trigger',
  dedup_key: `${environment}-${taskName}`,
  payload: {
    summary: `${taskName} ${taskStatus} in ${environment}`,
    severity: taskStatus === 'success' ? 'info' : 'error',
    source: os.hostname(),
    timestamp: new Date().toISOString(),
    custom_details: {
      task: taskName,
      environment,
      output: taskOutput
    }
  }
};

context.set("slack_payload", slackPayload);
context.set("pagerduty_payload", pagerdutyPayload);
context.set("slack_json", JSON.stringify(slackPayload));
context.set("pagerduty_json", JSON.stringify(pagerdutyPayload));
```
