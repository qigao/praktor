# Core-only Praktor Runtime

## Context

Praktor currently hard-links the TurboScript-backed `script:` engine into every
runtime build. That remains the correct default distribution, but it prevents
deployments that deliberately prohibit inline scripting from using a smaller
runtime and makes hosted integration difficult when TurboScript is not available
as an installable package.

## Decision

Add `ENABLE_SCRIPT_ENGINE`, default `ON`.

When enabled, runtime behavior and dependencies remain unchanged.

When disabled:

- `TurboScript::TurboScript` is not discovered or linked.
- the public YAML grammar is unchanged.
- workflows without `script:` retain normal DAG and runner behavior.
- `WorkflowExecutor` rejects any task set containing `script:` in its
  constructor, before scheduling any task.
- a disabled `Praktor::Script::execute` backend remains linked as a defensive
  invariant if an internal caller ever bypasses executor validation.
- the C ABI keeps ABI major 2 and advertises script availability through
  `PRAKTOR_CAPABILITY_SCRIPT_ENGINE`.
- the installed CMake package requires TurboScript only for script-enabled
  builds.

## Why validate at WorkflowExecutor

`WorkflowRunner` is not the only workflow execution entry. The `uses` runner
parses a nested workflow and constructs another `WorkflowExecutor` directly.
The executor constructor is therefore the narrowest shared boundary that runs
before task side effects for both top-level and reusable workflows.

Dynamic task templates currently generate command/Orch tasks only, so they do
not introduce a script-bearing task after executor construction.

## State and failure semantics

The parsed workflow/task list remains the single source of truth. Core-only mode
does not rewrite tasks, drop script fields, or maintain a second representation.

If any task contains `script:`, executor construction throws before scheduling.
`WorkflowRunner::execute` converts that into the existing canonical failed
workflow result. The C API therefore returns
`PRAKTOR_RESULT_EXECUTION_FAILED` with canonical JSON rather than introducing
a second error protocol.

No earlier command or action is allowed to run before this rejection.

## ABI and package compatibility

This is additive:

- default `ENABLE_SCRIPT_ENGINE=ON` behavior is unchanged;
- YAML syntax is unchanged;
- C ABI entrypoints and ABI major/minor remain unchanged;
- `PRAKTOR_CAPABILITY_SCRIPT_ENGINE` is a new capability bit;
- core-only installs omit the TurboScript package dependency.

Embedding hosts can therefore distinguish a full runtime from a core-only
runtime through `praktor_get_api()->capabilities`.

## Migration and rollback

No migration is required for current users because the option defaults to ON.
A deployment choosing core-only must ensure its registered workflows and
transitive `uses` workflows contain no `script:`.

Rollback is simply rebuilding with `ENABLE_SCRIPT_ENGINE=ON`.

## Verification

The script-enabled test matrix remains unchanged.

Core-only builds use focused WorkflowRunner and C ABI tests proving that:

1. a command-only workflow succeeds;
2. a script-bearing workflow is rejected before an earlier command can create
   an observable side-effect file;
3. core-only ABI still advertises JSON workflow execution;
4. core-only ABI does not advertise the script engine;
5. the C API returns normal canonical failure JSON for a script-bearing
   workflow.
