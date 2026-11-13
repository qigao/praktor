# Praktor — native C11 workflow runtime

Praktor is a **C11** YAML workflow runtime with a shared SDK exposing **C ABI v3**. The CLI and embedded API use the same bounded runtime, CMeta component lifecycle and SaltsBase process I/O.

**Current qualification:** the Windows x64 LLVM-MinGW full build, 71 CTest entries and installation have passed locally. Cross-platform and sanitizer qualification remains separate. Build and test commands below are the current entrypoints.

## Runtime architecture

- **Salts CFlow:** the only bounded run/cancel/finalize execution mechanism. The application Owner serializes DAG claims and task-status publication; CFlow workers never own the DAG or root value store.
- **CMeta:** immutable configuration Reflection, exact-signature Interceptor admission, and lexical cleanup for operations with a no-fail release contract. Fallible close, flush, and Process retirement remain explicitly checked.
- **CMeta ACE Configurator:** one static provider's dependency and lifecycle authority. Dynamic providers, when implemented, must use the canonical **ServiceHost/Plugin** generation and scope mechanisms; no private loader is supplied.
- **Praktor BehaviorTree + SaltsBase Process/NativeIO:** canonical C11 Session/Actions use `process_async` for OS subprocess and I/O ownership. Praktor's bounded retainer keeps closing async owners and their buffers alive until native drain completes, before outputs are published.
- **C11 YAML Snapshot, DAG and TaskRegistry:** immutable plan input with bounded task admission and typed, owner-only output publication.

Production code is C11. The shared/behavior-tree tests also compile its public header from C++17. The runtime is **not** a translation of the former C++ class hierarchy.

## Current admitted subset

The initial runtime is a bounded **single-owner** executor (maximum 64 tasks). It accepts self-contained YAML workflows composed of supported direct SetVariable, Shell, and canonical single-Command roots. Unsupported scripts, includes, unqualified nodes, file-based WorkflowPlan capabilities, dynamic tasks, global plugin discovery and application-level Parallel/multi-worker execution **fail closed**. A successful lower-level CFlow/Process test does not enable a missing application feature.

## CLI

```sh
praktor --config workflow.yml --task build
```

The C-only CLI reads one YAML file, executes the admitted subset and returns canonical JSON with workflow/task states. This is not the former subcommand-based CLI.

## Embedded SDK (C ABI v3)

```c
#include <praktor.h>

const char source[] =
    "tasks:\n  - name: first\n    set_variable: result\n    value: ready\n";
praktor_execute_request request = {0};
praktor_runtime_result result = {0};
request.yaml = source;
request.yaml_size = sizeof(source) - 1u;
request.limits = praktor_default_limits();
praktor_runtime_status status = praktor_execute(&request, &result);
/* result.json/result.json_size are valid until release. */
praktor_release_result(&result);
```

See [the C API guide](docs/C_API.md) for ownership and result-release requirements.

The public header is `praktor/include/praktor.h`. All Praktor implementation
sources compile into one shared `Praktor` target. The CLI lives in `app/main.c`,
is built by `app/CMakeLists.txt`, and links only that shared library. Tests and fixtures
live in `praktor/test`, and runnable workflows live in `examples`.
Actions headers and implementations live in `praktor/include/actions` and
`praktor/src/actions`; see [the Actions guide](docs/ACTIONS.md).
Reusable `tool` tasks reference independent service YAML files through the root
`services` map. Configurator shares one generic execution instance per name;
executable selection and argv templates belong to YAML. See [the user guide](USER_GUIDE.md)
and [the curl service declaration](examples/services/download.yml).

Each module adds its own `test` or `tests` directory when `BUILD_TESTS` is
enabled. Actions tests live in `praktor/test/actions`, process tests in
`shared/process/test`, and schema-tool tests in `shared/schema/test`.
The Praktor shared target is declared only in `praktor/CMakeLists.txt`.
Its independent `praktor/test/CMakeLists.txt` registers every Praktor test,
including those whose sources remain in `praktor/test/actions`. Other
Praktor subdirectories contain no other CMake project files. Internal tests
compile the required private implementation sources; fault-injection translation
units retain their test hooks. Public C API and CLI tests exercise the shared
library. This keeps internal symbols outside the installed public ABI.
Shared modules retain their own CMake entrypoints. Common helpers stay in
`cmake/`, and `CMakeOptions.cmake` remains the project option entrypoint.

## Building and qualification

