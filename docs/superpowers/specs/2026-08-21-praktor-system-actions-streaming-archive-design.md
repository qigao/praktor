# Praktor 系统动作与流式归档设计

日期：2026-08-21

状态：已确认，等待实现计划

范围：Praktor、Retro Trident 工作流、Retro Package Installer

## 1. 背景

Retro 当前把一部分系统管理能力放在 Praktor 工作流之外：

- `service-*.yml` 直接调用 `sc.exe`，依赖固定休眠和文本片段判断状态。
- `managed-app-*.yml` 调用独立的 `trident_process_control` helper。
- `package-sync-apply.yml` 先把升级包完整下载到文件，再由 `retro_package_installer` 解压并覆盖目标目录。
- `retro_package_installer` 同时负责 ZIP 解压、文件覆盖和 marker 生命周期，职责边界过宽。

Praktor 已经具备 YAML 工作流、DAG 调度、`program`、`download`、TurboHTTP 和进程执行能力。本设计把系统动作与归档数据通道收敛到 Praktor，同时保留 Retro 对安装状态和业务流程的所有权。

## 2. 目标与非目标

### 2.1 目标

1. 提供声明式 `service` 动作，但仍通过进程模板调用系统命令。
2. 提供非阻塞生命周期语义的 `managed_process` 动作，与阻塞式 `program` 分离。
3. 提供文件和字节流双向归档：
   - HTTP 下载流 → ZIP 解压 → staging directory。
   - 文件/目录 → ZIP 创建 → HTTP 上传流。
   - 文件 ↔ ZIP 文件仍作为同一 archive 接口的普通端点。
4. 流句柄只在单次 workflow execution 内存在，不进入可序列化 context，也不改变 `Praktor.dll` C ABI。
5. 明确状态所有权、失败清理、背压、资源上限与可复验测试。
6. 迁移 Retro 现有 service、managed app 和 package 工作流，并在验证完成后删除重复 helper。

### 2.2 非目标

- 不直接调用 Windows Service Control Manager API。
- 不把 TurboScript `os.service_*` 立即删除或改成新动作的别名。
- 不实现跨 workflow、跨进程或可持久化的流句柄。
- 不支持 TAR、7z、加密 ZIP、多卷 ZIP 或任意归档算法。
- 不把归档中的单个 entry 暴露为可被后续任务独立消费的流。
- 不宣称现有 package overlay 是原子部署，也不在本次改造中实现完整回滚。

## 3. 已评估方案

### 3.1 Service 管理

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| Windows SCM API | 状态和错误结构化，避免解析命令输出 | 平台绑定，偏离“模板调用系统命令”的既定部署方式 | 不采用 |
| TurboScript `os.service_*` | 已存在，可快速复用 | 同样解析命令输出；等待、幂等和错误结构化不足；领域语义留在脚本层 | 不作为新动作实现 |
| Praktor `service` + process profile | YAML 语义稳定；平台命令可配置；复用统一进程执行器；可集中实现等待与错误 | profile 仍需解析外部工具输出 | 采用 |

### 3.2 Process 管理

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| 继续保留 `trident_process_control` | 当前行为已存在 | 多一个二进制与部署入口；工作流无法统一得到结构化结果 | 迁移期保留，最终删除 |
| 扩展 `program` | 文件较少 | 阻塞命令与长生命周期进程语义混合，状态和所有权不清 | 不采用 |
| 新增 `managed_process` | 生命周期、幂等和查询语义清晰；可复用底层 ProcessExecutor | 新增 schema/executor/test | 采用 |

### 3.3 Archive 引擎

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| 继续使用当前 `zip` 库 | 仓库已有使用点 | 未验证 callback 式流输入输出；当前安装路径是文件导向 | 不采用 |
| 手写 ZIP streaming | 可完全控制接口 | 格式、安全和边界风险高，违反成熟库优先原则 | 禁止 |
| vcpkg `libarchive` | 官方 read/write callback 支持适配流；格式校验成熟 | 新增构建及运行时依赖；需要限制启用格式 | 采用，仅启用 ZIP store/deflate |

## 4. 总体架构

```text
YAML parser / schema validation
                |
        WorkflowExecutor
          /     |      \
   service  managed   stream/archive
   executor process      executors
      |         |           |
 command     Process    ResourceRegistry
 profile     Manager       /       \
      \         /     HTTP stream  libarchive
       ProcessExecutor      adapter
```

分层职责：

- Parser/schema 只负责输入格式、类型、范围和引用校验。
- Executor 把声明式动作翻译为领域操作，不直接散落平台命令拼接。
- Manager 负责状态查询、幂等、等待、超时、取消和结构化结果。
- Adapter 负责 TurboHTTP、libarchive 和平台进程类型的转换。
- `ResourceRegistry` 负责 execution-local 非序列化资源及其清理。

