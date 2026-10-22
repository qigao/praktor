# C11 runtime dependency split (Praktor #78)

This first split isolates a C11-only tree/runtime adapter. It does not
declare the whole Praktor workflow runtime C-only or authorize a default
backend switch. PR #78 stays Draft until the remaining gates pass.

| Target | Language | Runtime dependency |
| --- | --- | --- |
| `praktor_c11_bt_core` | C11 | installed `Salts::BehaviorTree` |
| `praktor_actions_c11` | transitional C++17 | C core, `actions`, `Salts::Process` |
| `praktor_lib` | C++17 | current `actions` default plus opt-in C11 bridge |

The Praktor-facing `DeclarativeTreeExecutor` public header now forward
declares legacy AST/Blackboard types instead of importing the complete
`core/executor.hpp`. The latter is implementation-local, and the
legacy `actions` library is a PRIVATE link (not a public compile
requirement) for `praktor_lib`. The distinct `praktor_shell` target is
now declared explicitly PUBLIC because the existing
`execution/controlled_process.hpp` public header directly includes
`praktor/shell/shell_executor.hpp`; depending on the legacy
`actions` link to leak this include path was incorrect. The standalone public-header object
test compiles this contract in Legacy-default and C11-enabled builds.

`actions/c11/standalone` is independently configured as `LANGUAGES C`.
It links the real installed BehaviorTree SDK and runs the C adapter
conformance tests with no C++ compiler or `actions` target, also
under full-source TSan in dedicated CI.

Remaining cuts before the C++ action runtime can be removed:

1. Compile a typed tree plan from validated `OrchNode` without
   `actions::Node` / `actions::Value`, retaining deterministic
   malformed-node and SubTree fail-closed semantics.
2. Replace `actions::Blackboard` / `ExecutionContext` with bounded
   host-owned typed values and explicit Parallel branch snapshot, merge,
   abort and cancellation contracts.
3. Remove `actions::Executor` dispatch and any legacy Shell fallback.
   Shell must use the single Salts::Process owner and retainer.
4. Pass Process destroy-error and permanent-driver fault interleavings,
   real host-driven recovery and exact-source ASan/UBSan/TSan gates.
5. Pass full YAML/WorkflowContext/node semantics differential tests,
   then remove `actions` from the production `praktor_lib` closure
   and verify the installed public C ABI.

No C++ runtime removal is claimed for the current C++ WorkflowRunner.

## Header dependency cut — C11 session API

The C11 tree-plan/session public headers now depend on the action AST model
and forward declarations for `Blackboard`, `Executor`, and
`ExecutionContext`; they no longer transitively include the full legacy
`core/executor.hpp`. Implementation and compatibility tests include the
legacy runtime explicitly where they still use it.

This is an include-graph boundary only, not a claim that the C11 session
is already runtime-independent: its constructor and registered-leaf path
still borrow legacy Executor/Blackboard/ExecutionContext. The next runtime
cut must replace those borrowed objects with a typed C11 plan, typed bounded
blackboard, and explicit host-leaf dispatch before `actions` can be removed
from the production link closure.

A compile-only `actions_c11_session_header_contract` object target now
includes `tree_executor_session.hpp` while linking only the C11 core and
Salts Process usage requirements. Its translation unit intentionally omits
`core/executor.hpp`, guarding this boundary against accidental transitive
reintroduction. It is a compile-contract test; the runtime session still has
legacy Executor references until the next implementation cut.

## Bounded C11 Blackboard foundation

Added a fixed-capacity C11 string store. Initialization reserves all entry/key/value storage; writes never grow the store, and capacity/value-size exhaustion returns explicit errors. It has no dependency on the legacy actions runtime. The normal C11 target and independent C-only SDK project compile and run negative-capacity/size regression tests.

This is a foundation, not yet the production Blackboard replacement: TreeExecutorSession still uses actions::Blackboard for compatibility. The next step is to adapt C11 built-in stateful controls and Shell output mapping to a typed host value interface, then differential-test semantics before removing the legacy Blackboard from the session constructor.

The value store is a separate `praktor_c11_value_store` target with no Salts BehaviorTree link dependency; its unit test links only this library. The standalone C-only project mirrors this separation. This keeps the future host state primitive reusable without bringing in the tree runtime or any C++ action target.

When optional host value callbacks are configured, the Session requires explicit host leaf tick/halt hooks and refuses declined non-Shell leaves instead of running them against a second legacy Blackboard. Built-in Shell remains owned by the C11 Salts::Process path and publishes output through the same host value callbacks. A Session test proves EntryUpdated reads the host store (not a conflicting legacy Blackboard value) and dispatches its child through the host callback. Dynamic numeric decorator resolution still uses the compatibility Blackboard and remains a migration item.


## Static plan compilation without Blackboard lookup

The C11 session now compiles its flattened tree topology through the no-Blackboard `compileTreePlan(tree, limits)` overload. Dynamic scalar control parameters use explicit `{key}` markers and retain tick-time resolution from configured host value callbacks. The compiler receives no Blackboard pointer in this path; unresolved brace substitutions on parameters not classified as runtime-dynamic fail closed. The legacy overload remains for compatibility tests while AST/SubTree ownership is migrated separately.
