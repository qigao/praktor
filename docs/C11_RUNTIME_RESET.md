# Praktor: C11-only architecture reset

ABI v3 is intentionally incompatible with the former C++ WorkflowExecutor, Actions and WorkflowContext. No legacy fallback remains.

- CFlow alone owns bounded worker/callback admission, drain, finalize and execution.
- CMeta supplies static typed Reflection for configuration, a native typed Interceptor for admission, and explicit cleanup obligations.
- Salts Configurator owns the static provider graph and component lifecycle. ServiceHost is reserved for actual dynamic Plugin generations; we do not invent a second loader or fake DSO ownership.
- CMeta cleanup discharges only infallible lexical resources. NativeIO Process close, callback settlement and retainer drain are explicit checked operations before owners expire.
- Canonical native YAML Snapshot/DAG/TaskRegistry, Orch and C11 Session/BehaviorTree implement workflow and actions; Salts Process is the only subprocess provider.
- SDK ABI v3 and CLI are C-only. Previous C++ workflow plan, TurboScript execution, old ABI v2, global registries and implicit adapters are removed without emulation.

Initial feature gate: bounded serial Owner, finite inline YAML, native SetVariable-root and Shell/Command-root, 64 tasks maximum. Unsupported node compositions, unqualified output projection, scripts, includes, dynamic workflow plans, multi-worker/Parallel application execution and dynamic Plugin reload fail closed.

Testing starts only after the C-only branch is merged, as requested. Require post-merge Linux/Windows/macOS true native C builds, installed SDK C consumer, actual NativeIO/Process, ASan/UBSan/unsuppressed source TSan, and all lifecycle ownership gates. No passing status is asserted before those runs.