## 5. 状态和所有权

唯一事实源如下：

| 状态 | 唯一 owner | Praktor 的角色 |
|---|---|---|
| Workflow/task/resource 生命周期 | Praktor workflow execution | 直接拥有并推进 |
| 服务运行状态 | 操作系统服务管理器 | 通过配置的命令 profile 查询和请求变更 |
| 进程运行状态 | 操作系统 | 查询并发出启动/停止请求，不保存独立真值 |
| ZIP entry 与 staging 内容 | archive task | 任务成功前均视为临时派生数据 |
| 安装目标和 package marker | Retro Package Installer | Praktor 只调用命令并消费结果 |
| MQTT 任务与业务相关性 | Trident | 将消息映射到 workflow，不直接写内部任务状态 |

每个状态只能由其 owner 推进。查询返回 snapshot，不得隐式改变状态。外部副作用由 executor 发起，最终成功只由重新查询到的系统事实或已完成的 I/O 事实确认。

## 6. YAML 动作契约

### 6.1 `service`

```yaml
- name: start_camera_service
  service:
    operation: start
    name: "{{ SERVICE_NAME }}"
    profile: windows_scm
    arguments: []
    timeout_ms: 30000
    poll_interval_ms: 200
```

支持的 operation：`status`、`start`、`stop`、`restart`。

规则：

1. `profile` 是配置中的命令模板名称，不是可执行 shell 文本。
2. 参数以 argv 传递，不经过 shell 拼接。
3. `start`/`stop` 先查询现状；已处于目标状态时返回成功且 `changed: false`。
4. 请求变更后轮询到目标状态；超时返回 `timeout`，不得用固定 sleep 当作成功依据。
5. `restart` 是受控的 stop → confirmed stopped → start → confirmed running 流程。
6. Windows profile 解析 `sc.exe query` 的数字状态字段，不依赖本地化状态名称。
7. profile 必须声明 status/start/stop 命令、状态解析器和允许的退出码；缺失即在 workflow 启动时失败。

建议结果：

```json
{
  "name": "RetroCamera",
  "operation": "start",
  "state": "running",
  "changed": true,
  "duration_ms": 842
}
```

### 6.2 `managed_process`

```yaml
- name: start_screensaver
  managed_process:
    operation: start
    executable: "{{ SCREENSAVER_EXE }}"
    arguments: []
    identity:
      image_name: RetroScreenSaver.exe
    startup_timeout_ms: 5000
    stop_timeout_ms: 5000
```

支持的 operation：`status`、`start`、`stop`、`restart`。

与 `program` 的边界：

- `program` 表示启动后等待退出并取得 exit/stdout/stderr。
- `managed_process` 表示进程生命周期控制；成功启动后任务即可结束。
- `managed_process` 不把 PID 当作永久身份。每次状态改变前重新验证配置的 identity。
- stop 优先请求优雅退出，在显式允许时才在超时后强制终止。
- 重复 start/stop 返回成功且 `changed: false`。

## 7. Execution-local 流资源

### 7.1 ResourceRegistry

`WorkflowExecutionContext` 增加独立的 `ResourceRegistry`。它与可序列化 `WorkflowValue` 分开：JSON 中只保存资源引用，真实对象不能复制、持久化或跨 ABI。

```text
Ready -> Acquired -> Completed
                  -> Failed
                  -> Cancelled
```

不变量：

- 句柄包含 resource type、workflow generation 和唯一 ID。
- `ByteSource` 与 `ByteSink` 是不同类型，不能互换。
- 单消费者、单次 acquire；重复消费返回 `resource_already_consumed`。
- generation 不匹配返回 `resource_not_found`，不尝试猜测或恢复。
- workflow 结束时 registry 取消并关闭所有未完成资源。
- 每个 workflow 配置最大 handle 数、单 bridge 字节数和总 retained bytes。
- registry 自身由 workflow owner context 修改；传输线程只操作已取得的资源实例。

### 7.2 HTTP source

```yaml
- name: package_source
  stream:
    open_http_source:
      url: "{{ DOWNLOAD_URL }}"
      timeout_ms: 300000
    output: body
```

此任务只注册 lazy source 并返回 handle。实际请求由消费它的 archive task 启动，因此 DAG 不需要把“资源就绪”误认为“下载完成”。

### 7.3 HTTP sink

```yaml
- name: upload_target
  stream:
    open_http_sink:
      url: "{{ UPLOAD_URL }}"
      method: PUT
      timeout_ms: 300000
    output: body
```

sink 在 archive writer 获取后开始请求。只有生产完成、请求 body 完整发送且最终 HTTP 响应为 2xx，消费 task 才成功。

## 8. Archive 契约

### 8.1 流式解压

