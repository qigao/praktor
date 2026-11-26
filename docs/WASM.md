# WASM tools

A workflow can call a WebAssembly guest as a task with `tool` or as a behavior-tree leaf with `Tool`. A native application supplies an execution host through `praktor_execute_with_wasm`. The host approves each module and export before Praktor starts tasks, then creates a fresh guest invocation for each call.

## Declare the tool

Define the provider in its own service YAML file.

```yaml
provider:
  kind: wasm
  module: llm.measure
config:
  export: run
  fuel: 100000
  timeout_ms: 1000
  output_key: argument_metrics
```

Reference the service and pass arguments through the existing `with` map.

```yaml
services:
  measure: examples/services/llm-measure-wasm.yml

variables:
  model_arguments: '{"text":"你好"}'

tasks:
  - name: measure_arguments
    tool: measure
    with:
      arguments: $model_arguments
```

`module` is a logical name resolved by the native host. The required `config`
map may be empty to use defaults. The service reference covers field names,
default values and the behavior-tree form. [WASM service grammar](grammar.md#wasm-services)

Use `praktor --config examples/llm/wasm-tool.yml --check` to validate the
workflow and its WASM service declaration. The syntax check ends before
module and sandbox policy authorization.

Praktor resolves `with` values in the task scope and encodes up to 64 text fields into a UTF-8 JSON object of at most 64 KiB. A model's structured arguments can be passed as one JSON string field. A successful guest returns one UTF-8 JSON document of up to 4,096 bytes. Its text is available to the rest of the BT under `output_key` and is published by a root tool task on success. For a BT task, list the keys to publish under `outputs`. Downstream tasks can use `ParseJson` on the published text.

## Embed the host

The host owns the module allowlist and module bytes. Praktor owns admission, task scopes, per-invocation and workflow deadlines, cancellation, output validation and publication. The host receives the module, export, requested fuel and requested service timeout at bind time. The host limits fuel across all steps of an invocation and releases interrupted work synchronously. Workflow cancellation and deadline checks are available inside a step. A trap, budget failure, or invalid result terminates the workflow with its corresponding status and leaves that invocation's result unpublished. [Public host contract](../praktor/include/praktor.h)

An engine linked into `Praktor` would let the CLI provide its own Wasm execution environment and make the engine a dependency of every deployment. The host callback approach keeps engine selection and guest authorization with the application. The tradeoff is that an application needs to supply a host to use this feature. The existing CLI and other SDK entrypoints report `PRAKTOR_RUNTIME_UNSUPPORTED` when a workflow declares a WASM service. Existing `exe` and Shell workflows keep their entrypoints and configuration. To remove a WASM integration, remove or replace its service declarations and use the existing entrypoint.

The TurboWasm host example loads a sandbox policy from a YAML file selected by the deployment. Policy entries map logical module names to files, SHA-256 digests, allowed exports, and maximum fuel and timeout per invocation. The host reads and hashes each file once at startup, then keeps the verified bytes until execution and cleanup finish. A service that exceeds either maximum is denied at bind time, before the first task starts. Paths in the policy are relative to the host's working directory. The policy accepts one YAML document of up to 64 KiB and up to 16 unique module names. [Policy loader](../examples/wasm/host.c)

The sandbox policy authorizes WASM guests. Select the policy and workflow from trusted deployment configuration and pass model arguments through `with`. The adapter admits a Core wasm32 module with one bounded private memory and a guest-defined `() -> i32` entry. [TurboWasm host example](../examples/wasm/turbowasm_host.c)

The example executable takes the policy YAML path as its first argument. Embedded hosts use the updated `bind` signature with `fuel` and `timeout_ms` and build against the matching Praktor header and library.

| Import in module `praktor` | Contract |
| --- | --- |
| `input_size() -> i32` | Length of the encoded input in bytes |
| `input_read(offset: i32, destination: i32, length: i32) -> i32` | Copy from the host input into guest memory, returning 0 on success |
| `output_write(source: i32, length: i32) -> i32` | Submit one result from guest memory, returning 0 on success |

The entry returns 0 to accept the submitted output. The copy budget across all `input_read` calls equals the input size. The adapter rejects start sections, other imports, out-of-bounds memory access and repeated result submission. It limits a module to 256 KiB of source, 2 MiB of linear memory, 256 table elements and 8 MiB of cumulative live allocations through its runtime allocator per binding. It runs at most 10,000 fuel units in one step and accounts for fuel across yields. For LLM integration, the native application maps approved tool names to workflow declarations and passes model arguments as data.

## Run the LLM example

The example uses a sample tool-call argument with Chinese text. The guest measures the encoded input, and a dependent task reads `input_bytes` with `ParseJson`.

From the repository root, build the guest with Clang and `wasm-ld`.

```powershell
clang --target=wasm32 -O2 -nostdlib '-Wl,--no-entry' '-Wl,--max-memory=2097152' examples/wasm/measure.c -o build/measure.wasm
```

On Windows x64 with LLVM-MinGW, select the TurboWasm 0.3.0 native SDK from its release archive. Build Praktor with the matched SDKs recorded in the archive's `BUILD-MANIFEST.txt`, including SaltsBase 0.1.11, CMeta 0.1.11, SaltsUtils 0.1.10 and vcpkg-cache 0.1.9. Set `$TurboWasmNative` to the archive's `native` directory, `$PraktorImportLibrary` to the rebuilt `libPraktor.dll.a`, and `$VcpkgTripletRoot` to the selected SDK's `qigao-x64-windows-llvm-mingw` directory. Put the matching Praktor and SDK DLL directories on `PATH`. The example uses SaltsBase's YAML parser and GmSSL SHA-256 from the matched vcpkg SDK. [TurboWasm 0.3.0 Windows SDK release](https://github.com/qigao/turbowasm/releases/tag/v0.3.0)

```powershell
x86_64-w64-mingw32-clang -std=c11 -Ipraktor/include -I"$TurboWasmNative/include" -I"$env:CMETA_ROOT/include" -I"$env:SALTS_BASE_ROOT/include" -I"$VcpkgTripletRoot/include" examples/wasm/host.c examples/wasm/turbowasm_host.c "$PraktorImportLibrary" -L"$TurboWasmNative/lib" -L"$env:CMETA_ROOT/lib" -L"$env:SALTS_BASE_ROOT/lib" -L"$VcpkgTripletRoot/lib" -lturbowasm -lsimd -lcoroutine -lcmeta_cmeta -lcyaml_json_adapter -lcyaml -ljson_parser -lquery_vm -lgmssl -lsalts -o build/praktor-wasm-example.exe
```

Create `build/wasm-sandbox.yml` for the locally built guest and run the host. For a deployment, pin the digest from the approved artifact.

```powershell
$digest = (Get-FileHash -Algorithm SHA256 build/measure.wasm).Hash
@"
modules:
  - module: llm.measure
    path: build/measure.wasm
    sha256: "$digest"
    export: run
    max_fuel: 100000
    max_timeout_ms: 1000
"@ | Set-Content -Encoding utf8 build/wasm-sandbox.yml

& build/praktor-wasm-example.exe build/wasm-sandbox.yml examples/llm/wasm-tool.yml
```

The output includes `{"input_bytes":37,"non_ascii_bytes":6}` in `argument_metrics` and the text `"37"` in the next task's `input_bytes`.
