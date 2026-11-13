# Salts C11 Behavior Tree

This directory introduces the reusable C11 `Salts::BehaviorTree` target for
Praktor and other Salts-based applications. It deliberately does not translate
the old C++ runtime line by line.

## Initial slice

The current C11 implementation provides:

- Bounded, caller-configured tree allocation, a single root and parent-before-child construction.
- Leaf callbacks plus Sequence, Fallback, cooperative Parallel/ReactiveSequence/ReactiveFallback, Inverter, ForceSuccess/ForceFailure, bounded Retry/Repeat/KeepRunningUntilFailure, RunOnce, cooperative Timeout and non-blocking Delay.
- Status-predicate/selector-driven IfThenElse, Precondition, WhileDo and Switch. IfThenElse, Precondition and WhileDo also accept an in-tree condition child, keeping condition state and cancellation in the same tree.
- Host-context adapters for PipelineSequence, EntryUpdated and ConsumeQueue. They use typed callbacks; optional abort hooks clean adapter-owned state on interrupted pipeline/queue work. The library does not impose Praktor's blackboard or workflow value representation.
- SUCCESS / FAILURE / RUNNING transitions with per-control-node state retained across ticks. Retry attempts, Repeat cycles, KeepRunningUntilFailure iterations and WhileDo iterations are bounded by the tree configuration (default 64, hard maximum 4096). WhileDo executes at most one body iteration per tick and retains its activation-wide iteration count across RUNNING ticks; this intentionally bounds per-tick work and differs from legacy Praktor `actions`, which can complete multiple successful iterations in one call. The Praktor migration corpus must keep this difference explicit and verify its effect on shipped workflows.
- Full topology validation before leaf side effects, plus explicit cancel/reset/destruction; API errors abort active execution, clear persistent caches and halt descendants. Cancel halts active descendants and retains tree topology for reuse.
- Reentrancy guards: attempts to reset/destroy/mutate the same tree from tick/halt callbacks return `SALTS_EBUSY`.
- Monotonic cooperative Timeout/Delay. Timeout invokes leaf halt callbacks for active descendants; Delay never blocks the driver.
- A single synchronous driver. All callback user contexts are borrowed, never freed by the tree, and must remain valid until tree destruction succeeds; callbacks run on the driver thread and reentrant tree mutations are rejected. Leaf callbacks return behavior status only: represent operational errors as FAILURE and keep detailed diagnostics in host-owned state. Invalid leaf statuses are API errors and trigger lifecycle cleanup. This runtime creates no hidden worker threads and never launches processes.
- Cooperative Parallel advances each branch at most once per parent tick; terminal branches are reactivated on the next tick whenever another branch keeps the overall Parallel node RUNNING, matching Praktor's current executor. Host hooks run enter → step (after each active branch tick) → join (once after all branches tick, before the parent resumes) → finish (terminal) or abort (interruption/error); join commits staged branch outputs in deterministic child order. ReactiveSequence reticks from the first child and halts children to the right when an earlier child blocks or fails.
- CTest coverage for state transitions, predicate/selector routing including condition RUNNING and invalid status rejection, ReactiveFallback priority re-evaluation, Parallel RUNNING-over-FAILURE aggregation, terminal-branch reactivation and per-tick merge/join errors, queue/pipeline/change-detection adapters, cancellation through reset/halt, Parallel branch begin/finish/abort failures and branch lifetimes across resetting decorators, invalid topology, callback errors and capacity limits.

Tree node IDs are stable for the tree lifetime; the root depth is one. Ordinary internal subtree re-entry preserves terminal RunOnce and EntryUpdated caches, matching Praktor's ExecutionContext lifetime; explicit tree reset/cancel clears them. Retry's limit is maximum attempts including the first attempt; Repeat's limit is its terminal child-cycle count. RUNNING preserves the active child for later ticks. Pipeline prepare/complete callbacks run once on their specified transitions. If a child is interrupted, the optional pipeline abort runs after descendant halt/reset. EntryUpdated's change callback owns its comparison snapshot. ConsumeQueue begin/finish callbacks own queue and item data; an empty/exhausted queue fails without ticking the child, matching Praktor's current `ConsumeQueue` behavior. An active item is aborted after descendant halt/reset if interrupted, but not after successful finish. User callbacks run synchronously on the single driver thread.

## Not yet implemented

This remains **partial parity**, not a completed replacement for Praktor's current `actions` runtime:

- Tree-local blackboard storage. Parallel enter/step/join/finish/abort callbacks let the host create/select isolated branch contexts, stage branch progress, commit outputs once after all branches tick but before the parent resumes, close terminal branch activations and abort interrupted branches. If the aggregate result is RUNNING, terminal branch state is reset for the next activation while active RUNNING branches retain their state. Join should commit atomically; on join error, prepared contexts are aborted. The library does not dictate blackboard snapshot/merge policy or store values.
- Workflow-level cancellation/deadline integration and any asynchronous host-completion adapter beyond cooperative leaf polling and halt callbacks.
- SubTree expansion is left to Praktor's compile-time adapter, which should resolve and expand named trees while preserving independent node state.
- Full node-by-node differential tests against the current Praktor `actions` behavior. Any known semantic mismatch must be documented before removing the C++ dependency.

No YAML/DSL parser, process owner, native Plugin loader, or CFlow-backed scheduler is added here. Praktor continues to parse YAML and map it to typed nodes. Subprocess leaves should use `Salts::Process`; Salts Plugin/ServiceHost retains native module lifetime. CFlow remains a separate graph/execution abstraction.

## Build

The root SaltsUtils build installs the target as `Salts::BehaviorTree`:

```cmake
find_package(SaltsUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::BehaviorTree)
```

Public header: `salts/behavior_tree.h`.
