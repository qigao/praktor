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

The runtime is a bounded **single-owner** executor with a maximum of 64 tasks after expansion. It accepts YAML workflows with action trees and their task-level shorthands. Root `parallelism` allows ready tasks to overlap process I/O and waits, with atomic group output publication. Local `includes` and `uses` reuse task dependencies, and `dynamic_tasks` builds tasks from JSON input before execution. [DAG parallelism](docs/grammar.md#dag-parallelism) · [Workflow composition](docs/grammar.md#workflow-composition)

Use `dotEnv` or the inherited environment for external values and `variables`
or `vars` for workflow values. Admission rejects root `env` and `outputs`,
as well as `env`, `when` and `triggers` on selected tasks.

An `actions` tree can compose control nodes with Shell and Tool leaves.
The workflow admits process defaults and service bindings before execution.
Use a `cases` sequence for a zero-based integer `switch` or a `cases`
mapping for exact string matches with an optional `default`. [Workflow grammar](docs/grammar.md)

A task or workflow can also declare a `statechart` with named states, guarded
transitions, lifecycle events, completion notifications and state-scoped timers.
Statecharts expose read-only state to BT guards. The DAG schedules tasks, and
each task's BT executes its actions and determines its outcome. A BT can
signal the document chart with `raise` and `scope: workflow`. Other ready
tasks can observe the transition through their guards. [Statechart grammar](docs/STATECHART.md) · [Device assessment](examples/statechart.yml) · [Sensor handshake](examples/statechart-signals.yml)

### Running build and model workflows

Run `praktor --config build.yml --task test --timeout-ms 3600000` locally or
inside a CI runner configured for this build. [CI usage](docs/USER_GUIDE.md#在-ci-中复用工作流)

For model calls, a `tool` task invokes a declared service, the downstream BT
validates the response, and an optional document Statechart records the
validation result. The sample-response example runs locally without an
API key. The OpenAI, Claude and Gemini examples use the same task boundary
with curl services. [Offline workflow](examples/llm/summary-workflow.yml) · [Live model workflows](docs/USER_GUIDE.md#llm-工作流)

### Behavior tree guards

`if`, `while`, `precondition`, `wait_until` and `assert` accept a `guard` mapping with an explicit
list of readable task variables in `fields` and exactly one of `match` or
`expr`. Conditions read task values when the tree ticks.

- `match` uses Salts Selector for string equality, set membership, `has($field)`
  and `capability("name")`. It requires at least one entry in `fields`.
  Capabilities come from activated services in the current workflow.
- `expr` compiles integer comparisons, arithmetic (`+`, `-`, `*`, `/`, `%`),
  bitwise operations (`&`, `|`, `^`, `~`, `<<`, `>>`), boolean literals,
  parentheses, `!`, `&&`, `||`, and `has($field)` to Salts
  QueryVM. Comparisons read signed 64-bit decimal integers from task text
  values. A missing or invalid integer operand produces an execution error
  if the expression reads it. The logical operators short circuit.
  Expressions with no variable references can use `fields: []`.

Integer operators bind more tightly than comparisons. From highest to lowest
their binary precedence is `* / %`, `+ -`, `<< >>`, `&`, `^`, then `|`.
Write ranges as two comparisons, such as `0 <= $used && $used <= $limit`.
Division truncates toward zero. Arithmetic overflow, division or remainder
by zero, and shift counts outside 0 to 63 fail evaluation when reached.
Bitwise operators use 64-bit two's-complement patterns. Shifts fill with
zeros and discard bits shifted out.

For `match`, a comparison on a missing field stays unknown under negation
and yields a non-match. Use `has($field)` to test presence.

```yaml
tasks:
  - name: prepare
    set_variable: phase
    value: ready
  - name: verify
    depends_on: [prepare]
    precondition:
      guard:
        fields: [phase]
        match: '$phase in ["ready", "verified"]'
    child:
      set_variable: check
      value: passed
```

Running `praktor --config workflow.yml --task verify` first publishes
`prepare.phase`. The precondition reads that value and admits its child.
Composite tasks use a task-level list such as `outputs: [$result]` to publish
selected variables to the workflow when they succeed. Direct task roots also
provide default output and process or service bindings. [Output semantics](docs/grammar.md#task-outputs)

A numeric condition uses the same control node syntax.

```yaml
if:
  guard:
    fields: [used, requested, limit, flags]
    expr: '$used + $requested <= $limit && ($flags & (1 << 2)) != 0'
then:
  set_variable: decision
  value: start
else:
  set_variable: decision
  value: wait
```

Existing scalar conditions and condition subtrees keep their execution
semantics. The workflow preflight validates each selected guard before any
task runs. A Session owns its compiled conditions and releases them with the
tree. The Plan retains source and bindings so each Session can open and close
independently. QueryVM expressions use at most 4096 source bytes, 256
instructions, and 32 levels of expression nesting.

### JSON values in guards

Use `ParseJson` before the guard to extract from a JSON document in the task
scope. The `result` option controls the output written to `output_key`.

| `result` | Output |
| --- | --- |
| `first` (default) | The first match. JSON strings become text and other values are serialized. |
| `one` | Exactly one scalar. Strings are decoded, numbers retain their original token, and booleans and null become text. Zero or multiple matches, arrays and objects fail the action. |
| `count` | The decimal number of matches. Zero matches write `0`. |

`one` and `count` validate a fixed, streamable JSONPath during workflow
preflight. Supported paths include member access, wildcards, and terminal
scalar filters such as `$.exit_codes[?@ != 0]`. Invalid input or a failed
selection leaves the previous output unchanged.

```yaml
tasks:
  - name: verify
    vars:
      report: '{"health":{"status":"ready"},"exit_codes":[0,0]}'
    actions:
      Sequence:
        - ParseJson: {input_key: $report, path: '$.health.status', output_key: health, result: one}
        - ParseJson: {input_key: $report, path: '$.exit_codes[?@ != 0]', output_key: failed_jobs, result: count}
        - assert: {guard: {fields: [health], match: '$health == "ready"'}}
        - assert: {guard: {fields: [failed_jobs], expr: '$failed_jobs == 0'}}
```

### Waiting and assertions

`wait_until` checks its guard on each tick. It succeeds as soon as the guard
matches and returns `RUNNING` while it waits. A positive integer `timeout_ms`
sets the local deadline from the first tick of each activation. Reaching that
deadline fails the node with a timeout diagnostic. The workflow deadline
applies independently. A value that changes while waiting must be supplied
to the Session by its host.

`assert` checks its guard once per activation and fails immediately when it
does not match. Set `message` to provide a diagnostic. Set `actual` to one
`$name` reference from the guard's `fields` to include its observed value in the
failure message. Both nodes propagate guard evaluation errors.

```yaml
tasks:
  - name: verify
    vars: {phase: ready, failed_jobs: '0'}
    actions:
      Sequence:
        - wait_until:
            guard:
              fields: [phase]
              match: '$phase == "ready"'
            timeout_ms: 30000
        - assert:
            guard:
              fields: [failed_jobs]
              expr: '$failed_jobs == 0'
            message: All jobs must succeed
            actual: $failed_jobs
```

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

See [the public header](praktor/include/praktor.h) for ownership and result-release requirements.

Embedded callers use `praktor_execute_with_events` to deliver typed external
events to the active document and task charts. [Host event protocol](docs/STATECHART.md#宿主事件)

The public header is `praktor/include/praktor.h`. All Praktor implementation
sources compile into one shared `Praktor` target. The CLI lives in `app/main.c`,
is built by `app/CMakeLists.txt`, and links only that shared library. Tests and fixtures
live in `praktor/test`, and runnable workflows live in `examples`.
Actions headers and implementations live in `praktor/include/actions` and
`praktor/src/actions`; see [the Actions guide](docs/ACTIONS.md).
Reusable `tool` tasks reference independent service YAML files through the root
`services` map. Configurator shares one generic execution instance per name;
executable selection and argv templates belong to YAML. See [the user guide](docs/USER_GUIDE.md)
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

The installed CMake SDK exports `Praktor::Praktor`. Consumers explicitly resolve
the required SaltsBase, CMeta and PCRE2 (10.46 or newer, 8-bit) packages before
loading Praktor. Supply the matching shared vcpkg-cache prefix through the
consumer's CMake dependency search paths.
The SDK archive does not bundle those external SDKs or their runtime DLLs.

The complete build consumes installed **SaltsBase**, **CMeta** and **SaltsUtils** packages from explicit `SALTS_BASE_ROOT`, `CMETA_ROOT` and `SALTS_UTILS_ROOT` environment variables. SaltsBase supplies Core, NativeIO and parsers; CMeta exports `CMeta::CMeta`, `CMeta::CSTL` and `CMeta::CFlow`; SaltsUtils supplies IDL and Schema. The shared/behavior-tree implementation lives in this repository. Configure consumes these installed dependencies without copying or rebuilding them.

Select the compiler-matched shared vcpkg-cache installation with `VCPKG_INSTALLED_DIR`. The Windows x64 LLVM-MinGW preset selects `qigao-x64-windows-llvm-mingw`; package discovery and runtime PATH use that tree. `vcpkg.json` remains the production dependency declaration. The cache checkout (`VCPKG_CACHE_REPOSITORY_ROOT`), restored installation and first-party SDK roots have separate roles; sharing the checkout does not establish ABI compatibility. Import the values described in [.env.example](.env.example) into the parent environment before invoking CMake; CMake does not load `.env`.

On Windows, enter the configured `VSDEVCMD_BAT` environment and restore the selected `VCPKG_ROOT` if VsDevCmd changes it. All production modules are assembled unconditionally. `BUILD_TESTS` controls formal tests and is enabled by the full native user profile; there is no foundation-only build profile.

The root `Praktor` project owns C11 and PIC for all modules. C++ is enabled only
with `BUILD_TESTS`, with the public-header tests using root-owned strict C++17.
Module targets retain public compile requirements, symbol visibility and folder
settings, without repeating root language modes or setting library VERSION/
SOVERSION. The SDK package version belongs to the root project. Target names,
the `Praktor::` namespace and the public C ABI remain unchanged.

Runtime profiles consume `CMeta::ACE`, `CMeta::CFlow` and SaltsBase's `process_async`. The public C ABI remains v3. `CMakeOptions.cmake` owns project options; dependency prefixes and runtime paths belong to the user preset. The Windows x64 entry includes the runtime, CLI, shared SDK and formal tests:

```sh
cmake --preset llvm-mingw-x64-release-user
cmake --build --preset llvm-mingw-x64-release-user
ctest --preset llvm-mingw-x64-release-user
cmake --install build/llvm-mingw-x64-release-user
```

[build.yml](build.yml) selects the configure, build and test preset from
`PRESET` in `.env` and installs from `build/<PRESET>`. Set
`PRESET=llvm-mingw-x64-release-user` for the Windows x64 profile above, or use
another user preset with matching configure, build and test entries. Praktor
loads `.env` and expands `$PRESET` in each shell command.
Use a name that begins with a letter, digit or underscore and contains letters,
digits, underscores, hyphens or dots.
Run from the repository root with the matching SDKs and developer environment.

```sh
praktor --config build.yml --task install --timeout-ms 3600000
```

Use an independent Praktor installation outside this checkout's build and
stage directories: Windows locks running executables and DLLs during rebuild
or installation. Bootstrap that tool with the CMake commands above before
using it to build this repository. The workflow keeps the complete CMake/CTest
graph and delegates compiler, paths and options to the existing user presets.
The install step copies artifacts from the tested build tree.
Its four stage logs are written under `build/logs`, preserving command exit
codes and stopping dependent tasks on failure. Redirecting full logs to files
keeps them outside the runtime's bounded task-output capture. The CLI option
sets a one-hour workflow budget. Shell stages inherit the document timeout
unless they set a stage-specific timeout.

The root manifest declares `pcre2`. The compiler-matched shared vcpkg-cache
selected by `VCPKG_INSTALLED_DIR` already supplies PCRE2 10.46; configure consumes
its `PCRE2::8BIT` target through the existing preset prefixes. Praktor does not
copy or rebuild that package. The existing writable product installation
`PRAKTOR_VCPKG_INSTALLED_DIR` remains separate from the shared SDK payload.
Regex actions use native PCRE2 syntax and leftmost-first matching. See
[the workflow grammar](docs/grammar.md) for the runtime boundary.

The presets retain writable local binary caching and a read-only shared feed through [the NuGet configuration](cmake/vcpkg-cache.nuget.config); authentication comes from the parent `GITHUB_TOKEN`. Missing cached packages follow vcpkg's manifest source-build path. Runtime PATH includes both installation trees without copying DLLs. Installation writes to `build/<preset>/stage`. Windows x64 tests cover real process I/O, failed admission, capacity and stale reservations, callback detachment, CFlow/cohort drain gates, the public SDK and CLI Shell output. Other platforms, sanitizers and release packaging require their own qualification.

The retired CI, source-policy audits, NuGet wrapper and separate consumer/smoke projects have been removed. Formal tests belong to their modules and run through the complete root CTest graph.

See [the user guide](docs/USER_GUIDE.md#当前执行范围) for the current execution profile.

## Build with Slate

[`slate.json`](slate.json) uses the same installed SDK roots and vcpkg
triplets as the CMake presets. Slate reads the local `.env` directly. Put
Ninja and pkgconf on `PATH` and choose SDKs that match the selected compiler
and architecture. The Windows LLVM-MinGW build uses `LLVM_MINGW_ROOT`.
The `release-msvc` and `debug-msvc` presets use `VSDEVCMD_BAT`.

```sh
slate doctor --preset release --target praktor
slate build --preset release
slate build --preset debug --target praktor
slate run --target praktor -- --config "$PWD/examples/basic.yml" --task hello
slate test --list --preset tests
slate test --preset tests
slate install --targets praktor praktor_runtime salts_behavior_tree --components runtime,dev
```

The `debug-tests` preset runs the same tests in a debug build. Use
`slate test --preset tests --label process` to run the process suites. The
other labels are `praktor`, `schema`, and `behavior-tree`. Slate builds the
native helper targets before generating the test fixtures.

The Slate runtime target is `praktor_runtime` so its Ninja object directory
is distinct from the `praktor` CLI on case-insensitive filesystems. Use
the CMake install workflow for the `Praktor::Praktor` CMake package.

## Upstream dependency versions

The locally tested Windows LLVM-MinGW chain uses SaltsBase 0.1.8, CMeta 0.1.6,
SaltsUtils 0.1.4 and the vcpkg-cache 0.1.3 restored payload. CMeta uses the declared
source SDK at `679710f7e3312811efdd92494744a2499286a6a5`, built against Base 0.1.8;
its older published 0.1.6 binary is built against Base 0.1.6. These identify this
qualification run, rather than imposing a second package lock. The installed
package configs enforce their dependency composition, and root configuration
requires the exact versions selected in the parent environment. `.env` supplies
the SDK roots and version fields; Praktor's native workflow loader can load it
before CMake runs. Configure rejects a package config outside the selected SDK
roots, including cached configs. Preserve credentials when updating those fields
and use `cmake --fresh --preset <user-preset>` after changing SDKs. Base 0.1.8 keeps
JPEG/libyuv discovery with direct Media consumers.

With `PRESET=llvm-mingw-x64-release-user`, the version checks compare the CMake
report with the selected SDK and cache versions, the PCRE2 minimum and the toolchain.
Select an individual check with `--task check_cmeta`, or run all six checks
through `confirm_compatible`. [Version check workflow](examples/upstream-version-checks.yml)

```sh
praktor --config build.yml --task configure
praktor --config examples/upstream-version-checks.yml --task confirm_compatible
```
