# Statechart、BT 和 DAG

| DSL | 持有的状态与职责 |
| --- | --- |
| `tasks`、`depends_on` | DAG 组装任务和依赖。运行时启动就绪任务、提交任务结果，并据此选择后继。 |
| 任务的 `actions` 或简写执行体 | BT Session 执行动作、等待、分支并返回结果。任务级 `command`、`tool` 等简写也下沉到 BT Session。 |
| `statechart` | 记录活动状态、历史、事件和计时器。根据事件与只读 guard 提交状态迁移。 |

任务级 `statechart` 与 `actions` 同级，由单次任务执行持有。文档根的 `statechart` 由单次工作流执行持有，跟踪多个任务共享的业务状态。两者使用相同的状态图语法。即使任务级状态图先到达终态，BT 仍继续运行，并由 BT 结果结算任务。

## 示例

### 设备遥测与诊断

DAG 让 `confirm_device` 等待 `assess_device`。诊断 BT 解析 JSON、模拟上报并等待诊断结果。任务图筛选旧请求、判断是否需要诊断并处理超时。文档图观察诊断任务的生命周期。后继任务读取 `chart.workflow.cleared`。 [设备诊断示例](../examples/statechart.yml)

```sh
praktor --config examples/statechart.yml --task confirm_device
```

默认告警会进入诊断分支并恢复。可把告警移到 `alarms.critical` 验证严重告警，降低 `fan_rpm` 验证复核失败，或把遥测里的 `request_id` 改成 `41` 验证超时。

### 发布路线

BT 发送两个模拟 ACK。任务级 Statechart 根据 `$event.data.attempt_id` 接受当前尝试的 ACK。BT 检查 `$chart.task.accepted`，通过后用 `if` 选择稳定或预览路线，再用字符串 `switch` 选择策略。Canary 分支在 BT 内执行 Shell 动作。 [发布路线示例](../examples/statechart-branching.yml)

```sh
praktor --config examples/statechart-branching.yml --task route_release
```

### 模型响应校验

`validate_summary` 用 BT 检查样例模型响应的完成原因及文本。文档图依据
`task.finished` 提交 `accepted` 或 `rejected`。DAG 让 `confirm_summary`
等待校验成功，确认任务再读取 `chart.workflow.accepted`。 [离线模型工作流](../examples/llm/summary-workflow.yml)

## 结构与迁移

`initial` 和 `states` 必填，`events` 可选。`initial` 引用当前层的直接真实子状态。状态 ID 在一张图内唯一，匹配 `[A-Za-z_][A-Za-z0-9_-]*`。事件名称按点分段匹配同一标识符规则。状态和事件按全名精确匹配，区分大小写。

| 状态 | 字段与行为 |
| --- | --- |
| 原子 | 可定义 `transitions`。空映射声明静止状态。 |
| 复合 | 定义 `states`、`initial` 及可选 `transitions`。同时进入一个直接子状态。 |
| 并行 | 定义 `type: parallel`、`states` 及可选 `transitions`。每个直接子区是复合或并行状态。 |
| 终态 | `type: final`。直接父复合状态可据此完成。顶层终态保留活动状态并停止本图迁移。 |
| 历史 | `type: history`、`mode: shallow` 或 `mode: deep`、`default`。在复合状态下声明，初次恢复使用默认直接子状态。浅历史恢复直接子状态并进入它的默认配置，深历史恢复全部已记录的活动后代。 |

每项迁移必须有 `target`，可选 `guard` 和 `type`。最多选择一种触发方式。

| 触发字段 | 匹配规则 |
| --- | --- |
| `'on': deploy.ack` | 收到同名事件。 |
| `after_ms: 5000` | 源状态本次进入后的单调计时到期，单位毫秒。 |
| `on_done: checking` | 指定的当前复合状态、并行状态或其后代进入完成配置。复合状态进入直接终态即完成，并行状态在所有子区完成时完成。 |
| 省略触发字段 | 每次推进时可检查的无事件迁移。 |

`guard` 使用 `fields` 加 `match` 或 `expr`。未指定条件时视为真，条件为假时继续检查下一条。一次触发的候选迁移从活动叶状态向父状态搜索。更具体的源状态优先于和它冲突的祖先状态。互不冲突的候选共享同一配置快照并一起提交。

