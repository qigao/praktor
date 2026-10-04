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

同一 0.4.5 Native SDK 还发布 reviewed HostTool 的有限 retry contract：
`retries.count` 表示首次 invocation 之后允许的额外尝试次数，公开上限为
`PRAKTOR_HOST_TOOL_MAX_RETRIES = 1024`。只有 `PRAKTOR_HOST_TOOL_FAILED`
可重试；NotFound / Denied / Cancelled / TimedOut 都是终态。Praktor 只调度
compiler 已批准的有限尝试，不判断 tool 的 idempotency 安全性。

PR 只做 qualification；不在 master push 上重复跑整套三平台构建。正式 package 只由与当前项目版本匹配的 release tag 发布。
