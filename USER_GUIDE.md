# Praktor 使用指南

当前 CLI 和共享 SDK 使用同一套 C11 工作流运行时。工作流来自一份 YAML，
任务按依赖关系串行执行；任务数和保留值由显式 limits 限制。

## 运行

完成根目录 [构建说明](README.md#building-and-qualification) 后，在匹配的
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
任务变量覆盖同名根变量。参数为 `{name}` 时解析整个变量值，例如
`shell: {cmd: '{command}'}`。嵌入式调用可显式调用
`praktor_load_environment`，再调用 `praktor_execute`。

`--task` 选择指定任务及其传递 `depends_on` 依赖，其他任务返回 `skipped`；
未知任务名会在启动子进程前失败。变量值从 `.env`/继承环境读取，任务 `vars`
和根 `variables` 中的显式值优先；环境值不会复制进可变任务状态。
根级 `inputs`、`input_policy` 以及 CLI `--input`/`--var` 都不是合法入口。
原有位置文件参数形式保留用于整份工作流执行，规范调用使用 `--config`/`--task`。

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

服务文件描述通用执行器及参数模板。例如 `services/download.yml`：

```yaml
provider:
  kind: exe
  path: curl
config:
  args: [--disable, --fail, --location, --globoff, --output, '{path}', --url, '{url}']
  timeout_ms: 300000
```

工作流引用该文件，多个任务复用同一个服务名：

```yaml
services:
  packages: services/download.yml
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
时会创建不同实例。当前工作流仍按依赖串行执行。

服务文件路径相对调用方当前工作目录；`config.working_dir` 可设置子进程
目录。服务声明最多 16 个，每个文件最多 1 MiB。当前只接纳 `kind: exe`，
文件缺失、声明无效、服务名未知或参数不匹配都会失败；不会切换其他后端。

`config.args` 最多 256 项。`{name}` 必须占据完整 argv 项，并在任务 `with`
中提供对应标量；多余参数也会拒绝。若传入值本身为 `{variable}`，则从现有
工作流值存储解析，支持任务 `vars`、根 `variables` 及 `.env`/继承环境。执行使用原生 argv，
参数中的空格、引号及 shell 元字符保持为参数内容。每个 job 的参数文本总量
最多 1 MiB，同时 stdout/stderr 合计最多 4096 字节且必须是不含 NUL 的文本。
超限会失败，不截断成成功。

任务输出沿用 `exit_code`、`stdout`、`stderr`。取消后等待实际进程与 IO 排空，
再释放 job 和服务。DLL、嵌入式 TurboScript、WASM provider 尚未接入。
完整工作流示例见 [shared-downloads.yml](examples/shared-downloads.yml)。

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

## SetVariable

```yaml
tasks:
  - name: first
    set_variable: answer
    value: ready
  - name: second
    depends_on: [first]
    set_variable: copied
    from: answer
```

公共运行时还接纳受限的直接 Shell 根。脚本、includes、动态计划、应用层
Parallel 和插件发现尚未接纳；这些输入会明确失败。更广的 schema 或内部
控制节点测试不代表公开入口已支持对应功能。

## 嵌入

公共头文件是 `praktor.h`。调用者提供 YAML 字节、limits 和可选取消回调；
每次调用后的结果均通过 `praktor_release_result` 释放。JSON 在释放前有效。
详见 [C API 文档](docs/C_API.md) 和根 README 中的 C 示例。
