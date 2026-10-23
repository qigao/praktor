# Experimental C11 execution boundaries

PR #78 is merged. `Legacy` remains the accepted workflow default while the
[complete C11 migration](../../docs/c11-migration.md) is in progress. Independent
C execution targets alone do not establish whole-workflow parity.

| Target | Owner and dependencies |
| --- | --- |
| `praktor_c11_bt_core` | C11 topology/state and Salts::BehaviorTree; no C++ compiler required |
| `praktor_c11_value_store` | Fixed-capacity C11 values and optimistic transactions |
| `praktor_output_core` | Allocation-free C text scanner and bounded key/value JSON encoder; no SDK dependency |
| `praktor_c11_native_host` | C11 value-store owner and preallocated parameter scratch; typed `SetVariable`, `CheckExitCode`, cooperative `Sleep`, unfiltered `ParseLines` and `ParseKeyValue` leaves |
| `praktor_process_driver` | C11 Process I/O/retainer driver plus optional C11 joined worker with Salts thread, cancellation, synchronized observers and bounded admission copies; no C++ runtime |
| `praktor_service_control` | Bounded C service state machine and numeric STATE parser; base Salts::Platform only |
| `praktor_service_process_host` | Explicit service command binding to the joined C Process worker; no shell parser or C++ runtime |
| `praktor_process_native` | C11 persistent-application controller and bounded Windows host; base Salts::Platform only, no Shell driver or C++ runtime |
| `praktor_c11_session_core` | C11 control compiler and entire Session runtime: owned bounded typed plan, dispatch/activation ownership, dynamic controls, pipeline/entry/JSON queue state and Salts::Process; no C++ compiler or runtime |
| `praktor_c11_session` | Construction-only C++ AST compiler and exception boundary over `praktor_c11_session_core`; no legacy execution symbols |
| `praktor_actions_c11` | Explicit compatibility constructors, legacy Blackboard access, Executor dispatch, environment and Shell log adaptation |

The independent Session uses the host-only constructor, a construction-only
SubTree resolver and `praktor_c11_native_host_bind()` (or explicit host hooks).
C callers use `plan.h` to compile action names, arity and typed control
parameters, then `session.h` directly and link `praktor_c11_session_core`.
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

Native `ParseKeyValue` resolves typed/dynamic input, delimiter, separator and
output ports. Defaults remain `shell_output`, `=`, LF and `parsed_output`.
It emits the same sorted, string-valued JSON object as Legacy's map and SDK
serializer: first delimiter per line, space/tab/CR/LF trim, last duplicate wins,
empty keys/values and empty delimiters preserved. Missing input is FAILURE.
All pair workspace (`max_value_bytes / 2 + 1` entries) and five parameter/input/
output scratch buffers are allocated at Host init. Scanning, sorted insertion
and JSON escaping allocate nothing; insertion is quadratic in the bounded pair
count. Overflow or invalid arguments leave the existing output untouched, even
when output aliases input. Empty line separators now fail explicitly in both
paths; Legacy previously could loop forever. The length-aware shared scanners
preserve binary input for legacy containers, while the JSON encoder rejects
embedded NUL; Native Host's value API already uses NUL-terminated strings.

`actions/src/output_parser.cpp` has been replaced by the explicit
`output_parser_legacy_bridge.cpp`: C++ regex and JSON DOM compatibility remain
there, but line scanning, pair splitting/trimming and substring search delegate
to `actions/core/output_scan.c`. The independent `praktor_output_core` archive
has no SDK, DOM, allocator, Executor or C++ runtime dependency.

The legacy adapter borrows Executor, Blackboard and ExecutionContext until
session destruction. Process callbacks now touch only a stable C driver payload.
The driver detaches its borrowed stream callback/user before closing admission;
retained callbacks never reference the Session, AST, adapter or host value hooks.
The Session owns Impl uniquely; Process no longer retains a shared C++ Impl.
The retainer owner must keep driving until native leases settle. No forced
release is introduced.

