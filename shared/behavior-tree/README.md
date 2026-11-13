# C11 Behavior Tree

This repository owns the bounded C11 shared/behavior-tree implementation. The build
alias is `Salts::BehaviorTree`; the installed Praktor package exports
`Praktor::BehaviorTree`. The public header is `salts/behavior_tree.h`.

## Execution contract

A tree has one root, bounded allocation and parent-before-child construction.
Topology is validated before leaf side effects. Node IDs remain stable for the
tree lifetime. One synchronous driver owns mutation and execution; callbacks
borrow their contexts until successful destruction. Reentrant tree operations
return `SALTS_EBUSY`.

Leaf callbacks report SUCCESS, FAILURE or RUNNING. Operational diagnostics stay
with the host. RUNNING preserves active control state; cancel/reset halts active
leaves and clears retained caches. Ordinary subtree reentry preserves terminal
RunOnce and EntryUpdated caches.

The implementation includes Sequence/Fallback, reactive controls, bounded
Retry/Repeat/WhileDo, status decorators, predicates/selectors, monotonic Timeout
and cooperative Delay. WhileDo advances at most one body iteration per tick.
Parallel advances each branch once per parent tick and reactivates terminal
branches while another branch remains RUNNING. Host enter/step/join/finish/abort
hooks own isolated branch data and publication. PipelineSequence, EntryUpdated
and ConsumeQueue likewise use explicit host lifecycle hooks.

Values, YAML parsing, workflow DAG state, processes and scheduling belong to
their owning Praktor or SDK modules. The tree creates no threads or processes.
The application's admitted workflow subset remains narrower than the internal
node API; see [the runtime contract](../docs/C11_RUNTIME_RESET.md).

## Build and validation

Use the complete root user preset. `BUILD_TESTS` enables the formal state,
capacity, callback, cancellation and lifecycle tests in this module, including
its C++17 public-header test. Installed consumers use:

```cmake
find_package(Praktor CONFIG REQUIRED)
target_link_libraries(app PRIVATE Praktor::BehaviorTree)
```

Dependency setup and CTest commands are in [the root build guide](../README.md#building-and-qualification).
