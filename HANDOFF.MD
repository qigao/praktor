# 构建与运行时排障记录

## 2026-11-13：split SDK 与完整构建

- 构建方向为完整组装所有生产模块、CLI 和共享 SDK；已移除 foundation
  profile、`PRAKTOR_BUILD_RUNTIME` 和可选旧 Process backend 分支。
  `CMakeOptions.cmake` 声明 `BUILD_TESTS`，模块消费此开关。
- SDK 通过 `SALTS_BASE_ROOT`、`CMETA_ROOT`、`SALTS_UTILS_ROOT` 精确查找。
  两棵 vcpkg 安装树的搜索前缀与运行时路径归 `presets/UserEnvironments.json`。
  SDK 根不加入 `CMAKE_PREFIX_PATH`；CMake 不自动加载 `.env`。
- 本轮使用 SaltsBase 0.1.6、CMeta 0.1.6、SaltsUtils 0.1.3、vcpkg-cache
  0.1.3 的 Windows LLVM-MinGW payload，编译器 Clang 23.1.4。
  SDK/编译器/preset 变更后使用 `cmake --fresh --preset ...`；旧 cache
  曾保留空的 Release flags，fresh 后恢复 `-O3 -DNDEBUG`。
- TRE 原已由根 manifest 声明；共享 payload 不包含它。本轮按同一
  `qigao-x64-windows-llvm-mingw` triplet 安装到项目 `vcpkg_installed`，
  写入本机二进制缓存。没有修改共享 SDK payload 或第三方源码。

## 进程清理 owner 的迁移边界

旧 `cflow_process` / `cflow_process_retainer` 未由当前 split SDK 导出。
进程执行改用 `process_async`；Configurator 位于 `CMeta::ACE`。
Core 的 `process_retainer_t` 接收 raw `process_t`，无法直接接管 opaque
async owner 的管道与回调。Praktor 的有界 retainer 因此保留完整 async
handle 的地址及缓冲区，等待 native quiescence、destroy 后再释放。
详细状态和失败协议见 `docs/C11_RUNTIME_RESET.md`。

## 成功 Command 返回资源错误

- 触发：公共 C API / CLI 执行 `command: echo ...`，进程成功退出，
  最终却返回 `PRAKTOR_RUNTIME_RESOURCE` / `SALTS_EINVAL`。
- 原因：Command lowering 绑定 `stdout`，运行时发布却读取 Shell 默认
  `shell_output`。缺少端口使发布失败，与进程执行结果无关。
- 修复：按 task kind 选择源端口；导出的 JSON 字段仍为 `stdout`。
  `praktor_c_api_test` 和 `praktor_cli_test` 已覆盖真实执行及输出。

## 故障注入测试计数错误

- 触发：`orch_tree_fault_test` 在第一个注入点报告存活分配不为零。
- 原因：AST 已从 Vec 改成 owned arrays，测试仍拦截 `vec_push`；
  `realloc` 分配未计入，释放数组时计数发生下溢。
- 修复：拦截实际 `realloc` 边界。新分配增加 owner，扩容失败保留原
  owner；扩容成功不增加 owner 数。保留逐分配失败回滚测试。

## 实际验证

在已加载 `.env` 相关路径的 VS developer 环境中执行：

```text
cmake --fresh --preset llvm-mingw-x64-release-user
cmake --build --preset llvm-mingw-x64-release-user
ctest --preset llvm-mingw-x64-release-user
cmake --build --preset install-llvm-mingw-x64-release-user
```

最终完整构建成功，69/69 CTest 通过；CLI、共享库、头文件和 CMake package
安装到 `build/llvm-mingw-x64-release-user/stage`。未执行其他平台、sanitizer
或 CI，不能把本轮本地验证当作跨平台或 CI 已通过。

## 2026-11-13：旧 CI 与兼容接口清理

