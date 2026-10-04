# Praktor.Native

Praktor 的 script-enabled native SDK，由 `qigao/praktor` 自己构建、验证和发布。

正式依赖不在 Praktor 中固定版本。NuGet restore 使用 floating dependency，
由发布/消费时可用的最新兼容 Native SDK 决定：

- `Salts.Native`
- `SaltsUtils.Native`
- `CHttp.Native`
- `TurboScript.Native`

其中 TurboScript 负责 Praktor 的脚本执行能力（MIR interpreter/JIT 和 native modules）。
`ENABLE_SCRIPT_ENGINE=OFF` 的 core-only Praktor 不依赖 TurboScript。

SDK 平台：

- `sdk/linux-x64`
- `sdk/macos-arm64`
- `sdk/android-arm64-v8a`

第三方 C/C++ 依赖由 Praktor 仓库根目录的 `vcpkg.json` 定义；
`qigao/vcpkg-cache` 只提供共享 vcpkg infrastructure/cache 与 re2c host tool。

消费者还原 NuGet graph 后设置 `SALTS_ROOT`、`SALTS_UTILS_ROOT`、`CHTTP_ROOT`、
`TURBOSCRIPT_ROOT` 和 `PRAKTOR_ROOT`，再使用：

```cmake
find_package(Praktor CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Praktor::Praktor)
```

Praktor 0.4.4 起，Native SDK 公开 reviewed `WorkflowPlan` HostTool ABI（`PRAKTOR_CAPABILITY_HOST_TOOL` / ABI 2.5）。
HostTool 只提供 backend-neutral validate/invoke boundary；Praktor.Native 不依赖 TurboAgent、TurboWasm 或 RuntimeTools。

Praktor 0.4.5 起，Native SDK 进一步公开 ABI 2.6 的
`PRAKTOR_CAPABILITY_INLINE_WORKFLOW_PLAN` / `praktor_compile_workflow_inline()`。
该入口面向 compiler-generated finite DAG：source bytes 由 plan 拷贝拥有，不创建临时文件，
第一版只接受无外部依赖的 HostTool-only DAG，并对 include/uses/dotenv/script/process/
dynamic-each/trigger 等路径 fail-closed。

Praktor 0.4.6 起，reviewed `tool:` HostTool task 支持有限
`retries.count`。count 表示首次调用之后允许的额外 attempt，范围为
`0..PRAKTOR_HOST_TOOL_MAX_RETRIES`（当前 1024）。只有 HostToolStatus::Failed
可以进入下一 attempt；NotFound/Denied/Cancelled/TimedOut 均为终止状态。
同一个 ExecutionControl 在每次 attempt 前重新检查，因此 retry 不会刷新 deadline，
也不会重跑依赖或整个 DAG。retry 安全性仍由 embedding compiler/host 负责 admission。

PR 只做 qualification；不在 master push 上重复跑整套三平台构建。正式 package 只由与当前项目版本匹配的 release tag 发布。
