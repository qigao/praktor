# Orch to C11 Session plans

`praktor/include/actions/orch_session_plan.h` declares internal plan construction over the
immutable Orch AST. Plans use the existing C11 compiler and Session; they own
copied action/parameter descriptors rather than a second YAML parser or runtime.

## Ownership and admission

Construction uses iterative preorder traversal and the explicit plan/tree
limits. On failure, the caller's output remains unchanged. On success, input
YAML and Orch storage can be released. Session initialization deep-copies the
plan descriptors, so the plan can be destroyed before the first tick.

The default profile admits its native control/leaf subset. Explicit native
Shell and Command constructors select additional shapes. Command profiles
require canonical Sequence/Shell/parser structure and matching stdout ports;
unsupported shapes fail before any process side effect. Postprocessor helpers
remain internal tested APIs and do not enable those compositions through the
installed application workflow entrypoint.

The application runtime chooses the admitted plan profile before executing
any DAG task. It currently supports bounded serial SetVariable, direct Shell
and single-Command tasks. It rejects scripts, includes, dynamic plans and
application-level Parallel.

## Execution

A Session borrows one primary host and its callbacks until destruction. Native
Shell reserves recovery capacity before spawn and uses the shared process
driver over SaltsBase `process_async`. CFlow owns callback admission and
settlement; the application Owner owns DAG state and task output publication.

Output keys remain task-local until the Owner passes the native drain and
atomic commit gates. Command stdout is bound to `stdout`; default direct Shell
stdout is bound to `shell_output`. The installed JSON result uses `stdout` for
both. Failure/cancellation does not publish successful branch outputs.

## Formal tests

The root CTest graph contains plan, native Session, worker Session, real Shell
recovery, DAG publication, C API and CLI tests. Their sources remain in
`praktor/test/actions` and `praktor/test`. Current public contracts come from
those headers and executable cases, not a separate candidate bridge or build.
