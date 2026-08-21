# System Actions Residual Ownership Fixes Design

## Status

Approved in chat on 2026-08-21. This document records the implementation contract for the two residual merge blockers found after the system-actions branch review.

## Background

The system-actions branch already provides typed `service` and `managed_process` actions, process-backed service control, Windows managed-process identity validation, structured task failures, schema parity tests, and end-to-end workflow coverage.

Two ownership defects remain:

1. A Windows process can be created successfully after the controller's startup deadline has expired. The controller currently returns a timeout before reconciling OS state, so its result may contain the stale pre-start `not_running/pid=0` snapshot while the process remains alive.
2. `TaskFailureContext::inner_failure` is a mutable shared pointer. Copies of a finalized failure snapshot can therefore mutate the same nested object outside `TaskRegistry`'s mutex, and `markFailed()` can overwrite an already-finalized outcome.

These are correctness defects in state ownership, not retry-policy or logging defects.

## Goals

- Make a successfully created process immediately identifiable and owned by the controller.
- Ensure startup timeout always performs identity-safe reconciliation and compensation.
- Never return a stale `not_running/pid=0` snapshot after a successful spawn.
- Make nested failure trees immutable after publication.
- Prevent a finalized task failure snapshot from being overwritten.
- Preserve existing service behavior, YAML schema, public result fields, and successful managed-process behavior.

## Non-goals

- No new WorkflowRunner executor injection API.
- No privileged SCM integration test.
- No retry policy for arbitrary failed process termination.
- No configurable service status-parser DSL.
- No changes to `program`, `download`, or TurboScript service behavior.

## Decision 1: Start Returns Stable Process Identity

Change the backend contract from:

```cpp
ManagedProcessResult<std::uint32_t> start(const ManagedProcessParams& params);
```

to:

```cpp
ManagedProcessResult<ManagedProcessSnapshot> start(
    const ManagedProcessParams& params);
```

The Windows backend constructs the returned snapshot from the newly created process handle before releasing that handle. The snapshot contains:

- `state = Running`
- PID
- current session ID
- creation-time instance token
- canonical image basename

The backend must apply the same evidence rules used by query and action-time validation. An immediate process exit, unexpected session, image mismatch, or evidence-read failure returns an explicit failed result and does not return a partial success snapshot.

This interface change is source-breaking only for the new, not-yet-released backend interface and its test fakes.

## Decision 2: Startup Timeout Compensation

`ManagedProcessController` remains the only workflow owner and creates one `steady_clock` startup deadline before calling the backend.

After `backend.start()` succeeds:

1. Store the returned snapshot immediately in `ManagedProcessExecutionResult`.
2. Query OS state using the configured identity, even if the deadline has just elapsed, so the result cannot retain the pre-start snapshot.
3. If the target is confirmed running within the deadline, return success.
4. If the deadline expires before successful confirmation, treat the returned start snapshot as the compensation target.
5. Call `backend.terminate(started_snapshot)`. The backend revalidates session, canonical image, and instance token while holding the target handle before terminating.
6. Query until `NotRunning` using a separate, bounded compensation deadline equal to `stop_timeout_ms`.

Terminal results:

| Condition | Error | Phase | Snapshot |
|---|---|---|---|
| Startup confirmed before deadline | none | empty | authoritative running snapshot |
| Startup timeout; compensation confirmed | timeout | `start_poll` | authoritative `NotRunning` snapshot |
| Compensation command fails | backend failure | `start_timeout_terminate` | latest authoritative snapshot, never the old pre-start value |
| Compensation confirmation times out | timeout | `start_timeout_cleanup_poll` | latest authoritative snapshot, including running identity if still alive |
| Reconciliation query fails | backend failure | `start_timeout_query` | the stable snapshot returned by start |

The primary startup timeout remains visible when compensation succeeds. A failed compensation supersedes it because the terminal resource state is no longer known to be safe.

For `restart`, the same startup compensation applies after the previous instance has been confirmed stopped.

## Decision 3: Immutable Nested Failure Trees

Change:

```cpp
std::shared_ptr<TaskFailureContext> inner_failure;
```

to:

```cpp
std::shared_ptr<const TaskFailureContext> inner_failure;
```

Nested failures are constructed as `std::make_shared<const TaskFailureContext>(...)`. Copies may share nested storage because it is immutable. Top-level snapshots continue to use value semantics, preserving existing callers of `getFailureSnapshot()`.

`TaskRegistry::markFailed()` becomes a terminal latch:

- It accepts a failure only when the task is not already terminal.
- It throws `std::logic_error` if the task is already `Completed`, `Failed`, or `Skipped`.
- The original terminal state and failure snapshot remain unchanged after a rejected overwrite.

This is fail-fast behavior for an internal owner invariant; duplicate finalization is not treated as idempotent delivery.

## State Ownership

### Managed process

- OS process state is authoritative.
- The backend owns native handles only for the duration of an operation.
- A successful `start()` transfers an immutable identity snapshot to the controller.
- The controller owns workflow phase, deadlines, compensation, and the returned execution result.
- The executor only maps the controller result to workflow output and structured errors.

### Task failure

- `TaskRegistry` owns the sole terminal failure snapshot for each task.
- Trigger failure context is a scoped projection of that snapshot.
- `UsesExecutor` consumes snapshots but cannot mutate their nested tree.
- Terminal state can transition exactly once.

## Compatibility and Migration

- Update all `IManagedProcessBackend` implementations and fakes to return `ManagedProcessSnapshot`.
- Existing workflow YAML and output field names remain unchanged.
- Existing successful start/stop/restart semantics remain unchanged.
- Startup timeout becomes stricter: it now attempts deterministic cleanup rather than leaving the created process alive.
- Code constructing a mutable nested failure pointer must switch to `shared_ptr<const TaskFailureContext>` or `make_shared<const ...>`.

## Error and Recovery Semantics

- Invalid parameters remain fail-fast before OS mutation.
- Identity mismatch prevents compensation from terminating an unrelated process.
- Cleanup is bounded; no transition waits indefinitely.
- A failed cleanup returns the latest available identity snapshot and an explicit phase.
- The implementation does not silently retry a failed terminate command.

## Test Contract

Tests must be written and observed failing before production changes.

### Managed process

- Fake backend: a successful start that consumes the deadline returns a stable running snapshot, invokes terminate with exactly that snapshot, then returns timeout with `NotRunning` after successful compensation.
- Fake backend: terminate failure returns backend failure, phase `start_timeout_terminate`, and the stable started snapshot.
- Fake backend: cleanup polling timeout returns phase `start_timeout_cleanup_poll` and the latest running snapshot.
- Windows fixture: a 1 ms startup timeout leaves no fixture process and does not return stale `pid=0` state.
- Existing identity-revalidation, restart, graceful stop, and force-stop tests remain green.

### Failure snapshots

- Compile-time assertion proves `inner_failure` points to const.
- Copying a failure snapshot preserves readable nested fields without exposing mutation.
- A second `markFailed()` throws and preserves the first snapshot.
- `markFailed()` after another terminal state throws and preserves that state.
- Existing nested `uses` structured-failure test remains green.

### Full gate

- Full build succeeds.
- Full CTest passes.
- Shared fixture tests remain serialized and leave zero residual processes.
- `git diff --check` is clean.

## Rollback

The implementation will be isolated in a new commit after this design commit. If the migration introduces a regression, revert the implementation commit while retaining this design and the existing `dddd38d` baseline. No data migration or external cleanup is required.
