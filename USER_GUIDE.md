# Praktor 使用指南

> **版本**: v9.1 | **最后更新**: 2026-06-17

Praktor 是一个基于 YAML 的工作流引擎，支持 DAG 任务依赖、行为树控制流、并发执行和内置脚本。

## 目录

1. [快速入门](#1-快速入门)
2. [工作流结构](#2-工作流结构)
3. [任务定义](#3-任务定义)
4. [Runner 类型](#4-runner-类型)
5. [编排节点参考](#5-编排节点参考)
6. [脚本模块参考](#6-脚本模块参考)
7. [模板与表达式](#7-模板与表达式)
8. [Triggers 事件触发](#8-triggers-事件触发)
9. [高级特性](#9-高级特性)
10. [完整示例](#10-完整示例)

---

## 1. 快速入门

### 安装与运行

```bash
praktor run my-workflow.yml
praktor run my-workflow.yml --var ENV=production
praktor run my-workflow.yml --task build  # 只运行指定任务
```

### 最小工作流

```yaml
name: hello-world

tasks:
  - name: greet
    command: echo "Hello, Praktor!"

  - name: build
    depends_on: [greet]
    command: cmake --build build
```

运行结果：`greet` 先执行，完成后并行调度所有依赖它的任务（此处为 `build`）。

---

## 2. 工作流结构

```yaml
name: my-workflow          # 工作流名称（必填）
description: "简要说明"    # 可选

variables:                 # 全局变量（可被 Mustache 模板引用）
  ENV: production
  VERSION: "1.0.0"

env:                       # 全局环境变量（传递给所有 shell 命令）
  NODE_ENV: production

dotEnv:                    # 加载 .env 文件
  - ".env.production"

defaults:                  # 所有任务的默认属性
  timeout: "10m"

tasks:                     # 任务列表（至少 1 个）
  - name: task_a
    command: echo "running"
```

---

## 3. 任务定义

### 3.1 核心属性

| 属性 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | String | **是** | 工作流内唯一标识符 |
| `description` | String | 否 | 任务说明 |
| `depends_on` | String 或 Array | 否 | 前置任务，支持 DAG 依赖 |
| `vars` | Map | 否 | 任务级变量，覆盖全局变量 |
| `env` | Map | 否 | 任务级环境变量 |
| `dotEnv` | String 或 Array | 否 | 任务级 .env 文件 |

### 3.2 控制流属性

| 属性 | 类型 | 说明 |
|------|------|------|
| `when` | String | 条件表达式，为 false 时跳过任务 |
| `each` | Object | 遍历列表或矩阵，为每个元素运行一次 |
| `timeout` | String 或 Number | 任务级超时（`"30s"` / `"5m"` / `"1h"` / 毫秒数） |
| `triggers` | Object | 任务完成后的事件驱动动作 |

### 3.3 执行上下文属性

| 属性 | 类型 | 说明 |
|------|------|------|
| `working_dir` | String | 工作目录（相对路径从工作流文件位置解析） |
| `silent` | Boolean | 抑制命令输出。默认 `false` |

### 3.4 when 条件

```yaml
tasks:
  - name: deploy
    command: ./deploy.sh
    when: "$tasks.build.status == 'success' and $env.ENVIRONMENT == 'production'"

  - name: notify
    command: curl -X POST https://slack.com/webhook
    when: "not empty($tasks.test.outputs.stdout)"
```

`when` 支持的操作符：`==`、`!=`、`<`、`>`、`<=`、`>=`、`and`、`or`、`not`、`contains`、`starts_with`、`ends_with`、`matches`

### 3.5 each 循环

```yaml
tasks:
  - name: deploy_service
    each:
      items: "{{ services }}"    # 遍历变量中的列表
      as: service
    command: "./deploy.sh {{ item.name }} --port {{ item.port }}"

  - name: matrix_build
    each:
      matrix:
        os: [ubuntu, windows]
        node: ["18", "20"]
    command: "npm test --os {{ item.os }} --node {{ item.node }}"
```

---

## 4. Runner 类型

每个任务必须选择一种 runner（恰好一个）。

### 4.1 command — Shell 命令

```yaml
- name: build
  command: "cmake --build build --config Release"

# 多行命令（列表形式）
- name: setup
  command:
    - "mkdir -p dist"
    - "npm install"
    - "npm run build"

# 带参数的命令
- name: test
  command: "pytest tests/"
  working_dir: "./backend"
  timeout: "5m"
  silent: false
  env:
    PYTHONPATH: "."
```

**输出访问**：命令执行后，以下输出自动写入上下文：
- `tasks.TASK_NAME.outputs.stdout` — 标准输出
- `tasks.TASK_NAME.outputs.stderr` — 标准错误
- `tasks.TASK_NAME.outputs.exit_code` — 退出码

### 4.2 program — 直接进程执行（不经过 shell）

```yaml
- name: run_tool
  program: "./tools/my_tool"
  args:
    - "--input"
    - "{{ tasks.prepare.outputs.stdout }}"
    - "--verbose"
  stdin: "optional input data"
  output_format: json   # text（默认）或 json
```

`output_format: json` 时，stdout 会被解析为 JSON 对象并写入 `tasks.TASK_NAME.outputs.data`。

### 4.3 uses — 复用子工作流

```yaml
- name: deploy_backend
  uses: ./workflows/deploy.yml
  vars:
    SERVICE: backend
    PORT: "8080"
  env:
    DEPLOY_ENV: production
```

子工作流的所有任务输出通过 `tasks.TASK_NAME.outputs.*` 在父工作流中可见。

### 4.4 dynamic_tasks — 运行时生成任务

```yaml
- name: fetch_services
  command: "curl -s https://api.example.com/services"
  output_format: json

- name: deploy_all
  dynamic_tasks:
    source: "{{ tasks.fetch_services.outputs.data }}"
    template:
      name: "deploy_{{ item.name }}"
      command: "./deploy.sh --service {{ item.name }}"
      timeout: "5m"
```

### 4.5 actions — 编排控制流

```yaml
- name: resilient_deploy
  actions:
    fallback:
      - sequence:
          - shell: "./check_health.sh"
          - shell: "./deploy.sh"
      - sequence:
          - shell: "./rollback.sh"
          - shell: echo "Rolled back"
```

编排节点用于单个任务内部的细粒度控制流，详见 [第 5 节](#5-编排节点参考)。

---

## 5. 编排节点参考

编排节点在平铺语法中使用，提供任务内部的控制流。所有编排节点内使用 `{ctx.*}` 语法访问上下文（**不是** Mustache 双花括号）。

### 5.1 控制流节点

#### sequence — 顺序执行

所有子节点依次执行，任意一个失败则立即返回 FAILURE。

```yaml
sequence:
  - shell: "step1.sh"
  - shell: "step2.sh"
  - shell: "step3.sh"
```

#### fallback — 备选执行

依次执行子节点，直到一个成功为止。全部失败则返回 FAILURE。

```yaml
fallback:
  - shell: "curl https://primary.api.com"
  - shell: "curl https://backup.api.com"
  - shell: echo "All APIs failed"
```

#### selector — 备选执行（fallback 的别名）

与 `fallback` 语义相同。

#### parallel — 并行执行

并发运行所有子节点，默认策略为所有节点成功才返回 SUCCESS。

```yaml
parallel:
  - shell: "run_tests.sh"
  - shell: "run_linter.sh"
  - shell: "run_security_scan.sh"
```

> **重要**：并行分支各自持有独立的 Blackboard 副本，执行后合并结果。若多个分支写入同一个 key，最后一个写入的分支获胜。

#### reactive_sequence — 响应式顺序

每次 tick 都从第一个子节点重新检查，适合监控场景。

#### if_then_else — 条件分支

```yaml
if_then_else:
  condition:
      shell: "./check_condition.sh"
    then:
      shell: "./on_true.sh"
    else:
      shell: "./on_false.sh"
```

也可用内联条件：

```yaml
if_then_else:
    condition: "{ctx.variables.ENV} == 'production'"
    then:
      shell: "deploy-prod.sh"
    else:
      shell: "deploy-staging.sh"
```

#### while_do — 条件循环

```yaml
while_do:
    max_iterations: 50   # 可选，默认 100，超出时输出 warning
    condition:
      shell: "./check_ready.sh"
    action:
      shell: "./wait_step.sh"
```

也可用内联条件：

```yaml
while_do:
    condition: "{ctx.variables.RETRY_COUNT} < 5"
    action:
      shell: "./retry.sh"
```

#### switch — 多路分支

```yaml
switch:
    variable: "{ctx.variables.DEPLOY_TARGET}"
    cases:
      - shell: "deploy-dev.sh"
      - shell: "deploy-staging.sh"
      - shell: "deploy-prod.sh"
```

#### pipeline_sequence — 管道序列

将前一个节点的输出自动传递给下一个节点的输入。

```yaml
pipeline_sequence:
    input_key: raw_data
    output_key: final_result
    children:
      - shell:
          cmd: "transform_step1.sh"
          output_key: step1_out
      - shell:
          cmd: "transform_step2.sh"
          output_key: step2_out
```

### 5.2 装饰器节点

装饰器包裹一个子节点，修改其执行行为。

#### timeout — 超时装饰器

```yaml
timeout:
    timeout_ms: 5000   # 毫秒
    child:
      shell: "long_task.sh"
```

> **限制**：`timeout` 对 `shell:` 子节点有效（进程会被强制终止）。对 `sequence`、`fallback` 等复合子节点，超时检查发生在子节点完成之后，无法中途终止。

#### repeat — 重复执行

```yaml
repeat:
    num_cycles: 3   # 必填，重复执行次数
    child:
      shell: "run_test.sh"
```

#### delay — 延迟后执行

```yaml
delay:
    delay_ms: 2000   # 等待 2 秒后执行子节点
    child:
      shell: "delayed_task.sh"
```

#### inverter — 反转结果

SUCCESS ↔ FAILURE，RUNNING 不变。

```yaml
inverter:
    child:
      shell: "check_not_exists.sh"
```

#### force_success / force_failure

强制返回 SUCCESS 或 FAILURE，忽略子节点实际状态。

```yaml
force_success:
    child:
      shell: "optional_task.sh"   # 失败也当成功
```

#### keep_running_until_failure — 持续运行直到失败

```yaml
keep_running_until_failure:
    max_iterations: 100   # 可选
    child:
      shell: "monitor_step.sh"
```

#### run_once — 只运行一次

在一次 tick 循环中只执行一次，后续 tick 跳过。

```yaml
run_once:
    child:
      shell: "init.sh"
```

#### precondition — 前置条件

```yaml
precondition:
    condition: "{ctx.variables.IS_READY} == true"
    child:
      shell: "main_task.sh"
```

#### entry_updated — 黑板变量变化时执行

```yaml
entry_updated:
    watch_key: sensor_value
    child:
      shell: "on_change.sh"
```

#### consume_queue — 消费队列

```yaml
consume_queue:
    queue_key: task_queue
    item_key: current_item    # 可选，默认 queue_item
    child:
      shell: "process_item.sh"
```

### 5.3 叶节点

叶节点执行实际动作，不包含子节点。

#### shell — 执行 Shell 命令

```yaml
# 简写形式
shell: "echo hello"

# 完整形式
shell:
    cmd: "cmake --build build --target all"
    output_key: build_output      # 保存 stdout（默认: shell_output）
    stderr_key: build_errors      # 保存 stderr
    exit_code_key: build_exit     # 保存退出码
    working_dir: "{ctx.variables.BUILD_DIR}"
    timeout: 60000                # 毫秒，此处覆盖任务级 timeout
    stream_output: true           # 实时流式输出到控制台
```

#### parse_json — 解析 JSON

```yaml
sequence:
    - shell:
        cmd: "curl -s https://api.example.com/data"
        output_key: api_response
    - parse_json:
        input_key: api_response
        path: "$.data.items[0].id"   # JSONPath
        output_key: item_id
```

#### parse_regex — 正则提取

```yaml
sequence:
    - shell:
        cmd: "git describe --tags"
        output_key: version_raw
    - parse_regex:
        input_key: version_raw
        pattern: "v([0-9]+\\.[0-9]+\\.[0-9]+)"
        capture_group: 1
        output_key: version_number
```

#### parse_lines — 按行解析

```yaml
parse_lines:
    input_key: command_output
    filter: "^ERROR"       # 可选正则过滤
    output_key: error_lines
```

#### parse_keyvalue — 键值对解析

```yaml
parse_keyvalue:
    input_key: env_output
    delimiter: "="
    line_separator: "\\n"
    output_key: parsed_env
```

#### check_exit_code — 验证退出码

```yaml
sequence:
    - shell:
        cmd: "make test"
        exit_code_key: test_exit
    - check_exit_code:
        expected: 0
        input_key: test_exit
```

> **注意**：`expected` 在编排内部是必填项。简写形式 `check_exit_code: 0` 也被接受。

#### file_exists — 文件存在检查

```yaml
file_exists:
    path: "{ctx.variables.ARTIFACT_PATH}"
    output_key: file_found
    fail_if_missing: true   # 可选，默认 false（文件不存在时返回 FAILURE）
```

#### sleep — 暂停执行

```yaml
sleep:
    duration: 1000   # 毫秒
```

#### wait_event — 等待外部事件

```yaml
wait_event:
    event: "build_complete"
    timeout: 30000   # 可选，超时返回 FAILURE
```

#### set_variable — 设置黑板变量

```yaml
set_variable:
    key: result_flag
    value: "true"
    # 或使用 from 从另一个键复制
    from: another_key
```

### 5.4 上下文访问（{ctx.*} 语法）

在编排节点参数中使用单花括号访问上下文：

```yaml
shell:
    cmd: "deploy.sh --env {ctx.variables.ENVIRONMENT} --version {ctx.tasks.build.outputs.version}"
    working_dir: "{ctx.env.DEPLOY_DIR}"
```

可用路径：
- `{ctx.variables.VAR_NAME}` — 全局或任务级变量
- `{ctx.env.ENV_VAR}` — 环境变量
- `{ctx.tasks.TASK_NAME.outputs.KEY}` — 前置任务的输出

> **注意**：编排层用 `{ctx.*}`（单花括号），任务层用 `{{ }}` （双花括号 / Mustache）。不要混用。

---

## 6. 脚本模块参考

`script:` 块在 runner 完成后执行，用于数据转换和上下文操作。脚本使用 TurboScript 语言（类 JavaScript 语法，JIT 编译）。

```yaml
- name: process_data
  command: "curl -s https://api.example.com/data"
  output_format: json
  script: |
    var data = ctx.get("tasks.process_data.outputs.data")
    var items = json.query(data, "$[@.status == \"active\"]")
    ctx.output("active_count", items.length)
    ctx.output("active_items", json.stringify(items))
```

### 6.1 ctx 模块 — 上下文访问

| 函数 | 说明 |
|------|------|
| `ctx.get(path)` | 读取上下文值，`path` 支持 `"tasks.TASK.outputs.KEY"`、`"variables.X"`、`"env.Y"` 等 |
| `ctx.set(path, value)` | 写入上下文变量（**禁止**写 `tasks.*`、`failure_context.*` 等保留路径） |
| `ctx.output(key, value)` | 写入当前任务的输出（推荐方式） |

```javascript
// 读取前置任务输出
var stdout = ctx.get("tasks.build.outputs.stdout")
var exit_code = ctx.get("tasks.build.outputs.exit_code")

// 写入当前任务输出
ctx.output("processed_count", 42)
ctx.output("result_json", json.stringify({status: "ok", count: 42}))

// 写入全局变量
ctx.set("variables.NEXT_VERSION", "2.0.0")
```

### 6.2 json 模块 — JSON 操作

| 函数 | 说明 |
|------|------|
| `json.parse(str)` | 将 JSON 字符串解析为对象 |
| `json.stringify(val)` | 将对象序列化为 JSON 字符串 |
| `json.query(val, expr)` | 使用 JSONPath 表达式查询 |

```javascript
var raw = ctx.get("tasks.fetch.outputs.stdout")
var data = json.parse(raw)

// JSONPath 查询
var users = json.query(data, "$.users[@.active == true]")
var names = json.query(users, "$[*].name")

ctx.output("user_names", json.stringify(names))
```

### 6.3 http 模块 — HTTP 请求

| 函数 | 说明 |
|------|------|
| `http.get(url [, options])` | GET 请求 |
| `http.post(url, body [, options])` | POST 请求 |
| `http.put(url, body [, options])` | PUT 请求 |
| `http.del(url [, options])` | DELETE 请求 |
| `http.patch(url, body [, options])` | PATCH 请求 |

```javascript
// 简单 GET（返回响应体字符串）
var body = http.get("https://api.example.com/status")

// 带选项的 GET（返回 {status, body, headers, data, error}）
var resp = http.get("https://api.example.com/data", {
    headers: {"Authorization": "Bearer " + ctx.get("variables.TOKEN")},
    timeout: 10000,
    follow_redirects: true
})
if (resp.status == 200) {
    ctx.output("api_data", resp.body)
}

// POST JSON
var payload = json.stringify({name: "test", value: 42})
var resp = http.post("https://api.example.com/create", payload, {
    headers: {"Content-Type": "application/json"},
    bearer_token: ctx.get("env.API_TOKEN")
})
```

### 6.4 fs 模块 — 文件系统

| 函数 | 说明 |
|------|------|
| `fs.read(path)` | 读取文件内容，返回字符串 |
| `fs.write(path, data)` | 写入文件（创建或覆盖） |
| `fs.append(path, data)` | 追加写入 |
| `fs.exists(path)` | 文件是否存在，返回 bool |
| `fs.stat(path)` | 返回 `{size, is_dir}` |
| `fs.mkdir(path)` | 创建目录（含父目录） |
| `fs.remove(path)` | 删除文件 |

```javascript
var content = fs.read("./config.json")
var config = json.parse(content)

fs.write("./output/result.json", json.stringify({
    version: ctx.get("variables.VERSION"),
    timestamp: ctx.get("variables.BUILD_TIME")
}))
```

### 6.5 shell 模块 — 执行 Shell 命令

| 函数 | 说明 |
|------|------|
| `shell.exec(command [, options])` | 执行 shell 命令，返回 `{exit_code, stdout, stderr, pid, success}` |

```javascript
var result = shell.exec("git rev-parse HEAD")
if (result.exit_code == 0) {
    ctx.output("commit_hash", result.stdout.trim())
}

// 带选项
var result = shell.exec("npm test", {
    working_dir: "./frontend",
    timeout_ms: 120000,
    stream_output: true,
    env: {"NODE_ENV": "test"}
})
```

> **注意**：`shell.exec()` **不受**任务级 `timeout:` 约束，需在 options 中显式设置 `timeout_ms`。

### 6.6 base64 模块

```javascript
var encoded = base64.encode("Hello, World!")
var decoded = base64.decode(encoded)
```

### 6.7 log 模块

```javascript
log.info("Processing " + item_count + " items")
log.warn("Retry attempt " + attempt)
log.error("Failed to connect: " + error_msg)
log.debug("Raw response: " + raw)
```

### 6.8 math 模块

```javascript
var result = math.abs(-42)
var expr_result = math.eval("2 * (3 + 4)")   // exprtk 表达式求值
```

TurboScript 扩展统一通过标准插件导入，不接受工作流中的 DLL 路径或自定义 ABI：

```javascript
import("net")
import("parser")
import("rules_forge")
```

### 6.9 fail() — 主动失败

```javascript
var data = json.parse(ctx.get("tasks.fetch.outputs.stdout"))
if (data.version == null) {
    fail("Version field missing in API response")
}
ctx.output("version", data.version)
```

`fail()` 调用后脚本立即停止，任务标记为失败。

---

## 7. 模板与表达式

### 7.1 Mustache 双花括号 `{{ }}`（任务层）

用于 `command:`、`program:`、`args:`、`vars:`、`env:` 等任务级字段：

```yaml
variables:
  ENV: production
  VERSION: "1.2.3"

tasks:
  - name: deploy
    command: "./deploy.sh --env {{ ENV }} --version {{ VERSION }}"
    env:
      TAG: "v{{ VERSION }}-{{ ENV }}"
```

可用路径：
- `{{ VAR_NAME }}` — 全局变量
- `{{ tasks.TASK.outputs.KEY }}` — 前置任务输出
- `{{ env.HOME }}` — 环境变量

Mustache 支持节（section）和循环：
```yaml
command: |
  echo "Artifacts:"
  {{#tasks.build.outputs.data.artifacts}}
  echo "- {{name}}"
  {{/tasks.build.outputs.data.artifacts}}
```

### 7.2 when 表达式

```yaml
when: "$tasks.build.status == 'success'"
when: "not empty($tasks.test.outputs.stdout)"
when: "$env.ENVIRONMENT == 'production' and $tasks.lint.status == 'success'"
when: "$tasks.check.outputs.exit_code == '0'"
```

表达式中用 `$` 前缀引用路径（与 Mustache `{{ }}` 不同）。

### 7.3 编排层上下文引用 `{ctx.*}`

```yaml
shell:
    cmd: "deploy.sh --target {ctx.variables.ENV}"
    working_dir: "{ctx.env.WORKSPACE}"
```

> **不要混用**：`{ctx.*}` 只在编排节点参数内有效；`{{ }}` 只在任务层字段有效。

---

## 8. Triggers 事件触发

```yaml
tasks:
  - name: build
    command: "cmake --build build"
    triggers:
      on_success:
        - notify_success
      on_failure:
        - notify_failure
        - rollback
      on_complete:
        - cleanup

  - name: notify_success
    command: "curl -X POST https://slack.com/webhook -d 'Build succeeded'"

  - name: notify_failure
    command: "curl -X POST https://slack.com/webhook -d 'Build FAILED'"

  - name: cleanup
    command: "rm -rf /tmp/build_artifacts"
```

Trigger 任务名支持变量替换：
```yaml
triggers:
  on_failure:
    - "notify_{{ env.ENVIRONMENT }}"
```

### 8.1 失败上下文变量

在 `on_failure` 触发的任务中，可访问以下失败上下文变量：
```yaml
- name: notify_failure
  script: |
    var error = ctx.get("failure_context.error_message")
    var task = ctx.get("failure_context.failed_task")
    log.error("Task " + task + " failed: " + error)
```

---

## 9. 高级特性

### 9.1 post-processing script（后置脚本）

任何 runner 后都可附加 `script:` 块：

```yaml
- name: parse_version
  command: "git describe --tags --long"
  script: |
    var raw = ctx.get("tasks.parse_version.outputs.stdout")
    # raw 格式: v1.2.3-14-gabcdef
    var parts = raw.split("-")
    ctx.output("tag", parts[0])
    ctx.output("commits_since", parts[1])
    ctx.output("commit_hash", parts[2])
```

### 9.2 output_format: json

`command:` 和 `program:` 支持自动解析 JSON 输出：

```yaml
- name: get_config
  command: "cat config.json"
  output_format: json

- name: use_config
  depends_on: [get_config]
  command: "deploy.sh --endpoint {{ tasks.get_config.outputs.data.api_endpoint }}"
```

### 9.3 变量优先级

从高到低：
1. 任务级 `vars:`
2. 全局 `variables:`
3. 系统环境变量
4. `dotEnv` 文件中的变量

### 9.4 数据流模式

```yaml
tasks:
  - name: fetch
    command: "curl -s https://api.example.com"
    output_format: json

  - name: transform
    depends_on: [fetch]
    script: |
      var data = ctx.get("tasks.fetch.outputs.data")
      var processed = json.query(data, "$.items[@.active == true].name")
      ctx.output("names", json.stringify(processed))

  - name: report
    depends_on: [transform]
    command: "echo 'Active users: {{ tasks.transform.outputs.names }}'"
```

---

## 10. 完整示例

### 10.1 CI/CD 流水线

```yaml
name: ci-pipeline
description: "构建、测试、发布完整流水线"

variables:
  REGISTRY: ghcr.io/myorg
  IMAGE_NAME: myapp

env:
  DOCKER_BUILDKIT: "1"

tasks:
  - name: lint
    command: "npm run lint"
    timeout: "5m"

  - name: test
    depends_on: [lint]
    command: "npm test -- --coverage"
    timeout: "10m"

  - name: build_image
    depends_on: [test]
    command: "docker build -t {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ env.GIT_SHA }} ."
    timeout: "15m"
    triggers:
      on_failure:
        - notify_build_failed

  - name: push_image
    depends_on: [build_image]
    command: "docker push {{ REGISTRY }}/{{ IMAGE_NAME }}:{{ env.GIT_SHA }}"
    when: "$env.BRANCH == 'main'"

  - name: notify_build_failed
    command: "curl -s -X POST {{ env.SLACK_WEBHOOK }} -d '{\"text\": \"Build failed!\"}'"
    silent: true
```

### 10.2 编排部署任务

```yaml
name: resilient-deploy

variables:
  MAX_RETRIES: "3"

tasks:
  - name: deploy_with_retry
    repeat:
        num_cycles: 3
        child:
          fallback:
            - sequence:
                - shell:
                    cmd: "./health_check.sh"
                    output_key: health_status
                - shell: "./deploy.sh --version {ctx.variables.VERSION}"
                - shell: "./post_deploy_check.sh"
            - sequence:
                - shell: log.sh "Deploy attempt failed, retrying..."
                - sleep:
                    duration: 5000

  - name: verify
    depends_on: [deploy_with_retry]
    sequence:
        - shell:
            cmd: "curl -s https://myapp.com/health"
            output_key: health_response
        - parse_json:
            input_key: health_response
            path: "$.status"
            output_key: status_value
        - check_exit_code:
            expected: 0
```

### 10.3 数据处理管道

```yaml
name: data-pipeline

tasks:
  - name: fetch_data
    command: "curl -s https://data.api.com/export"
    output_format: json

  - name: process
    depends_on: [fetch_data]
    script: |
      var data = ctx.get("tasks.fetch_data.outputs.data")
      var records = json.query(data, "$.records[@.status == \"active\"]")
      var count = records.length

      log.info("Processing " + count + " active records")

      var result = []
      var i = 0
      while (i < count) {
          var r = records[i]
          result.push({id: r.id, value: r.value * 2})
          i = i + 1
      }

      ctx.output("count", count)
      ctx.output("processed", json.stringify(result))

  - name: write_result
    depends_on: [process]
    script: |
      var data = ctx.get("tasks.process.outputs.processed")
      fs.write("./output/result.json", data)
      log.info("Written " + ctx.get("tasks.process.outputs.count") + " records")
```