Validation projects:

- `actions/output-standalone`: pure C scanner/encoder with base SDK TinyTest only;
  link and archive audits reject C++/DOM and allocation dependencies.
- `actions/c11/standalone`: pure C adapter, store and Native Host tests.
- `shared/process/standalone`: C compiler/linker only, real duplex child I/O,
  capture limits, callback failure, saturation and zero-budget handoff after
  callback-user destruction. The joined worker suite additionally tests copied
  argv/environment/binary input, bounded admission, setup failure, pre-admission
  cancellation, concurrent observers, callback reentrancy, timeout, overflow,
  failed spawn and teardown before callback storage destruction. The application
  controller suite covers typed arguments, idempotence, restart ordering, startup
  compensation against an exact instance despite replacement queries, late native
  calls, stop escalation, error propagation and counter wrap with a controlled
  monotonic clock. Service suites additionally exercise restart order, all terminal
  command outcomes, bounded template expansion, deadline charging, reentrancy,
  and actual C fixture commands through the C Process host. All six executables
  reject C++ ABI/runtime dependencies and run against baseline and P1 profiles.
- `shared/process/native-standalone`: `LANGUAGES C`, base Salts SDK only; rejects
  C++ compiler, Legacy and Salts::Process targets. TinyTest directly exercises
  native ownership, exact Unicode/session/token matching and evidence errors.
  Windows additionally runs real escaped argv, controller start/stop, stale-token
  rejection, persistent application survival after host destruction, bounded
  invalid admission and repeated failed-start recovery without handle growth.
- `actions/c11/session-c-standalone`: a `LANGUAGES C` project that rejects C++
  compiler/target loading. TinyTest exercises owned plan lifetime, bounded state,
  Switch/conditions/dynamic Retry and Timeout, pipeline/entry/JSON queue semantics,
  pinned Host owners, first-tick failure cleanup, reentrancy, Sleep cancellation,
  real Shell execution and retainer transfer after Host destruction. The shared
  `audit-c11-runtime-link.py` rejects C++ execution symbols and runtime libraries.
  The former C++ plan test has been migrated to `plan_test.c` / TinyTest, with
  all 13 scenario groups retained; AST source-pointer checks remain in the
  independent construction bridge test. Core-only and complete-source TSan run
  this project, and the C11 ASan/UBSan root slice runs both C executables.
  Native SDK separately protects released-SDK compatibility.
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
  ParseKeyValue compares 8,190 short data/delimiter/LF/space combinations with
  Legacy's SDK JSON serializer through YAML, including dynamic multi-byte ports,
  duplicate keys and input/output aliasing. C tests cover JSON escaping, exact
  bounds, failed publication and cancellation before parse.
  The value-store, Native Host, independent Session and YAML differential runners
  use Salts::TinyTest, including native C tests in the C-only build.

`tree_executor_session.cpp`, `tree_plan.cpp` and the private C++
`plan_value_reader.hpp` have been removed. Its runtime no longer exists in
C++; the construction-only `session_ast_bridge.cpp` delegates node compilation and
every execution operation to
the same C implementation used by direct C callers. C++ exceptions are contained
in the facade and cannot unwind through the C runtime. Compatibility dispatch is
an explicit optional callback group; the C archive has no Executor, Blackboard,
ExecutionContext or C++ ABI dependency.

The C++ AST traversal/conversion, YAML/Workflow/SDK/CLI layers and explicit Legacy
value/callback facades still remain. `shared/shell/src/managed_process.cpp` has
been removed; `process_legacy_bridge.cpp` contains only validation and C++
value/callback conversion. All process scheduling, cancellation, synchronized
completion and waiting now belong to `praktor_process_worker` in C. Its owner
always joins; it never detaches a live driver or drops native leases. Removing all
C++ is the migration goal; this step does not yet replace the default backend.
Full WorkflowContext migration, all built-in leaves, native event queues,
branch-transaction integration and complete YAML/node parity remain open.