```yaml
- name: extract_package
  archive:
    operation: extract
    format: zip
    source:
      stream: package_source.body
    destination:
      directory: "{{ STAGING_DIR }}"
    limits:
      max_entries: 10000
      max_entry_bytes: 1073741824
      max_total_bytes: 4294967296
      max_compression_ratio: 200
```

`source` 还允许 `{file: path}`。`destination` 首版只允许 directory。

### 8.2 流式创建

```yaml
- name: upload_snapshot
  archive:
    operation: create
    format: zip
    source:
      directory: "{{ SNAPSHOT_DIR }}"
    destination:
      stream: upload_target.body
    compression: deflate
```

`source` 还允许显式 file list，`destination` 还允许 `{file: path}`。

### 8.3 安全策略

在创建目标文件前验证 entry：

- 拒绝绝对路径、drive prefix、UNC、空名称和 `..` 穿越。
- 路径归一化后必须仍位于 staging root 内。
- 拒绝重复规范化路径和 file/directory 类型冲突。
- 拒绝 symlink、hardlink、junction 和 reparse point。
- 只启用 ZIP、store 和 deflate；拒绝加密、多卷及未知算法。
- 同时限制 entry 数、单 entry 展开大小、总展开大小和压缩比。
- 临时目录只能由本次任务创建；失败清理不得递归操作未经验证的路径。

## 9. HTTP 与 archive 并发

TurboHTTP stream callback 提供的 buffer 只在 callback 期间有效，因此不能把 borrowed 指针保存到后续任务。HTTP 与 libarchive 之间使用有界字节 bridge：

```text
download: TurboHTTP producer -> bounded SPSC bridge -> libarchive reader
upload:   libarchive writer  -> bounded SPSC bridge -> TurboHTTP consumer
```

规则：

- 每个已消费的 HTTP stream resource 拥有自己的请求/协程上下文、bridge 和取消状态。
- 缓冲区满时阻塞生产方形成背压；不允许无界增长或 `DROP_OLDEST`。
- EOF、错误和取消是不同终止状态，并由 bridge 显式传播。
- HTTP stream 一旦传输任何 body 字节，不允许透明 retry 或 redirect replay。
- 下载中 libarchive 提前失败时取消 HTTP；HTTP 失败时唤醒并终止 libarchive reader。
- 上传中 libarchive 失败时关闭 request body 为 error，而不是正常 EOF。
- 不记录 presigned URL query；诊断信息只保留 scheme、host、path、状态码和字节计数。

## 10. Package 升级迁移

目标流程：

```text
MQTT command
  -> Trident workflow dispatch
  -> open_http_source
  -> archive.extract to staging
  -> stop managed processes/services
  -> retro_package_installer apply-directory
  -> recover managed processes/services
  -> finalize package marker
  -> workflow result
```

`retro_package_installer` 新增 `--source-directory`，保留：

- overlay 目标文件的规则；
- pending/success/failed marker 生命周期；
- 安装错误和恢复结果。

ZIP 解压迁移到 Praktor 后，验证所有工作流不再使用 archive 输入，再删除 installer 内部 ZIP 依赖。现有 overlay 不是原子替换，也没有完整 backup/rollback；本次改动必须在文档和错误结果中如实保留这一限制。

状态迁移顺序：

1. 流和 archive 成功，只产生 staging 派生数据。
2. installer 创建 pending marker 后才开始写正式目标。
3. 写入失败由 installer 写 failed marker；Praktor 不伪造成功。
4. 服务/进程恢复失败使 workflow 失败，并保留可诊断 marker/output。
5. 只有安装和规定的恢复动作均成功后才 finalize success。

## 11. 错误模型

`TaskResult` 增加结构化错误信息，同时保留现有 success/output/exit 信息的兼容读取路径：

```text
error_code : stable machine-readable code
phase      : schema/open/transfer/archive/apply/recover/finalize
message    : redacted human-readable summary
details    : bounded structured values
```

错误码：

- `schema_invalid`
- `resource_not_found`
- `resource_type_mismatch`
- `resource_already_consumed`
- `resource_limit_exceeded`
- `transport_failed`
- `http_status_failed`
- `archive_format_invalid`
- `archive_policy_rejected`
- `filesystem_failed`
- `process_spawn_failed`
- `service_state_failed`
- `timeout`
- `cancelled`
- `unsupported_platform`

底层只返回错误。日志由 executor 或 workflow 结果边界消费一次，避免同一错误逐层重复记录。所有日志必须隐藏 URL query、认证头和归档文件内容。

## 12. 配置与兼容性

### 12.1 新增配置

- service command profiles。
- stream handle 和 retained-byte 上限。
- bridge capacity。
- archive 默认 entry/size/ratio 限制。
- managed process 的启动、停止和强制终止策略。

所有配置在 workflow 开始前校验；运行中不做隐式修复。

### 12.2 兼容性