默认使用 `type: external`，退出迁移边界内的活动后代并重新进入目标所需层级。外部自迁移重新计时。`type: internal` 要求复合源状态及其真后代目标，保留源状态和源状态计时。同一状态、相同延时的条目共享一次计时通知。首次到期时按声明顺序评估，离开状态会取消本轮计时。

## 事件和状态读取

| 事件 | 来源及可读字段 |
| --- | --- |
| `task.started` | 运行时在首个 BT tick 前发出。载荷包含 `task` 和 `status: running`。 |
| `task.finished` | 整个就绪组结算后、下一次 DAG 调度前，运行时按组内声明顺序为每个已启动任务发出。载荷包含任务名 `task` 和组的结算状态 `status`，取值为 `success`、`failed`、`cancelled` 或 `deadline`。 |
| 自定义 `source: internal` | BT 使用 `raise` 发送。省略 `scope` 或写 `scope: task` 时发给当前任务图。写 `scope: workflow` 时发给文档图。 |

运行时事件内置，任务图只接收所属任务的生命周期，文档图接收本次执行实际启动的任务生命周期。命名事件 guard 通过 `$event.name` 和 `$event.data.<字段>` 读取本次事件。自定义事件 `fields` 支持 `text`、`int64` 和 `bool`。BT 可使用 `raise: {event: deploy.ack, with: {attempt_id: $attempt_id}}`。字段值支持 YAML 标量和完整 `$name` 引用，按声明类型校验。

同组的 `task.finished` 携带相同的 `status`。单个 BT 即使已成功，
所在组执行失败或输出提交失败时仍会收到 `failed`。收到 `success` 时，
本组输出已经提交。

BT guard 可通过 `$chart.task.<状态ID>` 和 `$chart.workflow.<状态ID>` 查询状态成员，值为 `1`（活动）或 `0`（不活动）。ID 含连字符时用 Selector `match`，例如 `$chart.task.waiting-ack == "1"`。未知作用域或 ID 视为字段缺失。状态图的 guard 可以读取自身可见的状态及上下文。文档图读取工作流变量和已发布输出，任务图读取任务当前变量。计时器由工作流驱动推进，BT 可用 `wait_until` 等待目标状态。

### 跨任务状态信号

`raise: {scope: workflow, event: sensor.ready, with: {reading: $reading}}`
将事件发送给文档图。文档图须声明同名 `source: internal` 事件及载荷字段。
变量在发送任务的上下文中求值，文档图的 guard 可通过
`$event.data.reading` 读取本次事件值。发送成功后，同一任务的后续节点
和其他已就绪任务可以读取文档图提交后的状态。

工作流线程解析发送方的借用参数，复制并校验载荷后交给接收图的现有队列。
同一驱动按 BT tick 实际顺序投递，发送节点在接收图稳定后返回。已到终态
的图消费并释放合法载荷，保持终态。载荷、队列和稳定化共用下述容量上限。
类型校验或队列失败走原有执行错误路径，图销毁时释放待处理载荷。
该迁移在发送节点执行期间提交，可以早于所在 DAG 就绪组提交输出。
需要等待整个组的成功输出时，使用 `task.finished`。

文档图是同一运行中的共享状态。若信号尚未发出，发送方须与等待方同组
执行。DAG 按任务顺序领取就绪任务，每组最多 `parallelism` 项，整组结束后
才领取下一组。即使发送方已就绪，若被排进后续组，仍可能等到等待方超时
才结束当前组。也可以用 `depends_on` 让发送方先完成，再启动等待方。
旧版 DSL 可使用任务自己的 `raise`，并用 `task.finished` 和已发布输出
驱动文档图迁移。 [传感器握手示例](../examples/statechart-signals.yml)

状态图拒绝 `on_entry`、`on_exit`、`do`、迁移 `actions` 和终态 `result`。文档状态图的生命周期以一次工作流运行为界。

### 宿主事件

嵌入式调用通过 `praktor_execute_with_events` 提供同步轮询回调，事件声明使用
`source: external`。每次轮询指定一个活动目标，文档图的任务参数为空，
任务图收到已启动任务的名称。一次回调最多提供一个事件，无事件时立即返回。
工作流所有者在 BT tick 前校验并复制数据，使用现有状态图队列提交迁移。
回调从工作流线程调用，载荷借用至下次回调或执行返回，由宿主管理其生产者。
队列以及字段和字节上限与内部事件共用。无效事件或回调错误会终止执行。
已到终态的图停止轮询。 [公开接口](../praktor/include/praktor.h)

