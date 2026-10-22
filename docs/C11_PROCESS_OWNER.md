# Experimental C11 workflow Process owner

The C11 BehaviorTree adapter is **opt-in** and is not the default workflow
executor. It is gated by `PRAKTOR_ENABLE_C11_ACTIONS=ON` and the candidate
SaltsUtils Process/BehaviorTree ABI. Do not publish, merge, or switch production
defaults based on these qualification results alone.

## Single-driver ownership contract

`Praktor::Execution::C11WorkflowProcessOwner` is the ServiceHost-facing,
**fixed-capacity** owner for Process leases surviving cancellation. It:

- reserves a recovery slot **before** starting a C11 Shell;
- executes a real `WorkflowRunner` only in serial C11 mode;
- refuses overlapping `execute`, `runReady`, and `close` owner turns;
- rejects nested `uses` until a nested Process-owner propagation contract
  is separately qualified (no implicit Legacy fallback);
- returns from cancellation even if quiescence has not been reached, without
  freeing borrowed stdout/stderr/stdin buffers or completion user-data;
- reaps an adopted Process and releases its stable callback owner **only**
  after terminal completion has been delivered, acknowledged, and Process
  destruction succeeds.

`runReady(64, &reaped, &pending)` advances an explicitly bounded number of
Actor transitions per adopted Process. It never spawns an owner thread.
The embedding host MUST schedule more turns when `pending > 0`, **including
after the workflow result has already been returned**.

### Example host lifecycle

```cpp
// Pseudocode: the host's persistent object owns this across workflow runs.
// Compile against the opt-in source build, not the released default C SDK.
Praktor::Execution::C11WorkflowProcessOwner owner(/*capacity=*/8);

// On an owner event-loop turn; no other caller may run a turn concurrently:
auto result = owner.execute(runner, execution_control);
// A failed/cancelled workflow is NOT proof that native I/O has quiesced.

// On the next owner event-loop turn, after execute() returns:
size_t reaped = 0;
size_t pending = 0;
int status = owner.runReady(64, &reaped, &pending);
if (status == SALTS_EBUSY || pending != 0) {
    // Schedule another owner turn, retain owner and callback storage.
} else if (status != SALTS_OK) {
    // Report the native driver/ack error. Do not destroy or force-free;
    // retain the owner and retry only according to the host's policy.
}

// During host shutdown, only once all workflow calls have returned:
int shutdown_status = owner.close();
if (shutdown_status == SALTS_EBUSY) {
    // Shutdown is not complete. Retain the host and continue recovery turns.
}
```

A permanently failed NativeIO provider may occupy a bounded recovery slot
indefinitely. This is **capacity backpressure**, not invented completion:
future process admission fails with an explicit error. If a host destroys
`C11WorkflowProcessOwner` while a call is active or a Process remains
unsettled, the destructor terminates rather than allowing use-after-free.
The host must stop future callers and finish or retain unfinished leases
before its own storage is released.

## Qualification evidence

- [SaltsUtils Process PR #674](https://github.com/qigao/salts-utils/pull/674):
  bounded non-destructive `shutdown_step`, typed recovery owner, injected
  NativeIO/Actor close/ack faults.
- [SaltsUtils Process PR #674 exact source](https://github.com/qigao/salts-utils/commit/bd60887ef45c5acbf93d3550f75bfcde4d77004f):
  merged BehaviorTree from master plus validated Process/retainer;
  source tree matches qualified PR #674 HEAD byte-for-byte, and is now
  merged on master (no package release or production-default switch).
- [Praktor PR #78](https://github.com/qigao/praktor/pull/78):
  real YAML WorkflowRunner positive/negative tests, cancel-after-child-start,
  Process-owner capacity and concurrent-turn rejection, delayed callbacks.
- [Fully source-instrumented TSan #5](https://github.com/qigao/praktor/actions/runs/39647165423):
  Salts rc.17 + SaltsUtils + real Praktor C11 consumer, 2/2 tests passed.
  Later HEADs require their own exact-source qualification.
- [Salts #1177](https://github.com/qigao/salts/issues/1177) distinguishes
  fully instrumented async logger release/acquire from released binary
  mixed-instrumentation TSan diagnostics.

Remaining production gate: sustained host event-loop scheduling, long-lived
recovery on permanently faulted providers, exact-source ASan/UBSan and
complete source-instrumented TSan across relevant error interleavings.
The source-pinned fault test exercises NativeIO/Actor/executor destruction
refusal and permanent acknowledgment faults; success on a newer Praktor
HEAD must be confirmed independently before closing #673.

The stable `sdk/api/praktor.h` is a separate C interface. This C++ owner
is currently a source-tree experimental API; do not claim it is exposed by
the installed stable Praktor C SDK.
