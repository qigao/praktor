# 构建与运行时排障记录

## 2026-11-23 · Slate Ninja 构建

- Slate 的对象目录按目标名生成。Windows 会让 `Praktor` 与 `praktor`
  写入同一个目录，因此 Slate 使用 `praktor_runtime` 作为动态库目标名。
  LLVM-MinGW 下对照 CMake 的动态库，两边均导出相同的 7 个公共符号。
- `praktor_runtime_c_test` 的 50 ms 环境超时在 Windows 上会误伤普通
  Shell 用例。两套测试注册将该环境值调整为 500 ms，原有阻塞命令仍会触发
  超时，测试同时校验超时错误。显式 50 ms 的覆盖用例各自保留。

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

## 2026-11-13：0.1.0 发布前 SDK 依赖核对

- 安装导出的 `PraktorTargets-release.cmake` 在
  `IMPORTED_LINK_DEPENDENT_LIBRARIES_RELEASE` 中引用 PCRE2 的 shared target，
  而原 `PraktorConfig.cmake` 仅加载 SaltsBase/CMeta。配置中补入
  `find_dependency(PCRE2 10.46 CONFIG COMPONENTS 8BIT)`，使导出 target 的
  依赖由 SDK 自己声明；不依赖消费方提前手动导入 PCRE2。
- 通过独立已构建 Praktor 执行根 `build.yml --task install`：完整 configure、
  build、71/71 CTest、install 通过；安装后的 Config 已包含该依赖查找。
  未创建 consumer/smoke 等独立验证工程，未验证其他平台。
- 0.1.0 原生包使用 CMake 正式安装布局，不捆绑外部 SDK 或第三方 runtime
  DLL；消费方仍需匹配的 SaltsBase、CMeta、vcpkg-cache 及运行时路径。

## 2026-11-13：统一 CMake 配置与匹配 SDK

- 继续只有根 `Praktor` 一个项目。C11、PIC 和测试所需的严格 C++17 统一
  由根目录维护；仅在 `BUILD_TESTS` 开启时启用 C++，生产仍为 C 项目。
  删除目标上的重复标准/PIC、可由源码推导的 C linker 设置、与目标同名的
  OUTPUT_NAME，以及 helper 的自动 VERSION/SOVERSION 和 NO_VERSION 支持。
  保留公开编译要求、符号可见性、target 名称、`Praktor::` namespace 和
  现有生产/测试入口；未增加内部库、独立验证工程或功能开关。
- 原工作区已移除安装 Config 的依赖查找；本次保留该修改，并移除无用途的
  CMakeFindDependencyMacro include。SDK 消费方显式解析所需的 Base、CMeta
  和 PCRE2 target，再加载 Praktor；README 已同步。
- 旧 Base 0.1.6 仍会全局查找 JPEG/libyuv。现复用已验证的 Base 0.1.8、
  Utils 0.1.4 及匹配 Base 0.1.8 的 CMeta 0.1.6 源码 SDK，cache 仍为
  0.1.3、编译器仍为同一 LLVM-MinGW x64。CMeta 源码 ref 为
  `679710f7e3312811efdd92494744a2499286a6a5`；不能把按 Base 0.1.6
  发布的旧 CMeta 0.1.6 二进制当作该组合。原 SDK 目录未覆盖。
- Utils 使用刚发布的原始 Windows LLVM-MinGW TAR，SHA-256 为
  `396c31ba32adcd36fd40d46785223a01fb4c1794e193035e3cae72561d17b665`。
  经已构建 Praktor 的原生 YAML task 调用 tar 解到独立版本目录。
  仅调整本地 `.env` 相关 SDK 根和版本字段，保留凭据及机器设置；示例同步。
  根 find_package 读取已有版本变量进行精确查找，SDK 变更后显式 fresh 配置。
- 独立临时 runner 从旧已安装 CLI/DLL 启动，Praktor 原生加载 `.env` 并
  编排 `build/cmake-cleanup/verify.yml --task install`。该工作流依次执行
  正式 fresh configure、完整 build、CTest 和 install 用户 preset，没有
  用外部解释器加载环境、替换变量或编排构建。旧 SDK PATH 仅供 runner
  自身加载；CMake/CTest 子进程通过用户 preset 选择当前匹配的 SDK。
- 同一新 SDK 组合下，清理前后 553 条编译命令、全部 Ninja 构建命令及
  71 项 CTest 的命令/属性完全一致。配置 trace 只有一个源项目，
  JPEG/libyuv 查找和 cache 项均为零；生成审计见
  `build/cmake-cleanup/generation-audit.json`。
- Windows x64 完整构建、71/71 CTest（13.51 秒，JUnit 无失败/错误/跳过）
  和 install 通过。日志、原生工作流结果、JUnit 和验证回执位于
  `build/cmake-cleanup/`。CTest preset 将相对 JUnit 路径解释为相对 binary
  目录，原报告位于其 `build/cmake-cleanup/ctest.xml`，另复制一份到日志目录。
  安装前缀保持在当前 user preset 的 stage。其他平台和 sanitizer 未验证。