- 删除旧 post-merge workflow、SDK 下载/恢复 action、source-policy 审计和
  NuGet 发布包装；共享依赖仍由当前用户 preset 和 vcpkg 缓存配置接入。
- 删除独立 consumer/smoke/standalone 验证工程、失效的 C++/TurboScript
  文档与示例、旧 parser generator helper 和 YAML target shim。
  保留根构建图中的正式模块测试、现有 C ABI v3 和 `examples/hello-world.yml`。
- Session 删除无人使用的 `compatibility_*` 字段及备用 owner/dispatch；
  初始化必须提供完整主 Host value 和 leaf tick/halt 契约。正式 Session
  测试覆盖缺失契约在任何 callback 前失败，既有 native Shell 路径保持。
- 移除未接入实现的 sanitizer/benchmark 用户 preset，保留现有平台配置
  所需的 Debug/Release 基础 preset。其他平台及 sanitizer 未验证。

清理后执行 configure、完整 build、CTest 和 install 用户 preset：构建成功，
69/69 CTest 通过，安装成功。`git diff --check` 通过；CodeGraph 已同步。

## 2026-11-13：模块测试目录与遗漏注册

- Praktor 的 17 份测试注册从生产 `src` 迁入已有 `test`；runtime 和测试
  子目录由 Praktor 模块添加。Actions 的 23 份测试源码归 `praktor/test/actions/tests`，
  process 与 schema 的注册归各自已有 `test`。顶层只组装模块。
- Actions 不再加载 process 模块的 target 文件；依赖 target 由所属模块
  声明一次。YAML action 测试链接正式 process/output target，停止重复
  编译依赖实现。故障注入 TU 仍保留明确的实现替换边界。
- 发现 `output_scan_test.c` 原有两项行为测试未注册，因此旧 69 项通过
  不能证明输出扫描器的独立用例被执行。现已接入 `actions_output_scan_test`，
  覆盖二进制行/键值扫描、容量预检和 JSON 精确边界。
- 修正文档中仍称当前模块仅供 qualification、另有生产 C++ owner 的
  失效描述，保留已有解析兼容语义与应用层功能接纳边界。

同一 Windows x64 用户 preset 完整 configure/build、70/70 CTest 和 install
通过。`sdk` 源目录及旧构建目录已移除；公共 C ABI 与安装路径未改变。

## 2026-11-13：按目录管理 CMake 工程

- 44 份 target/test `.cmake` 碎片已归并到所属目录的 `CMakeLists.txt`。
  父模块使用 `add_subdirectory()` 组装；通用 helper 仍在 `cmake/`，
  根 `CMakeOptions.cmake` 仍是项目选项入口。现有 target 名称保持。
- core target 现在导出自身目录作为 include 根。首次完整构建暴露
  `native_host.c` 的旧 `core/output_scan.h` 引用；改为消费 target 导出的
  `output_scan.h`，YAML action 测试同步使用同一入口。
- behavior-tree 迁入 `shared` 后，源码头文件已为 `behavior_tree.h`，
  安装规则仍指向不存在的 `include/salts`。现按实际文件安装到
  `include/behavior_tree.h`，同步其文档，并清除本机 stage 的旧头文件。

修正后完整 build、70/70 CTest 和正常 install 成功，文档本地链接和
`git diff --check` 通过。其他平台与 sanitizer 未验证。

## 2026-11-13：Praktor 自身构建工作流

- 根 `build.yml` 使用现有 Windows x64 Release user preset，按 prepare、
  configure、build、test、install 串行执行，保留完整 build/CTest graph。
  SDK/cache 路径与 VS developer 环境由父进程提供，工作流不加载 `.env`。
- CLI 原固定使用默认 30 秒工作流预算。新增可选的文件后参数
  `--timeout-ms N`，直接传给现有 runtime limits；默认值与 C ABI 不变。
  CLI 正式测试覆盖正数上界、缺失/非法/非正数/溢出参数和真实超时取消。