- `program`、`download` 和当前 TurboScript `os.service_*` 保持现状。
- 新 YAML action 是增量语法；现有 workflow 不被自动重写。
- execution-local resource 不进入 `Praktor.dll` C ABI，现有 C 调用接口不变。
- `TaskResult` 的新增字段需要保持旧字段含义和序列化兼容性。
- 新增 vcpkg `libarchive` 依赖，并更新安装/export/runtime DLL 规则。
- 只迁移仓库内受控的 Retro YAML；迁移完成前保留原 helper 以便回滚。

## 13. 测试策略

### 13.1 Schema 与 executor 单元测试

- 每种 action 的合法最小输入和完整输入。
- 缺失字段、错误类型、非法 operation、负 timeout、未知 profile。
- service start/stop/status/restart 的幂等和状态轮询。
- managed process 的 identity、重复启动、优雅停止和强制终止策略。
- ResourceRegistry 类型错误、重复 acquire、过期 generation、容量上限和取消。

### 13.2 Archive 测试

- ZIP file → directory。
- directory/file list → ZIP file。
- HTTP source → directory。
- directory → HTTP sink。
- store 与 deflate golden archive。
- 路径穿越、绝对路径、重复路径、symlink/reparse point。
- entry 数、单文件、总大小和压缩比限制。
- 空归档、损坏 header、截断 body、CRC 失败、未知或加密算法。
- 失败和取消后的 staging 清理与 handle 关闭。

### 13.3 HTTP 与并发测试

- 本地测试服务器的 2xx、4xx、5xx、短 body 和半途断线。
- producer/consumer 速度失衡时的背压和 bounded-memory 验证。
- 等待双方均可被取消，shutdown 不遗留线程。
- body 开始后不重试；上传只在最终响应成功后完成。
- 日志中不出现 presigned query 或认证头。

### 13.4 集成与回归

- Praktor parser、DAG、既有 `program`/`download` 回归。
- 安装树和外部 consumer 对 `Praktor.dll` 的 ABI 回归。
- Windows 真实 `sc.exe` profile：运行、停止、无权限、服务不存在、超时。
- Retro managed app workflows 与旧 helper 的行为对照。
- Package installer `--source-directory`、marker 成功/失败和恢复路径。
- Trident 从 MQTT 命令到最终 workflow result 的相关性和错误传播。

## 14. 迁移步骤

1. 在 Praktor 增加 typed schema、ResourceRegistry 和结构化错误字段。
2. 实现 service profile、`service` 和 `managed_process`，保留旧 Retro helper。
3. 引入 libarchive，实现文件到文件的 archive 测试。
4. 实现 HTTP source/sink、bounded bridge 和流式 archive 测试。
5. 迁移 Retro `service-*.yml`，完成 Windows 集成测试。
6. 迁移 `managed-app-*.yml`，与 `trident_process_control` 对照验证。
7. 给 package installer 增加 `--source-directory` 并测试 marker/overlay。
8. 迁移 package workflow 到 HTTP stream → archive staging → apply-directory。
9. 回归通过后删除 `trident_process_control` target 和 installer 内部 ZIP 解压。
10. 保留 TurboScript `os.service_*` 兼容接口，另行评估废弃周期。

每一步都可通过恢复上一版 YAML 或保留的 helper 回滚。涉及安装目标已发生部分覆盖时，仍遵循 installer marker 语义，不能把工作流回滚描述为文件级原子回滚。

## 15. 风险与后续工作

- **HIGH（事实）**：当前 package overlay 可发生部分写入，且没有完整 backup/rollback。本设计不扩大该语义，但流式解压不能解决它。后续应单独设计版本目录、原子切换或恢复清单。
- **MED（事实）**：按既定要求，service profile 仍解析 `sc.exe` 输出。数字状态字段降低本地化风险，但外部命令错误表达仍弱于 SCM API。
- **MED（推论）**：新增 HTTP/archive 双执行上下文可能暴露取消或 shutdown 时序问题。必须用有界 bridge 和系统化并发测试证明资源能被回收。
- **MED（事实）**：流式 request body 不可重放。开始传输后任何网络失败都必须显式失败，不能透明重试。
- **LOW（常用做法）**：首版限制 ZIP/store/deflate 可缩小依赖面和输入攻击面；扩展格式必须另行评估 schema、安全限制和测试。

## 16. 完成条件

只有同时满足以下条件，才可宣称迁移完成：

- 所有新增 action 有 schema、单元测试和错误语义。
- 文件和 HTTP 双向 archive 路径均通过正常、边界、失败和取消测试。
- bounded-memory 与 shutdown 行为有可复验结果。
- Retro service、managed app 和 package 流程端到端通过。
- 旧 helper 和 installer ZIP 路径没有剩余调用点后才删除。
- 构建、安装、CTest 和外部 `Praktor.dll` consumer 回归通过。