## 2026-11-20：Statechart 的只读 guard

- 将状态图从独立执行体迁移为任务或文档的观察者时，旧 Session 契约即使只包含
  内建 `Assert` 也会要求宿主 leaf 的 tick/halt，导致首次迁移报告
  `statechart: cannot prepare transition guard`。
- 修正 Session 的准入边界：仅含内建 guard 的计划可以省略宿主 leaf 回调，
  普通运行时叶仍需成对提供回调。状态图 guard 禁止写入，并且没有进程 owner。
  复现和验证用 `ctest --preset llvm-mingw-x64-release-user -R praktor_runtime_c_test --output-on-failure`。

## 2026-11-20：模型服务的 JSON 请求与响应

- curl 8.9.1 的 `:json` 变量展开只转义字符串内容。直接把它放在 JSON
  字段值的位置会发出无引号的请求。模型服务改用 `--json` 传完整请求，
  curl 的变量展开只用于认证头及 Gemini 的模型路径。
- 以 `{` 开头且以 `}` 结尾的整个 `with` 参数会被解释为变量引用。
  YAML 的 `|` 保留末尾换行，让完整 JSON 作为字面 argv 参数。Claude 的
  文本块过滤路径用 `ParseJson result: first`，其筛选后取子字段的路径需要 DOM。
- 三种服务用本机 HTTP 回环和虚拟密钥验证请求与状态传递，覆盖正常响应、
  生成截断、空文本、null 文本、HTTP 429 和缺少密钥。离线入口为
  `praktor --config examples/llm/summary-workflow.yml --task confirm_summary`。

## 2026-11-20：构建缓存与安装边界

- CMake 的包版本即使匹配，旧的 `<Package>_DIR` 也可能覆盖新选定的搜索根。
  配置现在校验 SaltsBase、CMeta、SaltsUtils 实际加载的配置文件路径。
  提供 vcpkg triplet 和安装根时，PCRE2 也必须来自选定 triplet 下的共享
  SDK 或当前产品安装。路径错配时提示显式使用 `cmake --fresh`。
- Windows 上的 `tlhelp32.h` 依赖 `windows.h` 先声明的类型。格式化如果
  交换了两者顺序，LLVM-MinGW 会报告 `HANDLE` 和 `DWORD` 未声明。
  保留两个头文件之间的分组可避免排序再次引入这一错误。
- 用独立 Praktor 原生加载 `.env`，并用临时 dotenv 文件逐项覆盖四个 SDK
  根，确认配置均拒绝旧缓存。恢复配置后执行
  `praktor --config build.yml --task install --timeout-ms 3600000`，完整
  构建、71/71 CTest 和 `cmake --install` 通过，安装的 CLI 和 DLL 摘要
  与构建目录中的产物一致。

- `build.yml` 原生加载 `.env`，在 Shell 启动前展开命令里的
  `$PRESET`。`validate_preset` 先校验非空值和名称字符，CMake 校验
  preset 是否存在。安装阶段执行 `cmake --install "build/$PRESET"`。
- DSL 的整值、读取端口和 guard 统一使用 `$name`。Selector 编译输入
  将 sigil 替换为空格以保留诊断位置，并给缓冲区补 NUL 以满足当前
  SaltsBase Selector lexer 的 EOF 哨兵。使用独立副本执行
  `praktor --config build.yml --task install --timeout-ms 3600000`，
  Windows LLVM-MinGW x64 完整构建、71/71 CTest 和安装通过。已用安装
  产物执行设备状态、发布分流、离线模型响应和上游版本检查示例。

## 任务准入与输出结算

- YAML 任务元数据可以描述比运行时更广的字段。已选择任务在创建服务
  或启动 BT 前拒绝未接线的元数据。进程时限与目录由 `shell` 端口
  处理，`timeout` 加同级 `child` 属于 BT 装饰器。
- 公开运行时按直接 Shell 声明的键结算，CLI 的默认输出标签保持
  `stdout`、`stderr`、`exit_code`。任务级 `outputs: [$name]`
  显式选择完整的成功输出集合。复用工作流 owner 的值事务和任务结果
  提交，缺少任一值时整组拒绝。旧版本可使用直接 SetVariable 任务或
  固定进程输出传值，并移除新增的任务 `outputs` 声明。
- Windows LLVM-MinGW x64 使用 `praktor --config build.yml --task install
  --timeout-ms 3600000` 完整构建、71/71 CTest 及安装通过。安装后运行
  离线模型示例，文档 Statechart 在 `task.finished` 读取已提交的
  `$summary`，后继 BT 也能读取该值。两个状态图示例及上游版本检查通过。

## BT 内的进程动作与字符串分支