- 每个 Shell 仍有独立的 timeout。完整命令输出重定向到 `build/logs`
  下的四份阶段日志，以免超过当前任务输出捕获的 4 KiB 边界；保留
  命令退出码，失败停止后续依赖任务。
- Windows 自举时，运行中的 exe/DLL 不能作为当前 build/install 的覆盖
  目标。运行器须独立于本仓库 build/stage，不能直接用它们来重建自身。

验证先通过正常 user preset 构建并安装更新后的 CLI，再从 stage 将本项目
两个运行文件放入独立临时 runner 目录，依赖 DLL 沿匹配的 SDK/cache PATH
加载。实际执行 `praktor build.yml --timeout-ms 3600000`：五阶段均成功，
CTest 70/70 通过，install 成功；本次为增量构建。临时 runner 已删除。
其他平台与全量冷构建未在本轮验证。

## 2026-11-13：Praktor 生产与测试两个入口

- 按最新目录规则，`praktor/CMakeLists.txt` 集中管理本模块生产目标，
  `praktor/test/CMakeLists.txt` 独立管理全部模块测试（包括 actions 测试）。
  其余 11 个下级 CMake 入口已移除，源码、数据和 fixture 保持原归属。
- 合并时将目录相对路径改为明确的模块路径；保留 target 名称、完整构建图
  及 `BUILD_TESTS` 测试入口。其他模块不受该收敛规则影响。
- 回归发现目录重命名污染了 YAML `actions` 关键字，造成元数据白名单
  中的名字与长度不一致，5 项测试失败。已恢复语法字段与相关测试输入，
  `praktor/actions` 的物理目录不变。随后完整 build、70/70 CTest、install
  均通过。
- 构建编排规则已明确：Praktor 原生处理 `.env`/vars，网络下载用 curl，
  CMake 只负责构建；不引入 Python/PowerShell helper。包恢复的原生编排
  尚未完成，当前 `build.yml` 仍以已准备好的依赖为前提；移除了未实现的
  sync task 引用，不能将此前五阶段成功当作发布包同步已验证。

## 2026-11-13：单一动态库与独立 CLI

- 删除 Praktor 模块 33 个内部 STATIC target，生产代码统一编译到
  `Praktor` SHARED target，公共入口仅为 `include/praktor.h`。
  CLI 目标移入 `app/CMakeLists.txt`，由 `app/main.c` 构建并只链接动态库。
- 公共值类型从私有 runtime 头移入 `praktor.h`，布局和既有 C ABI v3
  执行入口保持。原 CLI 的 dotenv 预处理移入新增的显式公开入口
  `praktor_load_environment`；私有 runtime 头不再安装。
- 白盒测试在原测试 executable 中直接编译所需源码，不新增 STATIC
  或 OBJECT 支持 target，也不为测试导出私有符号；故障注入 TU 不重复
  编译被替换实现。测试构建因此需要编译更多实现副本。
- 全部代码进入动态库后，完整链接暴露查询模块对 XML、CSV、CYaml JSON
  adapter 的依赖，已显式链接对应已安装 SDK target。
- Windows x64 完整构建、70/70 CTest、install 通过。公共 C API 测试覆盖
  新入口，CLI 测试覆盖真实 dotenv、根变量与任务变量覆盖。COFF 导出表
  仅含 `praktor.h` 的 5 个公开函数。其他平台未验证。

## 2026-11-13：Actions 目录与符号归一

- 13 个 Actions 头文件移入 `praktor/include/actions`，14 个实现移入
  `praktor/src/actions`，统一通过 `actions/<header>.h` 引用。移除已空的
  旧 Actions 目录；测试沿用 `praktor/test/actions`，修正其旧 CMake 路径。
- 内部类型、函数、宏和 Actions 测试 target 删除 `c11` 命名标签，
  所有调用点同步改名；C11 语言标准与公开动态库入口保持不变。
  白盒故障注入测试继续直接包含对应实现，未重复编译被替换模块。
