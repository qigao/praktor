# Process Management

## Decision

Praktor uses `ManagedProcess` as the single owner of a child process, its process tree,
standard I/O pipes, capture buffers, worker thread, and terminal result. Platform handles
remain behind a Pimpl boundary. `ShellExecutor::execute()` and `ProcessExecutor::execute()`
are compatibility facades implemented as `start(...).wait()`.

The lifecycle is explicit:

```text
Starting -> Running -> Exited
                    -> Signaled
                    -> TimedOut
                    -> Cancelled
                    -> WaitFailed
                    -> OutputLimitExceeded
Starting -> SpawnFailed
Running  -> Cancelling -> Cancelled
```

Only `ManagedProcess` owns lifecycle state. Workflow and actions statuses are derived from
the terminal `ShellResult`; they do not independently advance OS process state.

## Platform Strategy

Windows starts the child suspended, assigns it to a Job Object configured with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, and only then resumes it. Cancellation, timeout,
destruction, and output-limit failures terminate the whole job.

POSIX creates a dedicated process group. Cancellation first sends `SIGTERM`; after
`cancel_grace_ms`, the manager sends `SIGKILL`. Timeout and output-limit failures send
`SIGKILL` immediately. All allocating data used by `execve()` is prepared before `fork()`;
the child only calls async-signal-safe functions before `execve()`.

## I/O And Limits

stdin, stdout, and stderr use separate anonymous pipes. stdin is pumped concurrently with
stdout and stderr so duplex workloads cannot deadlock on pipe capacity. Data order is
preserved within each stream, but stdout and stderr do not have a shared total order.

Captured output is bounded by `ProcessSpec::max_output_bytes`, defaulting to 16 MiB across
stdout and stderr. Exceeding the limit terminates the process tree and returns
`ProcessState::OutputLimitExceeded`.

## Public API And Compatibility

Use `ProcessExecutor::start(spec)` for managed asynchronous execution. `ManagedProcess` is
move-only and provides `state()`, `pid()`, `waitFor()`, `wait()`, `cancel()`, and `result()`.
Destroying a running handle cancels and reaps the process tree.

Existing synchronous calls remain source-compatible. `executeAsync()` and
`killProcess(pid)` remain compatibility APIs, but new code should retain a
`ManagedProcess`; a raw PID cannot provide ownership or protect against PID reuse.

Actions thread-pool tasks propagate a cancellation context into shell execution, so
`AsyncTask::cancel()` and `AsyncExecutor::cancelAll()` cancel the owned `ManagedProcess`
instead of attempting to infer ownership from a PID.

## Error Semantics

`ShellResult::success()` is true only for `ProcessState::Exited` with exit code zero.
Spawn failure, signal termination, timeout, cancellation, wait failure, and output-limit
failure are distinct states. POSIX signal termination also records `termination_signal`.

## Migration And Rollback

Callers may migrate incrementally from `execute(spec)` to `start(spec)` without changing
`ProcessSpec`. Rollback consists of changing compatibility facades back to the prior
synchronous platform calls; no workflow configuration or persisted data format changes.
The managed API adds fields and methods but does not remove existing source-level entry
points.

## Verification

Tests cover lifecycle observation, bounded waits, explicit cancellation, destructor
cleanup, process-tree termination, spawn failure, output limits, duplex 256 KiB I/O,
timeouts, environment and argument handling, stream callbacks, actions cancellation, and
workflow regressions.
