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
  子目录由 Praktor 模块添加。Actions 的 23 份测试源码归 `actions/tests`，
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
