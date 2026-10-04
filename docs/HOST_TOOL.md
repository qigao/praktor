# HostTool execution

Praktor 0.4.4 adds a backend-neutral HostTool runner for reviewed
`WorkflowPlan` execution.

A workflow names a stable host tool identity and supplies a typed argument
object:

```yaml
tasks:
  - name: inspect
    tool: repo.inspect
    with:
      path: "{{ variables.path }}"
      limit: 2
```

Praktor does not resolve that identity to a native library, Wasm module,
remote endpoint, RuntimeTools registry, or plugin. The embedding host owns that
mapping and the authority behind it.

## Architecture

```mermaid
flowchart LR
    S[Reviewed workflow source] --> P[WorkflowPlan]
    P --> M[Frozen HostTool manifest]
    M --> V[Host validate/preflight]
    V -->|reject| Z[No workflow side effects]
    V -->|all admitted| D[Praktor DAG]
    D --> T[HostTool task]
    T --> A[Praktor HostTool C ABI]
    A --> H[Embedding host]
    H --> R[Host registry/runtime]
    R --> W[Wasm backend]
    R --> N[Native backend]
    R --> X[Remote / MCP backend]
```

Ownership is intentionally one-way:

- Praktor owns workflow parsing, reviewed plan identity, DAG scheduling,
  cancellation/deadline composition, lifecycle events, and typed task output.
- The embedding host owns tool discovery, capability/effect admission, backend
  selection, and actual tool execution.
- HostTool is not a Wasm runner and does not make Praktor depend on TurboAgent,
  TurboWasm, RuntimeTools, MCP, or Plugin ABI.

## Lifecycle

```mermaid
sequenceDiagram
    participant C as Compiler / caller
    participant P as Praktor WorkflowPlan
    participant H as HostTool executor
    participant D as Praktor DAG
    participant T as Host backend

    C->>P: compile reviewed workflow
    P-->>C: digest + HostTool manifest
    C->>P: execute reviewed plan
    loop every frozen HostTool identity
        P->>H: validate(tool, typed argument template)
        alt missing or denied
            H-->>P: reject
            P-->>C: HOST_TOOL_REJECTED
        else admitted
            H-->>P: OK
        end
    end
    Note over P,D: DAG starts only after all HostTools are admitted
    P->>D: start workflow
    D->>H: invoke(tool, resolved typed arguments, control, observer)
    H->>T: host-owned execution
    T-->>H: canonical JSON result
    H-->>D: synchronous result sink
    D-->>C: tasks.<name>.outputs.result
```

Unknown or denied HostTools fail during preflight before the workflow starts.
There is no fallback to `command`, `program`, shell, a library path, or a
Wasm path.

## Public C ABI

The HostTool surface is additive in ABI major 2:

- `PRAKTOR_ABI_MINOR >= 5`
- `PRAKTOR_CAPABILITY_HOST_TOOL`
- `praktor_host_tool_executor`
- `praktor_execute_workflow_plan_host_tools()`

The executor has two operations:

1. `validate` — side-effect-free preflight of the frozen tool identity and
   reviewed typed argument template.
2. `invoke` — execution after all plan HostTools have passed preflight.

A successful invocation returns one canonical JSON payload through the
Praktor-owned synchronous result sink. The host owns the source bytes and may
release them immediately after the sink returns; allocator ownership never
crosses the DLL/CRT boundary.

## Execution control and lineage

HostTool invocation receives an execution-control view derived from the
workflow execution:

- cancellation remains connected to the workflow cancellation probe;
- timeout is the **remaining** workflow deadline, not a fresh full timeout;
- observer lineage is forwarded for tracing;
- none of these values are injected into workflow variables.

Nested `uses` workflows inherit the same execution-scoped HostTool authority.

## Effects and harness safety

Praktor can prove only that a task requires host execution. It therefore records
the conservative `host_tool` effect and marks the concrete effects as
host-resolved/unknown.

This is deliberate: a HostTool workflow must not become `harness_safe` merely
because Praktor cannot see whether the embedding backend performs filesystem,
network, process, or external mutations. The embedding compiler/host must
perform capability/effect admission before providing HostTool authority.
