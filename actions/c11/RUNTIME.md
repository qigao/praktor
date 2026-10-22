# Experimental C11 execution boundaries

PR #78 remains Draft. `Legacy` is still the workflow default. No release or
production switch is implied by these independent execution targets.

| Target | Owner and dependencies |
| --- | --- |
| `praktor_c11_bt_core` | C11 topology/state and Salts::BehaviorTree; no C++ compiler required |
| `praktor_c11_value_store` | Fixed-capacity C11 values and optimistic transactions |
| `praktor_c11_native_host` | C11 value-store owner and preallocated parameter scratch; typed `SetVariable` and `CheckExitCode` leaves |
| `praktor_c11_session` | C++ AST compiler/session over C11 state, JSON queue hooks and Salts::Process; no legacy execution symbols |
| `praktor_actions_c11` | Explicit compatibility constructors, legacy Blackboard access, Executor dispatch, environment and Shell log adaptation |

The independent Session uses the host-only constructor, a construction-only
SubTree resolver and `praktor_c11_native_host_bind()` (or explicit host hooks).
Typed requests preserve string/number/boolean ports. Payloads refer to the
session-owned AST and must not be retained past their callback. A dispatcher
returning RUNNING owns that activation until completion or `halt_leaf`; it
cannot decline on resume and transfer ownership. Select either typed or indexed
leaf dispatch, and always provide halt. Unsupported native leaves fail closed;
Shell can use the existing Salts::Process path with an explicit retainer.

The Native Host allocates all key/value and parameter scratch memory at init.
It is single-driver and must outlive the session. A failed leaf write (including
capacity exhaustion) leaves that value unchanged. These limits do not make the
C++ AST bridge allocation-free or provide a transaction for an entire Sequence.
Parallel still requires branch-aware host lifecycle/dispatch hooks; the new
host does not silently use root values inside branches. Atomic value-store group
commit is available separately; automatic Parallel integration remains open.

The legacy adapter borrows Executor, Blackboard and ExecutionContext until
session destruction. Retained Process callbacks touch only stable ShellState;
they never call the adapter or host value hooks after the session is destroyed.
The retainer owner must keep driving until native leases settle. No forced
release is introduced.

Validation projects:

- `actions/c11/standalone`: pure C adapter, store and Native Host tests.
- `actions/c11/session-standalone`: independent Session link/run, including typed
  dispatch, values/capacity, cancellation/dynamic Timeout and real Process
  handoff after host destruction. Legacy targets do not exist in this project.
- `cmake/ci/audit-c11-session-link.py`: rejects legacy execution symbols in that
  executable. Core-only and complete-source TSan run the independent project.
- `actions_c11_session_yaml_differential_test`: real YAML parsing, native vs
  legacy values/copy semantics, exit-code success/failure and malformed-value
  rejection before later leaf effects. Existing workflow/YAML/lifecycle tests
  continue exercising the compatibility entry point.

Full WorkflowContext migration, all built-in leaves, native event queues,
branch-transaction integration and complete YAML/node parity remain open.
