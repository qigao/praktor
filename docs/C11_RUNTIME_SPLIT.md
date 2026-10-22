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
