# Experimental C11 execution boundaries

PR #78 remains Draft. `Legacy` is still the workflow default. No release or
production switch is implied by these independent execution targets.

| Target | Owner and dependencies |
| --- | --- |
| `praktor_c11_bt_core` | C11 topology/state and Salts::BehaviorTree; no C++ compiler required |
| `praktor_c11_value_store` | Fixed-capacity C11 values and optimistic transactions |
| `praktor_c11_native_host` | C11 value-store owner and preallocated parameter scratch; typed `SetVariable`, `CheckExitCode`, cooperative `Sleep` and unfiltered `ParseLines` leaves |
| `praktor_process_driver` | C11 Process I/O, bounded capture, close and explicit retainer handoff; no C++ state, thread or runtime dependency |
| `praktor_c11_session_core` | Entire C11 Session runtime: owned bounded typed plan, dispatch/activation ownership, dynamic controls, pipeline/entry/JSON queue state and Salts::Process; no C++ compiler or runtime |
| `praktor_c11_session` | Construction-only C++ AST compiler and exception boundary over `praktor_c11_session_core`; no legacy execution symbols |
| `praktor_actions_c11` | Explicit compatibility constructors, legacy Blackboard access, Executor dispatch, environment and Shell log adaptation |

The independent Session uses the host-only constructor, a construction-only
SubTree resolver and `praktor_c11_native_host_bind()` (or explicit host hooks).
C callers instead use `session.h` directly and link `praktor_c11_session_core`.
`praktor_c11_session_init()` deep-copies a validated preorder plan and typed
parameters. Construction storage can be released immediately after init. The
configuration bounds plan bytes, parameters per node, each resolved value and
aggregate cached state; tree limits bound node/depth/iteration counts. Scratch
and caches grow within these budgets; JSON queue parsing allocates per visit.
This is bounded memory, not an allocation-free tick guarantee. Host callbacks
and the external Process retainer remain borrowed for the Session lifetime.

Typed requests preserve string/number/boolean ports. Payloads refer to the
C-owned compiled plan and must not be retained past their callback. A dispatcher
returning RUNNING owns that activation until completion or `halt_leaf`; it
cannot decline on resume and transfer ownership. Select either typed or indexed
leaf dispatch, and provide exactly one indexed or typed halt callback. Unsupported native leaves fail closed;
Shell can use the existing Salts::Process path with an explicit retainer.

The Native Host allocates all key/value and parameter scratch memory at init.
It is single-driver and must outlive the session. A failed leaf write (including
capacity exhaustion) leaves that value unchanged. These limits do not make the
C++ AST bridge allocation-free or provide a transaction for an entire Sequence.
Parallel still requires branch-aware host lifecycle/dispatch hooks; the new
host does not silently use root values inside branches. Atomic value-store group
commit is available separately; automatic Parallel integration remains open.

Positive-duration `Sleep` uses fixed-capacity native activation slots configured
through `praktor_c11_native_host_init_with_config`. The convenience initializer
reserves zero slots; positive durations then fail with `SALTS_ENOBUFS` rather
than blocking or delegating to Executor. Zero-duration Sleep needs no slot.
All waiting states are allocated at Host init. Admission snapshots the resolved
duration; later Blackboard changes apply only to the next activation. Each slot
is scoped by the ephemeral Session identity plus node index. Typed halt clears
only that activation, including on cancel/reset/destruction, Timeout and driver
errors. Two Sessions sharing one Host cannot cancel each other's equal indices.
The optional monotonic clock callback and user storage are borrowed for Host life;
they must be single-driver, non-blocking and return nanoseconds.

Supported Sleep inputs are finite non-negative numeric milliseconds or strings
with no suffix / `ms` / `s` / `m` / `h`, truncated to integer milliseconds in
`[0, INT_MAX]`. Bool, overflow, negative/non-finite values and unknown suffixes
fail closed. Legacy Sleep blocks its calling thread and treats unknown suffixes
as milliseconds; those behaviors are intentionally not copied. Differential
validation compares supported terminal outcomes and values, while native tests
cover cooperative RUNNING, cancellation and exact deadlines independently.

