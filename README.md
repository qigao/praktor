# Praktor — native C11 workflow runtime

Praktor is now a **C11-only** YAML workflow project. The breaking [PR #79](https://github.com/qigao/praktor/pull/79) removed the C++ runtime and changed the public SDK to **C ABI v3** (package version 0.5.0). The former C++ WorkflowExecutor, legacy Actions, Script Engine, CLI commands and API v2 are **not supported**. No fallback or compatibility implementation remains.

**Current qualification:** the native C11 source was merged before testing, as explicitly requested. The [post-merge CI](https://github.com/qigao/praktor/actions/workflows/c11-post-merge.yml) is the acceptance authority. A green source-only policy is not proof the SDK, CLI, process lifecycle or application behavior are ready.

## Runtime architecture

- **Salts CFlow:** the only bounded run/cancel/finalize execution mechanism. The application Owner serializes DAG claims and task-status publication; CFlow workers never own the DAG or root value store.
- **CMeta:** immutable configuration Reflection, exact-signature Interceptor admission, and lexical cleanup for operations with a no-fail release contract. Fallible close, flush, and Process retirement remain explicitly checked.
- **Salts Configurator:** one static provider's dependency and lifecycle authority. Dynamic providers, when implemented, must use the canonical **ServiceHost/Plugin** generation and scope mechanisms; no private loader is supplied.
- **SaltsUtils BehaviorTree + Process:** canonical C11 Session/Actions and sole OS subprocess/NativeIO owner. Task-owned Process resources and callback lifetimes must settle before publishing outputs.
- **C11 YAML Snapshot, DAG and TaskRegistry:** immutable plan input with bounded task admission and typed, owner-only output publication.

No C++ source or C++ header is retained in the repository. The runtime is **not** a translation of the former C++ class hierarchy.

## Current admitted subset

The initial runtime is a bounded **single-owner** executor (maximum 64 tasks). It accepts self-contained YAML workflows composed of supported direct SetVariable, Shell, and canonical single-Command roots. Unsupported scripts, includes, unqualified nodes, file-based WorkflowPlan capabilities, dynamic tasks, global plugin discovery and application-level Parallel/multi-worker execution **fail closed**. A successful lower-level CFlow/Process test does not enable a missing application feature.

## CLI

```sh
praktor workflow.yml
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

The public API no longer accepts the old ABI v2 workflow path/input structures. Callers must migrate deliberately.

## Building and qualification

Builds require the **canonical upstream master-owned** C SDK chain: SaltsCore (platform/NativeIO), Salts (CMeta/CFlow/Configurator), Crypto (static cryptography), and SaltsUtils (CYaml/JsonParser/BehaviorTree/Process). They are provisioned from recorded upstream commits; released older SDKs are not silently substituted. GmSSL belongs exclusively to the Crypto build's canonical vcpkg-cache package, not Praktor's production manifest.

The [post-merge C11 workflow](https://github.com/qigao/praktor/actions/workflows/c11-post-merge.yml) checks Linux, Windows, macOS, pure C source policy, C SDK consumer, OS Process and cleanup, plus Linux complete-source TSan. Failure or missing upstream packages means **not qualified**—never a Legacy fallback.

See [C11 runtime reset](docs/C11_RUNTIME_RESET.md) for the owner and feature boundaries.
