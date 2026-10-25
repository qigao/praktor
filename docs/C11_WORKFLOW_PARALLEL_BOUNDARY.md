# Praktor C11 Parallel qualification boundary

This document records a **source-only migration candidate** in PR #79.
Production WorkflowPlan, WorkflowContext, TaskRegistry, DAG scheduling and
DeclarativeTreeExecutor remain on the accepted C++ path. The qualification
libraries are **not** an installed public SDK contract.

## Reuse, never duplicate

| Concern | Canonical owner or implementation |
| --- | --- |
| YAML, task metadata, Command-to-Orch | C11 workflow_snapshot over the existing native parser and AST |
| Task dependency identity | C11 workflow_dependencies; this is not another scheduler |
| Typed task outputs | SaltsUtils JsonParser clone and checked object builders |
| Root/branch text values | Existing C11 value_store and value_txn_commit_group |
| Task queues and threads | Salts CFlow ManualExecutor and WorkerExecutor |
| Native I/O and Process completion | Existing praktor_process_driver and cflow_process_retainer |

## Required owner-turn order

A successful Parallel result requires **all** of the following.

1. **Close admission.** Stop new CFlow branch tasks and Process reservations
   before attempting publication. The caller owns this stop barrier.
2. **Settle callbacks.** Wait for every admitted task to finish exactly one
   run/cancel and one finalize callback. An admission refusal has no callback.
   The CFlow wait_idle contract provides the test fixture's evidence.
3. **Verify success.** Any failed branch, cancellation, or refused admission
   discards all uncommitted branch transactions and typed task updates.
4. **Drain native I/O.** Through the existing Process retainer, advance bounded
   close/reap work and inspect all reservations and adopted Process leases.
   BUSY or positive pending means no commit; a real NativeIO or ack error is
   surfaced separately and the recovery owner remains live. Worker success
   does not mean native I/O has quiesced.
5. **Stage typed output.** Deep-copy and validate every affected task's JSON
   output, retaining its state, output identity and monotonic generation.
   Preparation must not modify any published task or root value.
6. **Commit text values.** Use the existing optimistic value transaction group
   to check conflicts and capacity and publish every text write atomically.
7. **Publish typed output.** In the same exclusive owner turn, with no
   reentrant mutations, perform one allocation-free preflight and publish
   every staged task JSON object. Consume the prepared owner on success.

The basic C11 Process guard combines steps 4 through 7 **only**.
The stricter CFlow + Process guard additionally refuses a not-yet-CLOSED
executor, missing/rejected tasks, nonzero cancellations, incomplete callback
settlement and a non-idle executor. It demands exactly one fresh, group-owned
CFlow task admission per Parallel branch. It uses CFlow's own statistics and
wait_idle acknowledgment, not an invented "worker finished" flag.

**Neither guard can infer the application's semantic branch success.**
The caller must still cease admission and verify all branch results before
publishing. Do not infer them from a task status or child exit code alone.

## Failure ownership

- A live Process reservation or adopted Process produces PARALLEL_BUSY and
  must keep root values, output objects and optimistic transactions unchanged.
- A CFlow Executor whose admission is still OPEN/CLOSING is BUSY. Cancelled
  accepted callbacks produce PARALLEL_CANCELLED; missing or rejected task
  admissions fail INVALID. No typed or text data may be published for them.
- A native close, acknowledgment or NativeIO error produces PARALLEL_RECOVERY.
  It must not be transformed into an ordinary retry or invented success.
- Conflict/capacity/memory failures of the canonical value group do not
  publish typed outputs. Transactions remain active until explicit abort/retry.
- A prepared JSON group is invalidated by task mutation or trigger/each re-entry.
  A monotonic revision rejects ABA even when state and output pointer match.
- An INVARIANT error after a value group succeeds is not a normal retry.
  Retain ownership, report the violated single-driver precondition and
  reconcile the already-published value state. There is no fictitious
  reverse transaction to hide the problem.

## Executable qualification

- Real CFlow ManualExecutor tests check ordered run/finalize, cancel-before-run,
  queue capacity refusal, failed branch and the owner publication path.
- Real two-worker CFlow tests prove overlap, independent optimistic branch
  writes, cancel of a queued sibling while one callback runs, and failure of
  one settled branch without publishing the other.
- Task Registry tests check immutable terminal JSON, typed deep copies,
  atomic one-task multi-output writes and two-phase multi-task publication.
  Fault injection covers failed allocations, clones and partial owner setup.
- Transaction-group tests check conflict, capacity exhaustion and allocator
  failure without partial publication.
- Process retainer tests refuse publication while any reservation is live,
  verify recovery error propagation, and hand off a **real shell child** to the
  canonical Process retainer until release can be acknowledged.
