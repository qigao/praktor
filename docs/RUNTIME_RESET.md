# Praktor: C11-only architecture reset

ABI v3 is intentionally incompatible with the former C++ WorkflowExecutor, Actions and WorkflowContext. No legacy fallback remains.

- CFlow alone owns bounded worker/callback admission, drain, finalize and execution.
- CMeta supplies static typed Reflection for configuration, a native typed Interceptor for admission, and explicit cleanup obligations.
- Salts Configurator owns the static provider graph and component lifecycle. ServiceHost is reserved for actual dynamic Plugin generations; we do not invent a second loader or fake DSO ownership.
- CMeta cleanup discharges only infallible lexical resources. NativeIO Process close, callback settlement and retainer drain are explicit checked operations before owners expire.
- Canonical native YAML Snapshot/DAG/TaskRegistry, Orch and Session/BehaviorTree implement workflow and actions; SaltsBase Process/NativeIO is the only subprocess provider.
- SDK ABI v3 and CLI are C-only. Previous C++ workflow plan, TurboScript execution, old ABI v2, global registries and implicit adapters are removed without emulation.

Initial feature gate: bounded serial Owner, finite inline YAML, native SetVariable-root and Shell/Command-root, 64 tasks maximum. Unsupported node compositions, unqualified output projection, scripts, includes, dynamic workflow plans, multi-worker/Parallel application execution and dynamic Plugin reload fail closed.

## Split SDK composition

The installed split SDK moves Configurator to `CMeta::ACE` and process I/O to
SaltsBase's `Salts::NativeIO` / `process_async`. Praktor uses those public
owners directly. Retaining the former `Salts::Process` dependency would require
an unavailable package; reimplementing spawn or the I/O backend in Praktor
would duplicate SaltsBase ownership. Neither is part of this migration.

`praktor_process_driver` keeps one address-stable `process_async` handle and
owns its preallocated input, capture and line buffers. Accepted native requests
borrow those buffers until terminal callbacks. One driver submits at most one
request per stream. Close detaches application callbacks before cancelling I/O;
native quiescence and successful destroy must precede freeing any buffer.

`praktor_process_retainer` owns bounded recovery slots, allocated at init. Each
spawn reserves a slot first; full capacity returns `SALTS_ENOBUFS` before any
process side effect. Slots transition FREE -> RESERVED -> ADOPTED -> FREE.
Tokens identify the original owner and a monotonically increasing reservation;
stale, foreign or copied-owner tokens cannot release a newer reservation.
Failed admission releases its reservation through the driver's normal cleanup.
An unadopted reservation still counts as pending and blocks destroy/publication.

After close, release can transfer the complete C payload into its reserved
slot without moving the native handle. A recovery turn drives a bounded number
of native steps, destroys only quiescent processes, then frees the payload.
Native errors propagate while retaining ownership; the existing cohort gate
latches those errors and refuses publication. Retainers have one exclusive
driver, with no hidden thread or internal concurrent-call contract. CFlow
workers use distinct retainers; the application Owner takes over only after
worker callbacks settle. A live retainer must drain before destroy, and must
outlive every driver reservation. All buffers remain bounded by admission
limits; close, release and recovery allocate no storage.

Core's `process_retainer_t` was considered, but it adopts a raw `process_t`.
`process_async` deliberately keeps that handle opaque together with its native
pipe endpoints and pending callbacks. Praktor's slots therefore retain the
complete async owner and its containing buffers; they never detach the raw
process or introduce another OS process state machine.

The embedded C ABI and admitted workflow semantics remain unchanged. The
internal driver/retainer names and SDK linkage change together. Existing
process, worker, cohort, runtime and C API tests cover the migration. All
production modules are assembled by the complete native build profile. Reverting this composition requires restoring
the matching former SDK chain, not mixing old Process headers with split SDKs.

Command lowering binds stdout to `stdout`; direct Shell uses `shell_output`
by default. Runtime publication selects that task-kind-specific port before
the shared drain/commit gate. This fixes the former resource error after a
successful Command without changing the exported JSON label `stdout`.

## Current Session interface

Session borrows one complete primary host contract for values and leaf dispatch.
The host and its callback contexts outlive the Session. Indexed or typed leaf
dispatch can be selected, with one corresponding halt callback. A RUNNING leaf
retains its owner through completion or halt; a resumed decline cannot change
owners. Native Shell uses the process driver after the host declines initial
admission. Parallel Shell requires branch-aware host dispatch.

The unused C++ compatibility value/dispatch fields and their alternate owner
state have been removed. Session initialization rejects missing primary value
or leaf callbacks before execution. The installed C ABI v3 remains unchanged.

The full Windows x64 native build, module CTest graph and normal installation
are the current qualification route. Other platforms and sanitizers require
their own evidence. Obsolete CI, separate consumer/smoke projects and standalone
qualification builds have been removed.
