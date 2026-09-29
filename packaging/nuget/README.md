# Praktor.Native

Praktor 的 script-enabled native SDK，由 `qigao/praktor` 自己构建、验证和发布。

正式依赖固定为：

- `Salts.Native 1.8.3`
- `SaltsUtils.Native 4.1.3`
- `CHttp.Native 1.1.4`
- `TurboScript.Native 3.0.3`

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
find_package(Praktor 0.4.0 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Praktor::Praktor)
```

PR/master 只做 qualification。正式 package 由匹配项目版本的 `v0.4.0` tag 发布。