Native `ParseLines` supports omitted/empty `filter`, including exact `{key}`
substitution. It uses existing bounded scratch, with no per-tick allocation.
`input_key` / `output_key` default to `shell_output` / `parsed_lines`, as in
Executor (YAML still requires `input_key`). It preserves Legacy's actual
`getline` and join behavior: CR is retained, a final LF does not add a line,
leading empty lines contribute to the count but not leading separators, and
subsequent empty lines retain their separators. A missing input is FAILURE.
Content and the derived `<output_key>_count` are published together through
the allocation-free `praktor_c11_value_store_set_many` preflight API. Capacity,
key/value length or invalid-key errors publish neither output; a Sequence stops
before downstream effects. Duplicate batch keys are rejected.

Legacy filters use `std::regex_search` (and ignore invalid regexes), rather than
substring matching. Native nonempty filters return `SALTS_ENOTSUP` before any
write. Regex support remains open; the independent Session cannot fall back to
Executor. Parallel branch requests remain explicitly unsupported by this Host.

The legacy adapter borrows Executor, Blackboard and ExecutionContext until
session destruction. Process callbacks now touch only a stable C driver payload.
The driver detaches its borrowed stream callback/user before closing admission;
retained callbacks never reference the Session, AST, adapter or host value hooks.
The Session owns Impl uniquely; Process no longer retains a shared C++ Impl.
The retainer owner must keep driving until native leases settle. No forced
release is introduced.

Validation projects:

- `actions/c11/standalone`: pure C adapter, store and Native Host tests.
- `shared/process/standalone`: C compiler/linker only, real duplex child I/O,
  capture limits, callback failure, saturation and zero-budget handoff after
  callback-user destruction. A link audit rejects C++ ABI/runtime dependencies.
- `actions/c11/session-c-standalone`: a `LANGUAGES C` project that rejects C++
  compiler/target loading. TinyTest exercises owned plan lifetime, bounded state,
  Switch/conditions/dynamic Retry and Timeout, pipeline/entry/JSON queue semantics,
  pinned Host owners, first-tick failure cleanup, reentrancy, Sleep cancellation,
  real Shell execution and retainer transfer after Host destruction. The shared
  `audit-c11-runtime-link.py` rejects C++ execution symbols and runtime libraries.
  Core-only and complete-source TSan run this project; Native SDK and ASan/UBSan
  run the same C tests through the root build.
- `actions/c11/session-standalone`: independent C++ construction bridge link/run, including typed
  dispatch, values/capacity, cancellation/dynamic Timeout and real Process
  handoff after host destruction. Legacy targets do not exist in this project.
- `cmake/ci/audit-c11-session-link.py`: rejects legacy execution symbols in that
  executable. Core-only and complete-source TSan run the independent project.
- `actions_c11_session_yaml_differential_test`: real YAML parsing, native vs
  legacy values/copy semantics, exit-code success/failure and malformed-value
  rejection before later leaf effects. Supported Sleep durations compare terminal
  Blackboard effects with Legacy; a controlled native clock also verifies waiting,
  cancel-before-next-leaf, and fresh admission after cancel. Existing workflow /
  lifecycle tests continue exercising the compatibility entry point.
  ParseLines additionally compares 1,093 exhaustive short data/LF/CR strings
  through actual YAML, dynamic ports, empty filters and unchanged source values;
  native tests cover capacity/key-bound atomicity and cancellation before parse.
  The value-store, Native Host, independent Session and YAML differential runners
  use Salts::TinyTest, including native C tests in the C-only build.

`tree_executor_session.cpp` has been removed. Its runtime no longer exists in
C++; the construction-only `session_ast_bridge.cpp` delegates every operation to
the same C implementation used by direct C callers. C++ exceptions are contained
in the facade and cannot unwind through the C runtime. Compatibility dispatch is
an explicit optional callback group; the C archive has no Executor, Blackboard,
ExecutionContext or C++ ABI dependency.

The C++ AST compiler, YAML/Workflow/SDK/CLI layers and explicit Legacy facade
(including observer synchronization and scheduling) still remain. Removing all
C++ is the migration goal; this step does not yet replace the default backend.
Full WorkflowContext migration, all built-in leaves, native event queues,
branch-transaction integration and complete YAML/node parity remain open.