普通 CLI 和原有执行入口在准入时拒绝外部事件声明。迁移到旧版本时可由
任务 BT 经 `raise` 携带内部事件，并改用对应的内部事件声明。

### 常驻分流

宿主通过 RulesForge 的 C ABI 运行数据规则。RFL 匹配消息与事实，命中后可通过 `CALL` 调用
ToadScript 函数，计算 `DROP`、`FORWARD` 和逻辑目的地。RFL 把结果和原事件 ID 写入决策事实。
宿主持有原消息、发布代际和有界队列，校验决策后完成 MQTT 确认及下游投递。
涉及任务依赖、服务调用或流程状态时，宿主把规则结果作为输入交给 Praktor。 [RFL 调用与结果归属](https://github.com/qigao/RulesForge/blob/464501ce52dc33c6548c835fe9fe167e9610de65/docs/SCRIPT_PROGRAM.md#rules-and-ownership)

ToadScript `CALL` 通过 RulesForge `Program` 创建的 `StatefulSession` 执行。宿主在同一个线程驱动会话，
跨消息状态放在有界的会话事实中。需要事件时间、水位线和迟到处理时，宿主可以先用
`ContinuousSession` 计算数据特征，再交给 `Program` 执行分流。连续会话的批次确认由宿主按顺序处理。
该会话的 `max_replay_steps` 是累计上限，事件过期和批次确认都不会回收重放步骤。部署前需验证会话恢复与交接。
[Program 和连续会话](https://github.com/qigao/RulesForge/blob/464501ce52dc33c6548c835fe9fe167e9610de65/docs/USER_GUIDE.md#choose-a-session) · [连续会话限制](https://github.com/qigao/RulesForge/blob/464501ce52dc33c6548c835fe9fe167e9610de65/docs/CONTINUOUS_RULE_ENGINE_DESIGN.md#current-limits)

### 流程级逐事件决策计划

[#89](https://github.com/qigao/praktor/issues/89) 规划给同一次长驻状态图执行增加相关决策回调。
当前 SDK 提供事件轮询和最终执行结果。计划中的宿主回调将原样返回关联 ID 和发布代际，
并对每个已接纳事件给出一次结算。宿主在提供事件前预留决策槽。Praktor 在结算前暂停轮询该图，
并在提交 `FORWARD` 前校验宿主为该代际批准的逻辑目的地。

工作流线程推进状态图和单次事件的处理体。事件未命中允许的迁移时结算 `DROP`。
命中迁移后，处理体读取本次事件和捕获的状态，只暂存本轮决策。
成功结束后由工作流线程提交。分支中止、取消、期限或错误使本轮暂存结果作废，
排空活动工作后结算尚未完成的事件。Statechart 状态跨事件保留，处理体的临时值按事件重置，
流程级逐事件能力不受普通 BT 循环的迭代预算限制。验收包括同一张图的关联 `DROP` 和 `FORWARD`、
超过默认 BT 迭代上限的连续事件，以及取消时的唯一结算。

## 迁移与验证

旧的独立状态图执行体由顶层终态结算任务。每个任务统一进入 BT Session 后，任务只从执行体取得结果，状态迁移只提交状态图数据。依赖图继续负责决定哪些任务可以启动，BT 按业务需要显式等待或读取状态。

把原先放在状态图里的动作移到任务 `actions`。需要状态信号时，从 BT 发出命名事件，再用 `if`、`assert` 或 `wait_until` 读取状态。跨任务顺序放入 `depends_on`，任务的成败由 BT 节点给出。旧版本只接受独立状态图执行体。若回滚 Praktor 版本，同步恢复原 YAML，并保留新检查任务的独立依赖关系。

继续复用现有 guard 编译和求值。状态图中的 guard 只有只读上下文与内建谓词，任务 Session 持有动作执行权。两种作用域都由同一工作流线程推进。状态图持有已入队的事件副本，消费时仅向当前 guard 借出字段，退出时释放未处理事件。每张图最多 128 个状态、16 层嵌套、512 条迁移和 128 个自定义事件。每个事件最多 32 个字段，值总计最多 8 KiB。队列最多 64 项，每轮稳定化最多 256 个迁移步骤。准入或求值错误带诊断停止执行。正式回归覆盖独立任务准入、事件及时间判定、文档状态发布顺序和 BT 任务结算。