- Actions 说明集中到 `docs/ACTIONS.md` 和 `docs/ACTIONS_RUNTIME.md`，
  其余架构文档去掉文件名中的 `C11_`，同步修正引用。
- `llvm-mingw-x64-release-user` 完整 configure/build、70/70 CTest、
  install 通过。构建与测试日志位于 `build/logs/actions-*.log`。

## 2026-11-13：独立 YAML 服务与通用执行复用

- 工作流 `services` 将名称映射到独立 YAML 文件；任务沿用 `tool`/`with`。
  服务文件仅声明执行器、路径、argv 模板、超时及工作目录，下载等业务含义
  留在 YAML。新增运行链路没有 Download/Curl 专用接口或命令目录。
- 复用 CMeta Interface/DataDesc、Configurator 的显式 provider selection、
  process driver、原生值存储和 TaskRegistry；每次工作流/服务名对应一个
  实例，每次任务激活持有独立 job。声明与绑定预检完成后才允许任务副作用；
  取消必须完成 IO 排空和 job release，再停止服务和释放声明。
- Snapshot 只保留服务文件引用；执行时读取普通服务文件，路径相对调用方
  当前工作目录。最多 16 个服务，每个文件 1 MiB；argv 最多 256 项、
  合计 1 MiB，job stdout/stderr 合计最多 4096 字节且拒绝嵌入 NUL。
  当前只接纳 exe，DLL/嵌入式 TurboScript/WASM 均明确拒绝。
- 原生 argv 模式必须将 `process_options_init` 默认的同步 capture flags
  清零；它们与 `process_async` 自有 stdio 管道冲突，会使 admission 立即
  返回 EINVAL。测试需确认真实进程准入，避免将准入失败误当超时或取消通过。
- 正式 `praktor_service_test` 包含 13 个用例，覆盖共享实例、独立 job、
  真实 curl 的两个本地 file:// 传输、dotenv/vars、失败预检、取消、超时及
  输出边界。取消测试使用独立长预算，避免被短超时用例抢先终止。
- Windows x64 完整 configure/build、71/71 CTest、schema 生成、install
  通过，DLL 仍仅导出原 5 个公开函数。日志为 `build/logs/provider-*.log`。
  远程发布包恢复及 `build.yml` 包同步尚未接入，其他平台未验证。

## 2026-11-13：TRE 替换为共享 PCRE2

- 根 manifest 和共享 registry 已选择 `pcre2`；Praktor 改为必需查找
  `PCRE2 10.46` 的 `8BIT` component，直接链接 `PCRE2::8BIT`。实际
  `PCRE2_DIR` 来自共享 vcpkg-cache 的 LLVM-MinGW triplet，没有复制、
  重编译依赖，也未修改用户的依赖版本选择。
- 适配层及测试更名为 `regex_pcre2.*`，`praktor_regex_*` 函数、状态和
  span 入口保持。按用户确认使用 PCRE2 原生 leftmost-first 语义，支持
  lookaround、lazy quantifier 和 backreference，不实现 POSIX 兼容路径。
- 每个表达式限制 64 个捕获、64 KiB 模式文本、1 MiB 编译代码；解释匹配
  限制为 match 100000、depth 256、heap 1024 KiB。三类运行时耗尽统一
  映射为既有 LIMIT/ENOBUFS，保持 Native Host 的失败输出原子性。
- match-data 缓存属于表达式 owner，同一 handle 的匹配和销毁需串行。
  PCRE2 可在匹配时分配/增长有界 heap frames，因此不再声称 cache hit
  完全无分配；没有启用 JIT。显式长度、嵌入 NUL 和 byte span 均有测试。
