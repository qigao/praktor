# Praktor execution backends

Issue: [#74 — first-class Shell, Wasm, and TurboScript execution backends](https://github.com/qigao/praktor/issues/74)

## Decision

Praktor exposes three explicit execution modes while keeping one workflow model, one DAG scheduler, and one task result contract:

1. **Shell/process** — existing `command:` and `program:` tasks. Their established parsing, process semantics, output capture, timeout, and exit-status behavior remain unchanged.
2. **Wasm** — execute a host-approved Wasm module/export through a TurboWasm adapter.
3. **TurboScript** — execute an inline/reviewed TurboScript program or an approved script module through a TurboScript adapter.

This adds execution backends; it does not replace the backend-neutral `tool:` HostTool task. HostTool means “invoke a reviewed host capability by identity.” The three execution modes mean “which execution engine runs this task.” A HostTool implementation may itself use a native service, Wasm, TurboScript, or another backend, but Praktor core does not infer that mapping.

## Ownership

| Concern | Owner |
| --- | --- |
| Workflow parse/plan, DAG scheduling, task states/events, structured results | Praktor |
| Process launch, exit code, stdout/stderr and process termination | Praktor's existing process executors |
| Wasm admission, validation, instance/guest lifecycle, fuel, memory and execution | TurboWasm |
| Script compilation/execution, script modules, session state and script memory limits | TurboScript |
| Native DSO load, canonical Plugin ABI, leases and quiescent unload | Salts::Plugin |
| Native provider composition, generation publication and scope admission/drain | Salts::Configurator / Salts::ServiceHost |
| Approved module IDs, adapter provisioning, backend policy and shared runtime instances | Embedding host |

Praktor core must not become a second native loader, open arbitrary native/Wasm module paths supplied by workflow data, or silently fall back from a denied/missing runtime module to shell execution.

## YAML direction

The exact accepted spelling must be implemented with strict parsing. The examples below define the intended shape, not current behavior.

Existing process tasks remain unchanged:

```yaml
tasks:
  - name: build
    command: "cmake --build --preset release"

  - name: verify
    program: "./verify"
    args: ["--strict"]
```

TurboScript tasks use explicit source or module identity. A task must select exactly one form:

```yaml
tasks:
  - name: transform
    turboscript:
      source: |
        result = input * 2
      with:
        input: "{{ tasks.load.outputs.value }}"
```

Wasm tasks address an approved logical module and export, not a user-controlled file path:

```yaml
tasks:
  - name: evaluate
    wasm:
      module: risk.policy
      export: evaluate
      with:
        score: "{{ tasks.score.outputs.value }}"
      fuel: 100000
```

The host resolves `risk.policy` to an approved artifact and policy. Any requested fuel is bounded by host policy; a workflow cannot enlarge the host's maximum resource limits. Module and export identity, source bytes, parameters, limits, and other semantic runner fields participate in plan identity and effect analysis.

The existing flow-level `script:` property remains a post-processing feature. It is not silently reinterpreted as the standalone `turboscript:` runner.

## Common invocation/result contract

Every backend receives a resolved input object and the same execution-control budget and returns one structured task result. Backend-specific raw pointers or runtime objects never escape into workflow values.

- Shell/process keeps the existing exit code, stdout, stderr, and output capture behavior.
- Wasm and TurboScript return a canonical structured value (normally an object); Praktor publishes it under the same task output namespace used by other runners.
- Errors map to a stable task failure code and phase, preserving a bounded diagnostic and runtime-specific detail when available.
- Cancellation and deadlines propagate from the same workflow budget. A backend must stop cooperatively or report that cancellation is pending; Praktor must not mark an invocation terminal while its runtime still owns live callbacks, host waits, or result storage.
- Runtime quotas are enforced by the owning runtime. Praktor passes the control/budget contract and records the outcome; it does not reimplement Wasm fuel or TurboScript memory accounting.
- Task events preserve workflow/task lineage and distinguish queued, running, succeeded, failed, cancelled, and timed-out outcomes.

Input/output serialization must use the existing `WorkflowValue`/canonical JSON ownership contract. Never pass borrowed pointers from a guest linear memory or a TurboScript context into later DAG tasks without copying/lifting them into owned workflow values.

## Adapter boundary

The adapters are host-provided and versioned. Their contract should be narrow:

1. validate a logical module ID and requested export against host policy;
2. prepare/cache a typed binding before invocation, not perform DSO/manifest/reflection discovery on every call;
3. invoke using resolved `WorkflowValue` inputs plus execution control;
4. return an owned result or structured failure;
5. release per-invocation resources before signaling terminal completion.

Praktor core should not hard-link to TurboWasm. The TurboScript adapter must be optional, and a build without TurboScript must reject a `turboscript:` task before any task side effects instead of ignoring it. Existing `script:` fail-fast behavior in the core-only profile remains unchanged.

Native service adapters may use Salts ServiceHost scopes. A scope must cover the entire synchronous invocation or the full asynchronous terminal lifetime; module leases must not be released while callback code, descriptors, guest/host values, or cleanup can still reach the module.

## Admission, review, and effects

- Unknown runner keys or ambiguous runner combinations fail parsing.
- Missing or denied module IDs fail plan/host preflight; never shell-fallback.
- Workflow input chooses logical IDs, not filesystem paths or arbitrary executable paths.
- Wasm imports, TurboScript imports, and declared capabilities contribute conservatively to the plan's effect/capability manifest.
- Unknown dynamic imports/effects remain unknown or are denied in restricted profiles.
- Host policy may cap fuel, memory, execution time, concurrency, and available host interfaces. Workflow metadata cannot grant capabilities beyond those caps.
- Do not claim Wasm is automatically a complete sandbox: only explicitly constrained imports and runtime resources establish the effective boundary.

## Delivery sequence

1. Land this contract and align the existing HostTool distinction.
2. Add task parameter types, strict YAML parsing, runner selection validation, plan/effect identity, and parser/describe tests.
3. Add versioned host adapter registration and capability reporting, independent of direct TurboWasm linkage.
4. Add Wasm invocation and fixture tests with fuel exhaustion, denied imports, cancellation/deadline, output ownership, and teardown.
5. Add TurboScript invocation and fixture tests for source/module admission, Session isolation, cancellation/deadline, and teardown.
6. Test generation replacement and shared native services through Salts ServiceHost, including busy drain while an admitted call remains active.
7. Qualify core-only and installed SDK builds; retain existing workflow and ABI compatibility.

## Non-goals

- Replacing the DAG scheduler or workflow model.
- Converting every `tool:`, `uses:`, or orchestration node into an execution backend.
- Adding a Praktor-owned DSO loader or a second Plugin ABI.
- Exposing guest pointers, ExprTk internals, or TurboWasm internal structs through the Praktor public ABI.
- Treating native TurboScript plugins as sandboxed merely because the caller uses a script.
