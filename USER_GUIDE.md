# Praktor 使用指南

当前 CLI 和共享 SDK 使用同一套 C11 工作流运行时。工作流来自一份 YAML，
任务按依赖关系串行执行；任务数和保留值由显式 limits 限制。

## 运行

完成根目录 [构建说明](README.md#building-and-qualification) 后，在匹配的
运行时 PATH 环境中执行：

```sh
praktor examples/hello-world.yml
```

CLI 接受一个文件参数，向 stdout 输出 JSON，包含 `workflow_status` 和各
任务的状态、输出。成功返回 0；运行失败返回 1；参数或不支持的输入返回 2。

整个工作流默认限时 30 秒。较长的工作流可在文件参数后设置
`--timeout-ms N`，其中 N 是 1 到 2147483647 之间的毫秒数。例如：

```sh
praktor build.yml --timeout-ms 3600000
```

Shell 节点自己的 `timeout` 仍独立生效；任一预算耗尽都会终止相应执行。

CLI 原生加载根 `dotEnv` 指定的文件，然后执行工作流；不使用外部脚本。
路径相对当前工作目录，文件按顺序加载，后者覆盖前者及已有环境变量，
加载失败返回 2 并停止。根 `variables` 与任务 `vars` 使用现有值存储；
任务变量覆盖同名根变量。参数为 `{name}` 时解析整个变量值，例如
`shell: {cmd: '{command}'}`。嵌入式调用可显式调用
`praktor_load_environment`，再调用 `praktor_execute`。

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
