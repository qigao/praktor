# C11 cohort and output publication boundary

`workflow_parallel_process_guard.h` and `workflow_owner_settlement.h` describe
the internal publication gate over CFlow callback settlement and bounded process
recovery. The installed application runtime remains serial; this lower-level
composition does not enable application Parallel workflows.

## Owners

- CFlow owns accepted callback run/cancel/finalize and executor shutdown.
- Each worker owns its Session, task-local values and explicit process retainer.
- SaltsBase `process_async` owns the OS process, pipes and native requests.
- The application Owner owns DAG state, typed task outputs and root commit.

A retainer has one exclusive driver. Owner takeover follows worker callback
settlement. Distinct workers use distinct retainers, which remain alive until
all reservations and adopted process payloads are retired.

## Publication order

1. Close branch/task admission and stop new process reservations.
2. Settle every accepted callback, including exactly one finalize. Rejected
   admission has no callback and cannot count as a successful branch.
3. Verify semantic branch success. Cancellation, refusal or failure aborts the
   uncommitted value transactions and typed output updates.
4. Drain native I/O and inspect every retainer. Both reserved and adopted slots
   count as pending. Worker completion alone does not acknowledge pipe cleanup.
5. Commit typed task updates and text values together, within one exclusive
   application Owner turn.

The executor gate checks the expected accepted branch count, absence of refused
or cancelled callbacks, CLOSED lifecycle, no queued/running work and wait_idle.
A zero pending count is accepted only after a real successful recovery sweep.

## Durable recovery refusal

The cohort recovery record binds a bounded, immutable set of distinct owner and
implementation identities. Duplicate, foreign, reordered or replaced owners
cannot inherit an earlier acknowledgment. Preserve the same record through
recovery and publication preflight.

The first non-EBUSY native error is latched. Later successful recovery can retire
resources but cannot erase that refusal or permit publication. Reuse requires
explicit lifecycle completion under the current header contract. BUSY retains
ownership and requires another bounded Owner turn; it never authorizes an
invented terminal result or early buffer release.

## Validation

The full root CTest graph covers executor settlement, value commit rollback,
reserved-only owners, real process cancellation/drain, multi-retainer identity,
first-error latching and post-settlement output publication. No separate
consumer, smoke, source-policy audit or standalone build is required.