- 新正则套件 10 个用例及 Native Host 测试覆盖原生语义、可选捕获重用、
  UTF byte offsets、资源限额及失败输出保持。Windows x64 完整 build、
  71/71 CTest、install 通过；DLL 直接导入 `libpcre2-8.dll`，不导入 TRE。
  日志为 `build/logs/pcre2-*.log`。其他平台与 sanitizer 未验证。

## 2026-11-13：显式任务选择与环境变量入口

- 规范 CLI 为 `praktor --config build.yml --task build`；选择目标任务及其
  传递依赖，其余任务保留原始索引并输出 `skipped`。未知任务和无效参数
  在启动任务前失败；原位置参数形式仍保留已有全任务行为。
- 根级 `inputs` / `input_policy` 从元数据允许列表、Schema 和文档移除；
  不提供 `--input` / `--var`。动作读取变量时按任务、根、环境的顺序查询，
  环境值不写回 mutable value store；dotenv 仍由调用方显式加载。
- DAG 的 `task_count` 必须始终是原图大小，作为 Registry 和 owner settlement
  的索引域；完成与静止判定只统计选中任务，不能用选中数量替换索引域。
  正式 DAG 测试覆盖重复依赖边、非连续选中索引和未知任务的输出保持。
- 新增公开 `praktor_execute_task`，复用原执行路径与 request/result 布局；
  原 `praktor_execute` 保留全任务语义。当前 DLL 有 6 个公开入口，未增加
  版本化结构或并行调度器。
- TurboAgent 的本地 `build.yml` 同步改为规范任务结构；阶段命令与 PRESET
  放入 `.env` / `.env.example`，复用已有开发环境包装器。已有机器配置和
  凭据保留；`sdk-build.yml` 与共享旧工作流尚未迁移，文档已标明边界。
- Windows x64 完整 build、71/71 CTest、install 通过，日志为
  `build/logs/task-*.log`。当前 CLI 对 TurboAgent 的 `--task prepare` 实测
  成功，其余任务均为 skipped；未执行 TurboAgent 本体 configure/build。

## 2026-11-13：Shell/Command 默认配置逐字段继承

- 每次执行在启动 provider/任务前解析默认配置：Session 内置值
  （30000 毫秒、stream_output=true）→ 继承环境的 `PRAKTOR_TIMEOUT_MS` /
  `PRAKTOR_STREAM_OUTPUT` → 文档 `defaults`。Session 中的显式 Shell 参数
  逐字段覆盖结果；缺失才继承，false 不表示缺失。Command 降低后的 Shell
  也使用相同配置，不向可变值存储或共享 provider 实例写入默认值。
- 所有已提供的环境/文档值都需有效，即使会被后续层覆盖。timeout 采用
  1..2147483647 整数毫秒；环境开关只接受 true/false。此前仅解析而未使用
  的 `defaults.timeout: 30s` 需改成 30000，Schema 和迁移说明已同步。
  resolver 是显式输入的纯覆盖函数，失败保留原输出；环境仍由 Praktor
  原生 dotenv 加载，执行层只读取。公开 request/result/limits 布局未改。
- 工作流总 deadline、独立服务 YAML 的 `config.timeout_ms` 保持各自契约。
  stream_output 控制已有 emission hook，不影响捕获 stdout/stderr；回调
  包含命令提示及输出块，启用时不能断言固定调用次数。CLI 仍输出最终 JSON。
- 正式测试覆盖环境/文档/节点超时优先级、独立总 deadline、显式 false、
  开关继承、关闭回调后保留输出、无效/溢出输入及 CLI 原生 dotenv 失败。
  7/7 定向 CTest、71/71 完整 CTest 通过；Schema 已通过正式生成目标更新。
- 用独立已构建 CLI 实测根 `build.yml --task install --timeout-ms 3600000`，
  prepare/configure/build/test/install 全部成功。根文件已把公共配置移到
  defaults，只保留任务特有时限；日志为 `build/logs/defaults-local-build-yml.*`
  和 `build/logs/{configure,build,test,install}.log`。仅验证 Windows x64。