- CI gates include Native SDK, the three-platform Core-only matrix, ASan/
  UBSan and full-source TSan. The TSan workflow must actually run the
  WorkerExecutor and Process barrier binaries, with source Salts dynamic-link
  attestation; building without running is not acceptance.

## C11 DAG readiness qualification (source only)

An experimental C11 `workflow_dag_state` now borrows the already-admitted
`praktor_workflow_dependencies` graph. It counts remaining edges, reserves
ready task indices in declaration order under a bounded concurrency limit,
and waits for the **single application owner** to report completion or
admission refusal. Duplicate dependencies retire with original cardinality.
Failure/cancel/deadline seals future reservations; already-admitted siblings
must settle before the state may be freed. This is not a second CFlow queue,
thread pool, task registry, JSON owner, or graph parser.

This first slice covers only workflows whose projected tasks are all regularly
schedulable. Trigger-only tasks, dynamic/Each expansion, observer semantics,
CFlow owner-loop dispatch, Process leases, and atomic Parallel publication
must be composed explicitly and verified before production use. Graph lifetime
is borrowed. Task completion must be relayed after actual CFlow callback
finalization, and rejected admission must never pretend a callback occurred.
The current simple completion walk scans the bounded graph's edge list; it is
not a claimed large-DAG performance replacement.

CI qualifies C11 dependency cardinality/capacity, deterministic admission,
completed/failed/cancelled sibling settlement, refusal, and runtime safety
in core-only, ASan/UBSan, and complete-source TSan test selections.

The separate `actions_c11_workflow_dag_cflow_test` qualification executes
those indices through the real Salts CFlow ManualExecutor and WorkerExecutor.
Each accepted task receives exactly one run/cancel and finalize; rejected
admissions receive none. Dependency releases happen only in a single owner
turn *after* `control_wait_idle` confirms callback settlement. Tests cover
three diamond-DAG waves (including repeated edges), two overlapping workers,
sibling failure, cancellation of queued work, and bounded queue refusal.
Worker callbacks only write atomic test observations and never touch the DAG.
This is still not a production dispatcher or a TaskRegistry/Process/Parallel
end-to-end cutover.

### YAML Snapshot → DAG → CFlow → typed/text publication probe

`actions_c11_workflow_dag_publication_test` uses the real inline YAML
Snapshot, its canonical C11 dependency graph, root-variable seeding, task
registry and optimistic value transactions. Real CFlow WorkerExecutor callbacks
produce *test-only* results, and the strict existing CFlow + Process-retainer
barrier refuses cross-branch publication until callbacks finalize **and**
the reserved Process lease is explicitly retired. Only then does the owner
publish text and typed JSON outputs, mark tasks complete and unlock the
downstream DAG task in a fresh CFlow cohort. On a failed callback it discards
pre-staged JSON, aborts *all* sibling value transactions and suppresses the
downstream task. The source YAML's Shell commands are deliberately not run:
this is not a real leaf execution, Process-spawn or production cutover test.

## Still outside this qualification

The production DAG/trigger scheduler, structured WorkflowValue lexical scopes,
TaskRegistry observer and task-output projection semantics, long-lived Process
recovery under permanently faulted native providers, durable restart behavior,
CFlow owner event-loop integration and full end-to-end real workflow execution
remain separate gates. In-memory text and JSON publication is **not crash-atomic**
across independent owners, and these tests do not establish it as such.

PR #79 stays **Draft / do not merge** until the whole-program parity
and cancellation/NativeIO lifecycle gates are completed.
### Multiple real Process workers: bounded retainer cohort (candidate)

The source-only C11 barrier supports an explicit **one worker/branch → one
bounded Process retainer** group. Each CFlow WorkerExecutor callback owns
and drives its Session/NativeHost and touches **only its own** retainer.
Retainers have no shared internal thread-safety guarantee. Every reservation
must be acquired *before* spawning a native Process; a saturated retainer
fails closed with `SALTS_ENOBUFS`. Rejected CFlow posts are never silently
normalized to success.

After executor admission is shut down and every run/cancel/finalize callback
has settled (`CLOSED` and `wait_idle`), a **single application Owner**
drives each non-aliased retainer with bounded per-retainer steps. Publication
requires zero pending reservations **and** zero adopted processes across all
workers. A failed NativeIO close returns `PARALLEL_RECOVERY`, an outstanding
lease returns `PARALLEL_BUSY`, and an optimistic value conflict returns
`PARALLEL_CONFLICT`. None may publish partial typed/text outputs or
unblock a dependent DAG successor. Cancelled cohorts never commit; the Owner
still explicitly drains adopted processes *after* worker quiescence.

This is a C11 conformance path, **not** a new Process implementation or
production C++ WorkerExecutor switch. PR #79 remains Draft / DO NOT MERGE.
The independent macOS Legacy Shell cancellation issue #81 remains open.