The complete build consumes installed **SaltsBase**, **CMeta** and **SaltsUtils** packages from explicit `SALTS_BASE_ROOT`, `CMETA_ROOT` and `SALTS_UTILS_ROOT` environment variables. SaltsBase supplies Core, NativeIO and parsers; CMeta exports `CMeta::CMeta`, `CMeta::CSTL` and `CMeta::CFlow`; SaltsUtils supplies IDL and Schema. The shared/behavior-tree implementation lives in this repository. Configure consumes these installed dependencies without copying or rebuilding them.

Select the compiler-matched shared vcpkg-cache installation with `VCPKG_INSTALLED_DIR`. The Windows x64 LLVM-MinGW preset selects `qigao-x64-windows-llvm-mingw`; package discovery and runtime PATH use that tree. `vcpkg.json` remains the production dependency declaration. The cache checkout (`VCPKG_CACHE_REPOSITORY_ROOT`), restored installation and first-party SDK roots have separate roles; sharing the checkout does not establish ABI compatibility. Import the values described in [.env.example](.env.example) into the parent environment before invoking CMake; CMake does not load `.env`.

On Windows, enter the configured `VSDEVCMD_BAT` environment and restore the selected `VCPKG_ROOT` if VsDevCmd changes it. All production modules are assembled unconditionally. `BUILD_TESTS` controls formal tests and is enabled by the full native user profile; there is no foundation-only build profile.

Runtime profiles consume `CMeta::ACE`, `CMeta::CFlow` and SaltsBase's `process_async`. The public C ABI remains v3. `CMakeOptions.cmake` owns project options; dependency prefixes and runtime paths belong to the user preset. The Windows x64 entry includes the runtime, CLI, shared SDK and formal tests:

```sh
cmake --preset llvm-mingw-x64-release-user
cmake --build --preset llvm-mingw-x64-release-user
ctest --preset llvm-mingw-x64-release-user
cmake --build --preset install-llvm-mingw-x64-release-user
```

[build.yml](build.yml) orchestrates the same Windows x64 profile through
Praktor, with dependencies `prepare -> configure -> build -> test -> install`.
After loading the SDK/cache environment and entering the Windows developer
environment described above, run it from the repository root:

```sh
praktor --config build.yml --task install --timeout-ms 3600000
```

Use an independent Praktor installation outside this checkout's build and
stage directories: Windows locks running executables and DLLs during rebuild
or installation. Bootstrap that tool with the CMake commands above before
using it to build this repository. The workflow keeps the complete CMake/CTest
graph and delegates compiler, paths and options to the existing user presets.
Its four stage logs are written under `build/logs`, preserving command exit
codes and stopping dependent tasks on failure. Redirecting full logs to files
keeps them outside the runtime's bounded task-output capture. The CLI option
sets a one-hour workflow budget; each Shell stage also has an explicit timeout.

The root manifest declares `pcre2`. The compiler-matched shared vcpkg-cache
selected by `VCPKG_INSTALLED_DIR` already supplies PCRE2 10.46; configure consumes
its `PCRE2::8BIT` target through the existing preset prefixes. Praktor does not
copy or rebuild that package. The existing writable product installation
`PRAKTOR_VCPKG_INSTALLED_DIR` remains separate from the shared SDK payload.
Regex actions use native PCRE2 syntax and leftmost-first matching; see
[the runtime contract](docs/ACTIONS_RUNTIME.md) for capture and resource limits.

The presets retain writable local binary caching and a read-only shared feed through [the NuGet configuration](cmake/vcpkg-cache.nuget.config); authentication comes from the parent `GITHUB_TOKEN`. Missing cached packages follow vcpkg's manifest source-build path. Runtime PATH includes both installation trees without copying DLLs. Installation writes to `build/<preset>/stage`. Windows x64 tests cover real process I/O, failed admission, capacity and stale reservations, callback detachment, CFlow/cohort drain gates, the public SDK and CLI Shell output. Other platforms, sanitizers and release packaging require their own qualification.

The retired CI, source-policy audits, NuGet wrapper and separate consumer/smoke projects have been removed. Formal tests belong to their modules and run through the complete root CTest graph.

See [C11 runtime reset](docs/RUNTIME_RESET.md) for the owner and feature boundaries.

## Upstream dependency versions

The locally tested Windows LLVM-MinGW chain uses SaltsBase 0.1.6, CMeta 0.1.6,
SaltsUtils 0.1.3 and the vcpkg-cache 0.1.3 restored payload. These identify this
qualification run, rather than imposing a second package lock. The installed
package configs enforce their dependency versions. `.env` supplies the selected
SDK roots and version fields; preserve credentials when updating those fields.
