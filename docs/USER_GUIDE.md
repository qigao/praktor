# Praktor 使用指南

当前 CLI 和共享 SDK 使用同一套 C11 工作流运行时。工作流来自一份 YAML，
任务按依赖关系执行。根级 `parallelism` 控制同组就绪任务数，默认一项；
任务数和保留值由显式 limits 限制。

## 运行

完成根目录 [构建说明](../README.md#building-and-qualification) 后，在匹配的
运行时 PATH 环境中执行：

```sh
praktor --config examples/shared-downloads.yml --task all
```

CLI 用 `--config` 指定文件、`--task` 选择任务，向 stdout 输出 JSON，包含 `workflow_status` 和各
任务的状态、输出。成功返回 0；运行失败返回 1；参数或不支持的输入返回 2。

整个工作流默认限时 30 秒。较长的工作流可在文件参数后设置
`--timeout-ms N`，其中 N 是 1 到 2147483647 之间的毫秒数。例如：

```sh
praktor --config build.yml --task install --timeout-ms 3600000
```

Shell 节点自己的 `timeout` 仍独立生效；任一预算耗尽都会终止相应执行。

CLI 原生加载根 `dotEnv` 指定的文件，然后执行工作流；不使用外部脚本。
路径相对当前工作目录，文件按顺序加载，后者覆盖前者及已有环境变量，
加载失败返回 2 并停止。根 `variables` 与任务 `vars` 使用现有值存储；
任务变量覆盖同名根变量。变量引用统一使用 `$name`，例如
`shell: {cmd: $notify_command}`。嵌入式调用可显式调用
`praktor_load_environment`，再调用 `praktor_execute`。

`--task` 选择指定任务及其传递 `depends_on` 依赖，其他任务返回 `skipped`；
未知任务名会在启动子进程前失败。变量值从 `.env`/继承环境读取，任务 `vars`
和根 `variables` 中的显式值优先；环境值不会复制进可变任务状态。
根级 `inputs`、`input_policy` 以及 CLI `--input`/`--var` 都不是合法入口。
原有位置文件参数形式保留用于整份工作流执行，规范调用使用 `--config`/`--task`。

已选择任务里的 `env`、`dotEnv`、`when`、`triggers`、`each`、`retries`、
`finally`、`sources`、`generates`、`silent` 以及任务元数据里的 `timeout`、
`working_dir` 会在准入时被拒绝。使用文档根的 `dotEnv` 加载外部值，
并通过 `shell.timeout`、`shell.working_dir`、`shell.stream_output`
配置进程。

## 命令默认配置

Shell 和由 `command` 降低得到的命令逐字段继承配置：内置默认值 →
`.env`/继承环境 → 文档 `defaults` → 显式 `shell` 参数。未设置才继承，
`false` 是有效覆盖值；不合并或修改共享 provider 实例。

内置命令时限为 30000 毫秒，`stream_output` 为 `true`。全局环境入口为：

```dotenv
PRAKTOR_TIMEOUT_MS=30000
PRAKTOR_STREAM_OUTPUT=false
```

文档及任务可逐项覆盖：

```yaml
dotEnv: [.env]
defaults:
  timeout: 300000
  stream_output: false
tasks:
  - name: configure
    shell:
      cmd: cmake --preset llvm-mingw-x64-release-user
  - name: build
    depends_on: [configure]
    shell:
      cmd: cmake --build --preset llvm-mingw-x64-release-user
      timeout: 1800000
```

configure 继承文档的两项配置；build 仅覆盖命令时限，仍继承 `false`。
环境时限必须是 1 到 2147483647 的十进制整数毫秒，环境开关只接受
`true` / `false`。文档使用整数毫秒和布尔值；`30s` 不再是合法的
`defaults.timeout`。所有已提供的环境及文档值均先校验，即使随后会被
覆盖；无效值在启动任务前失败，不回退到其他层。

命令时限不会改变 `--timeout-ms` 或嵌入式 `limits.timeout_ms` 的工作流总
预算；任一预算耗尽均独立终止执行。`stream_output` 控制已有 Session
emission hook，关闭它仍保留捕获的 stdout/stderr；当前 CLI 仍返回最终
JSON。独立服务文件的 `config.timeout_ms` 保留 provider 契约，不参与这套
Shell/Command 默认值继承。

## 复用独立 YAML 服务

服务文件描述通用执行器及参数模板，例如仓库中的 `examples/services/download.yml`。

```yaml
provider:
  kind: exe
  path: curl
capabilities: [download]
config:
  args: [--disable, --fail, --location, --globoff, --output, $path, --url, $url]
  timeout_ms: 300000
```

从仓库根目录运行时，工作流使用以下相对路径。多个任务可以复用同一个服务名。

```yaml
services:
  packages: examples/services/download.yml
tasks:
  - name: base
    tool: packages
    with:
      url: https://example.com/
      path: base.html
  - name: cmeta
    tool: packages
    with:
      url: https://example.com/
      path: cmeta.html
```

下载的含义、curl 参数和文件策略都由 YAML 定义。Configurator 每次工作流
为每个服务名创建一个实例，各任务拥有独立 job。相同文件以不同服务名引用
时会创建不同实例。同组任务通过各自的 job 共享已声明服务。

服务文件路径相对调用方当前工作目录；`config.working_dir` 可设置子进程
目录。服务声明最多 16 个，每个文件最多 1 MiB。当前只接纳 `kind: exe`，
文件缺失、声明无效、服务名未知或参数不匹配都会失败；不会切换其他后端。

`config.args` 最多 256 项。`$name` 必须占据完整 argv 项，并在任务 `with`
中提供对应标量；多余参数也会拒绝。若传入值本身为 `$variable`，则从现有
工作流值存储解析，支持任务 `vars`、根 `variables` 及 `.env`/继承环境。执行使用原生 argv，
参数中的空格、引号及 shell 元字符保持为参数内容。每个 job 的参数文本总量
最多 1 MiB，同时 stdout/stderr 合计最多 4096 字节且必须是不含 NUL 的文本。
超限会失败，不截断成成功。

任务输出沿用 `exit_code`、`stdout`、`stderr`。取消后等待实际进程与 IO 排空，
再释放 job 和服务。DLL、嵌入式 TurboScript、WASM provider 尚未接入。
完整工作流示例见 [shared-downloads.yml](../examples/shared-downloads.yml)。

### 在 guard 中查询服务能力

服务文件可用 `capabilities` 声明最多 32 个名称，每个名称包含 1 至 128 个
ASCII 字母、数字、下划线、点或连字符。同一服务中的重复声明会在任务运行前
报错。一次工作流中，任意已激活的服务声明某名称，`capability("名称")` 就
返回真。名称区分大小写，未声明或没有服务时返回假。判定只读取声明，在调用
`tool` 时才启动进程。

```yaml
services:
  packages: examples/services/download.yml
tasks:
  - name: route
    vars: {phase: ready}
    if:
      guard:
        fields: [phase]
        match: '$phase == "ready" && capability("download")'
    then: {set_variable: route, value: download}
    else: {set_variable: route, value: local}
```

`fields` 列出允许读取的任务变量。Selector 要求 `match` 提供至少一个字段，
只使用 `capability()` 的表达式也遵循此要求。

## LLM 工作流

先运行离线的构建摘要示例。它将一份样例响应交给 BT 校验，文档级
Statechart 读取任务生命周期，后继任务再检查 `chart.workflow.accepted`。
把样例的 `finish_reason` 改成 `length` 可复现校验失败。 [离线示例](../examples/llm/summary-workflow.yml)

```sh
praktor --config examples/llm/summary-workflow.yml --task confirm_summary
```

在线示例使用 PATH 上的 curl 8.3.0 或更新版本。运行前将对应的 API 密钥
放入父进程环境，也可以在自己的工作流中通过 `dotEnv` 加载。服务文件里
的 `--variable %NAME` 让 curl 在进程内读取密钥，再用 `--expand-header`
生成认证头。服务声明提供 URL、HTTP 选项和超时，工作流传入请求 JSON。 [curl 变量](https://curl.se/docs/manpage.html#--variable)

| 模型接口 | 环境密钥 | 选择模型 |
| --- | --- | --- |
| [OpenAI Chat Completions 示例](../examples/llm/llm-openai.yml) | `OPENAI_API_KEY` | 修改请求 JSON 的 `model`。 [API 参考](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create) |
| [Claude Messages 示例](../examples/llm/llm-claude.yml) | `ANTHROPIC_API_KEY` | 修改请求 JSON 的 `model`。 [API 参考](https://platform.claude.com/docs/en/api/messages/create) |
| [Gemini generateContent 示例](../examples/llm/llm-gemini.yml) | `GEMINI_API_KEY` | 可用 `GEMINI_MODEL` 覆盖服务声明的默认模型。 [API 参考](https://aistudio.google.com/api-reference/generate-content/generate-content) |

从仓库根目录选择任意一家执行。每条命令用样例测试数据调用一次相应的
远端 API，再检查完成原因和非空文本。`with.request` 用一个 YAML 标量
承载完整 JSON，示例使用 `|` 书写。Tool 将该值作为独立的 argv 参数传给 curl。
[工具参数规则](grammar.md#named-execution-services)

```sh
praktor --config examples/llm/llm-openai.yml --task validate_summary --timeout-ms 65000
praktor --config examples/llm/llm-claude.yml --task validate_summary --timeout-ms 65000
praktor --config examples/llm/llm-gemini.yml --task validate_summary --timeout-ms 65000
```

CLI 结果中的 `tasks.call_<服务>.outputs.stdout` 保留原始响应。后继 BT 从
`shell_output` 读取同一响应。示例请求短文本，服务仍受 stdout/stderr
合计 4096 字节的捕获上限约束。HTTP 错误或捕获超限会使模型任务失败。
缺少摘要或返回生成截断的完成码会使校验任务失败。

Praktor 可以按预先声明的 DAG 编排模型、工具和校验任务。模型响应作为
数据交给 BT 中已声明的 guard，批准与拒绝可由文档级状态图记录。
需要模型自行生成任务图的 agent，由宿主管理动态规划。 [Statechart 与 BT](STATECHART.md) · [当前准入范围](../README.md#current-admitted-subset)

## 在 CI 中复用工作流

本地和 CI 可以使用同一份 Praktor YAML。GitHub Actions 负责触发工作流
及选择 Runner。在已安装 Praktor 并配置相应构建 SDK 的 Runner 上，可把
现有 `build.yml` 作为一个 step 运行。 [GitHub Actions 文档](https://docs.github.com/en/actions/get-started/understand-github-actions)

在 `.env` 中设置 `PRESET=llvm-mingw-x64-release-user` 或其他同时具有
configure、build 和 test 定义的用户 preset。Praktor 加载该值并展开四个
阶段的命令。安装从 `build/<PRESET>` 读取产物。 [环境变量示例](../.env.example)

```yaml
- name: Build and test with Praktor
  run: praktor --config build.yml --task test --timeout-ms 3600000
```

## Command 与依赖

```yaml
tasks:
  - name: greet
    command: echo Hello, Praktor!
  - name: finish
    depends_on: [greet]
    command: echo Workflow complete
```

当前 Command 接纳单条 Shell 命令。成功发布的任务输出包含 `exit_code`、
`stdout` 和 `stderr`。命令经目标系统 shell 解释，调用者负责其内容和引用。

整条命令可写成 `command: $notify_command`，也可在 `command` 和
`shell.cmd` 中内联引用，例如 `cmake --preset "$PRESET"`。Praktor 在启动
shell 前展开。已有的花括号和 `$vars.` 写法迁移为 `$name`。
读取端口和 guard 也使用 `$name`，声明、`fields` 和输出键写普通名称。
[命令语法](grammar.md#command-values)

## SetVariable

```yaml
tasks:
  - name: first
    set_variable: answer
    value: ready
  - name: second
    depends_on: [first]
    set_variable: copied
    from: $answer
```

## 任务输出

复合 BT 用任务级 `outputs` 声明成功时发布的变量。例如
`outputs: [$summary, $finish_reason]`。声明列表决定要提交到工作流的
全部成功输出，这些值也出现在任务结果的 `outputs` 里。若其中一项缺失，
该任务失败，整组输出都不提交。依赖任务可用 `$summary` 读取值。
[离线模型示例](../examples/llm/summary-workflow.yml)

未声明列表时，直接 `set_variable` 默认发布目标变量。直接 `shell`、
`command` 和 `tool` 默认发布其进程输出。Shell 的
`output_key`、`stderr_key` 和 `exit_code_key` 可指定三个不同的变量名，
CLI 的默认结果字段仍为 `stdout`、`stderr` 和 `exit_code`。
Tool 在解析参数或启动进程时失败，结果省略本次没有取得的退出码。

## 复用任务与生成任务

根级 `includes` 将别名映射到本地任务库。从工作目录解析库路径。
`uses: audit.verify` 将库入口及其依赖实例化到调用任务名下。例如调用任务
叫 `audit` 时，库里的 `prepare` 变成 `audit.prepare`。这些任务和普通
任务共用一个 DAG，支持 `--task audit` 选择入口及其依赖。

`dynamic_tasks.items_variable: $devices` 在启动前从声明任务的 `vars`、
根级 `variables` 或加载后的环境读取 JSON 数组。用模板 `name: device-$index`
给每项生成独立任务，BT 使用 `$item` 读取当前项。`depends_on: [inspect]`
等待 `inspect` 生成的整组任务成功，文档 Statechart 可订阅
`task.finished` 观察该汇合点。上游执行后产生的数组可以交给 BT 处理，
本轮 DAG 在执行前已确定。 [设备巡检示例](../examples/workflow-composition.yml) · [组合语法](grammar.md#workflow-composition)

用 `dynamic_tasks.collect: $results` 汇总并行任务的输出。生成任务的结果
各自可见，汇合任务成功后按输入顺序发布 JSON 数组 `$results`，后继 BT
可用 `ParseJson` 读取各项。数组文本上限为 4096 字节。

从仓库根目录运行示例。

```sh
praktor --config examples/workflow-composition.yml --task confirm
```

## 当前执行范围

公开 BT 支持 `repeat`、`delay`、`timeout`、`run_once`、
`keep_running_until_failure`、`ReactiveSequence`、`PipelineSequence`、
`entry_updated` 和 `consume_queue`。用 `timeout: 1000` 给同级
`child` 子树设定 1000 毫秒时限，或用 `delay: 1000` 推迟执行。

Shell 和 Tool 可以嵌入串行 BT。用 `Tool: {service: model, with: {request: $payload}}`
从当前任务读取输入。工作流准入时校验服务名和 `with` 参数名是否与服务声明
匹配。Tool 节点启动时解析 `$payload`，因此可以读取上游已发布的值或当前
BT 中前序节点产生的值。变量缺失时，Tool 在启动进程前失败。后续节点可读取
`$shell_output`，再通过任务的 `outputs` 发布所需结果。
[服务复用示例](../examples/shared-downloads.yml)

`switch` 支持按零基整数索引选择，也支持用 `cases` 映射按字符串匹配，
并用同级 `default` 处理无匹配情况。 [分支语法](grammar.md#switch-cases)

根级 `parallelism: 4` 允许最多四个就绪任务重叠等待与进程 I/O。
同组任务使用各自的上下文，并在全部成功后一起提交输出。
同组必须使用不同的发布键。 [并行语法](grammar.md#dag-parallelism) · [版本检查示例](../examples/upstream-version-checks.yml)

任务可用 `raise` 和 `scope: workflow` 向文档 Statechart 发送已声明的
内部事件，其他任务可用 `wait_until` 等目标状态。选择后继任务时，用其
`depends_on` 将发送方和等待方都纳入运行范围。若等待方需要尚未发出的
信号，还须让两者进入同一就绪组。握手示例用 `parallelism: 2` 让两个同时
就绪的任务同组运行。 [跨任务状态信号](STATECHART.md#跨任务状态信号) · [传感器握手](../examples/statechart-signals.yml)

公开运行时拒绝脚本、BT `Parallel` 和插件发现。

## 嵌入

公共头文件是 `praktor.h`。调用者提供 YAML 字节、limits 和可选取消回调；
每次调用后的结果均通过 `praktor_release_result` 释放。JSON 在释放前有效。
工作流声明 `source: external` 时，嵌入式调用可用
`praktor_execute_with_events` 提供宿主事件。回调按文档或任务作用域
返回类型化字段，迁移在工作流线程上完成。 [宿主事件](STATECHART.md#宿主事件)

详见 [公开头文件](../praktor/include/praktor.h) 和 [C 示例](../README.md#embedded-sdk-c-abi-v3)。