Persistent application control is a separate synchronous C state machine in
`shared/process/src/process_control.c`: bounded stack state, no allocations,
typed query/start/stop/terminate hooks, and Salts monotonic time/sleep. Backend
call time consumes startup/stop deadlines. It preserves the original started
PID/session/token/image for timeout compensation and re-queries before forced
stop. It leaves successful persistent applications alive. Its borrowed string
lifetimes are explicit in `process_control.h`; start and query identity storage
must be independent, and failed queries must preserve the last successful image.
Callbacks cannot be preempted and must themselves be bounded by the host.

`praktor/src/system/managed_process.cpp` is removed. The explicit
`process_control_legacy_bridge.cpp` only converts legacy values, pins separate
successful start/query snapshots and contains/rethrows exceptions around the C
boundary. All existing controller/executor tests also run with the script engine
disabled, including real Windows identity/instance tests. ASan/UBSan and the
complete-source TSan slice include this bridge suite. The C application-control
test additionally links and runs without any C++ compiler/runtime in both SDK
profiles. `managed_process_win.cpp` is also removed.
`process_native_legacy_bridge.cpp` performs only legacy value conversion and
explicit ownership of the C host. Its start result storage is reserved before
native admission; no C++ allocation is needed to publish successful startup.

The Windows host in `shared/process/src/process_native.c` owns native handles,
Unicode/path/argv conversion, enumeration and exact instance validation. Its
fixed buffers are allocated once at init (32768 UTF-16 units per path/command,
131072 bytes per UTF-8 identity and 1024 bytes of diagnostics). Failed admission
retains one cleanup handle until actual process exit; later admissions cannot
bypass it. Bounded close returns EBUSY/EIO without discarding ownership. An
explicit synchronous compatibility destroy joins cleanup and can block on a
permanent native fault. Successful persistent applications outlive host close.
This host is separate from Shell/Program, which continues using Salts::Process.
Other platforms explicitly report unsupported, preserving existing behavior.
Windows evidence loss during teardown gets one bounded 25 ms wait on the same
process handle before classification; only a signaled handle means exited.
Timeout/wait failure still returns the native error. The controller charges
the wait against its deadline. Windows tests cover pending/signaled/invalid
kernel objects and the real graceful-stop path. Their fixture is now compiled
and linked as C (`managed_process_fixture.c`), without a C++ entry point.


`praktor/src/system/service_controller.cpp` has been removed. Its replacement,
`service_control_legacy_bridge.cpp`, owns only the immutable Legacy profile map,
value conversion, C owner scope and exception containment. The C state machine
in `shared/process/src/service_control.c` pre-renders all three command templates
before dispatch and rejects configured argument/metadata bounds without partial
render admission. Callback time consumes one operation deadline; late successful
mutations retain `changed=true` but cannot authorize later commands. This also
closes the old late-status-query success hole. Polling and parsing allocate no
memory. C state, diagnostics and argv scratch are owned from init until destroy.
Default compatibility bounds are 4096 arguments per command, 1 MiB aggregate
render metadata (including argv pointers) and 16 MiB captured command output.
Embedded NUL in Legacy metadata is rejected before command dispatch.

The separate `service_process_host.c` binds that typed command interface to the
existing Salts::Process C worker. It retains the completed worker while output
is borrowed, joins/reclaims it before the next command, and joins again at host
close. There is no shell expansion or Legacy fallback. Native commands can fail
after external service effects; this synchronous controller does not promise
rollback or preemption of host callbacks. Cancellation remains an explicit
terminal command result. It does not introduce a default-backend switch.

Base-SDK-only `native-standalone` also links/runs the service controller test
without loading Salts::Process or a C++ compiler. `shared/process/standalone`
runs the real service fixture through the explicit C Process binding on Linux
and Windows; root builds run it across SDK platforms. Existing Service YAML and
compatibility tests remain in the SDK gate, and ASan/UBSan/full-source TSan now
include both service C suites plus the compatibility executor suite.
