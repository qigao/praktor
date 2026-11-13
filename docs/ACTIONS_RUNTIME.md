# Session and Native Host

The Session API is declared in `praktor/include/actions/session.h`; the host
callbacks and typed leaf request are declared in `actions/tree_adapter.h`.
`actions/native_host.h` provides the bounded
native implementation used by the application runtime.

## Construction and ownership

Initialize a zero-state Session with `praktor_session_init`. Configuration
limits bound node count, plan text, parameter count, values and retained state.
Plan descriptors and text are copied. The primary host callbacks and their
contexts remain borrowed until Session destruction.

A complete host value contract supplies get, set, contains and its user context.
Leaf dispatch selects indexed or typed tick, with one halt callback. Missing or
inconsistent contracts fail initialization before callbacks. There is one host
value authority and no alternate compatibility dispatcher.

A single driver owns tick, reset, cancel and destroy. Callback reentry is
rejected. A leaf returning RUNNING retains its host owner until completion or
halt; a resumed ENOTSUP cannot transfer that activation. Partially acquired
host activations are halted when their first tick fails.

## Native leaves and controls

The Native Host supplies bounded values, expression/regex helpers, output
parsers and cooperative waiting leaves. Unsupported leaves return ENOTSUP.
Session handles its admitted control-node semantics through the behavior tree
and explicit host hooks. Parallel, pipeline and queue hooks have paired cleanup
contracts; incomplete hook sets are rejected.

When the host declines a newly admitted Shell, Session uses
`praktor_process_driver`. An explicit retainer reservation precedes spawn.
Close detaches application callbacks, drives native I/O settlement and transfers
only the complete C payload when a reserved recovery slot is required. Value
publication and process retirement remain checked operations. See
[the process owner contract](PROCESS_OWNER.md).

The internal plan/compiler APIs support more tested compositions than the
installed workflow entrypoint admits. A passing control/leaf test does not
enable application-level Parallel, scripts, includes or dynamic plans.

## Validation

Build the complete root user preset and run CTest. The existing Session suite
covers copied plan storage, missing primary contracts, live control values,
owner pinning, callback failure, reset/cancel, native Shell and delayed cleanup.
The Native Host, plan, worker, cohort and runtime suites cover their respective
owners. All tests remain under the existing module directories.