- 任务级 `tool` 和 BT 内的 `Tool` 使用同一绑定路径。准入按 Session
  节点索引保存绑定，树的串行驱动只持有一个活动 job。正常完成先复制
  结果再释放 job，`timeout` 或 halt 则取消并排空后释放。此顺序允许
  `repeat` 重启同一个叶节点，也允许 `Fallback` 启动另一个服务。
- 字符串 `switch` 以 `cases` 映射的键精确匹配；找不到匹配时才选
  同级 `default`。默认分支由 BT 选择一次，选中分支执行失败时直接
  回传失败状态。原有序列形式继续读取零基整数索引。
- Windows LLVM-MinGW x64 完整构建、71/71 CTest、安装通过。安装后
  运行发布分流、设备状态和离线模型例子。下载示例用本地文件 URL
  验证了直接 Tool 与嵌套 Tool，并核对了两个下载文件的 SHA-256。

## DAG 同组并行与宿主 Statechart 事件

- 根 `parallelism` 控制一次领取的就绪任务数，范围 1 到 16。DAG 领取后
  先检查同组输出键冲突，再由一个手动 CFlow 描述符轮转各个 BT。所有
  Session 与服务 job 关闭、共享 Shell retainer 排空后，owner 用原有事务
  一起提交本组结果。后继未启动时报告 `pending`。
- SDK 的 `praktor_execute_with_events` 通过工作流线程中的同步回调轮询
  文档图和活动任务图。事件先匹配声明并校验类型，再复制进状态图原有
  有界队列。内部 `raise` 和外部事件入口各自校验来源。
- Windows LLVM-MinGW x64 完整构建、71/71 CTest 与安装通过。SDK 测试
  验证了两个作用域、精确 int64、载荷校验与回调错误。安装后运行
  `praktor --config examples/upstream-version-checks.yml --task confirm_compatible`，
  以及设备、发布和离线模型示例。

## 工作流组合准入

- `includes` 的路径从进程工作目录解析。启动前展开 `uses` 的依赖闭包
  和 `dynamic_tasks` 数组，再构建 DAG 并执行 `--task` 选择。遇到
  找不到生成任务的情况，可先核对 JSON 输入及结果中的完整任务名。
  展开不读取本轮任务产生的输出。
- 生成任务和调用入口计入 `max_tasks`。任务库的 YAML 由 snapshot
  持有，以保证实例的 Statechart 声明有效。生成任务使用原有 BT、
  Statechart 生命周期和输出结算。设备示例的汇合任务产生
  `task.finished` 后，文档图进入 `commissioned`。
- 修改 CMake 后先运行 `praktor --config build.yml --task build`
  加载 `.env` 并完成配置，再用 `update-workflow-schema` 生成
  编辑器 schema。直接执行该 CMake 目标触发重新配置时，
  父环境缺少 SDK 变量会导致包查找失败。
- Windows LLVM-MinGW x64 完整构建、71/71 CTest 与安装通过。
  安装产物运行了组合示例的 `confirm` 和 `inspect.device-1`，
  分别验证汇合与生成任务的依赖选择。

## 跨任务 Statechart 信号

- `raise` 增加 `scope: workflow`，接收方为文档图。排查同名变量在两种
  上下文取值不一致时，确认载荷引用由发送任务解析，接收图只从
  `$event.data.<字段>` 读取载荷。文档图其他 guard 字段仍读取工作流值。
- 自定义信号在发送节点返回前提交迁移。需要观察已结算输出的迁移
  继续使用 `task.finished`。选择握手示例的 `confirm` 可将等待方和发送方
  一起纳入运行范围。
- Windows LLVM-MinGW x64 完整构建、71/71 CTest 与安装通过。安装后执行
  `praktor --config examples/statechart-signals.yml --task confirm`，
  同时复核设备状态和组合示例。生成的 Schema 通过握手和组合示例校验。

## DSL 准入与动态任务结果

- Tool 的任务上下文继承工作流值，可能带有前序任务的 `shell_exit_code`。
  若新 Tool 在参数求值或进程启动时失败，只在本次 job 已写入真实退出码时
  发布该字段。既有进程非零退出仍报告实际退出码。
- `dynamic_tasks.collect` 将生成任务的输出保留在任务注册表，汇合时按
  输入顺序读取并写入一个最多 4096 字节的 JSON 数组。生成任务可以在同组
  使用相同输出键。汇合失败沿用原有组结算，已结算的成员结果仍可查看。
- 编辑器校验已同步可执行形状、变量标量和命名规则。用
  `update-workflow-schema` 重新生成 Schema。14 个工作流与任务库文件和
  10 个边界用例通过 Schema 校验。
- Windows LLVM-MinGW x64 完整构建、71/71 CTest 和安装通过。安装产物
  验证设备巡检、跨任务握手，以及两个并行 Command 的 stdout 和退出码汇总。
