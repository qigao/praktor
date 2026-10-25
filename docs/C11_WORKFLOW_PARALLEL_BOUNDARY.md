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

The C11 Parallel Process guard combines steps 4 through 7 **only**.
Steps 1 through 3 remain the caller's responsibility. Do not infer them
from task status or a child process exit code alone.

## Failure ownership

- A live Process reservation or adopted Process produces PARALLEL_BUSY and
  must keep root values, output objects and optimistic transactions unchanged.
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

## Still outside this qualification

The production DAG/trigger scheduler, structured WorkflowValue lexical scopes,
TaskRegistry observer and task-output projection semantics, long-lived Process
recovery under permanently faulted native providers, durable restart behavior,
CFlow owner event-loop integration and full end-to-end real workflow execution
remain separate gates. In-memory text and JSON publication is **not crash-atomic**
across independent owners, and these tests do not establish it as such.

PR #79 stays **Draft / do not merge** until the whole-program parity
and cancellation/NativeIO lifecycle gates are completed.
