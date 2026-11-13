# Praktor C API (ABI v3)

The embedded API is declared in [praktor.h](../praktor/include/praktor.h), with
its wrapper implementation in `praktor/src/runtime/praktor_api.c`. Production
execution is C11; callers supply inline YAML, explicit limits and an optional
cancellation probe. The installed Praktor interface is this single header;
`praktor/runtime.h` is private and is no longer installed. Existing request,
result and limits layouts and the existing execution entrypoints remain unchanged.
`praktor_execute` returns a status and an owning result. Release every result
with `praktor_release_result`, including failed executions with diagnostics.

The CLI calls `praktor_load_environment(yaml, size)` before execution. This
optional entrypoint loads up to 32 root `dotEnv` files in order; paths are
relative to the current directory and later files overwrite earlier/process
values. It borrows YAML up to 1 MiB for the duration of the call and returns
`PRAKTOR_RUNTIME_OK` or `PRAKTOR_RUNTIME_INVALID` on preparation/loading failure.
It runs no tasks. Callers must serialize this process-global environment
mutation with other environment access; a later failure does not roll back
earlier files. `praktor_execute` itself does not mutate the environment.

The admitted runtime supports bounded inline workflows containing SetVariable,
Shell, single-Command and named Tool roots. A workflow may explicitly reference
up to 16 service YAML files using `services`. Execution reads each regular file
(up to 1 MiB) relative to the current working directory, before starting tasks.
The immutable workflow snapshot parser itself performs no file IO. Provider
definitions remain owned by the Configurator host until all jobs are released.
Scripts, includes, dynamic plans and application
Parallel execution remain unsupported.
[shared-downloads.yml](../examples/shared-downloads.yml) demonstrates reusable
tools and service files; curl must be available on PATH. The CLI accepts one
workflow file and task: `praktor --config workflow.yml --task build`.

`praktor_execute_task(request, task, result)` selects one named task and all its
transitive `depends_on` predecessors; other task results are `skipped`. An unknown
or empty task name returns INVALID. This adds an entrypoint without changing
the existing request/result layouts; `praktor_execute` continues to execute the
complete workflow. Neither entrypoint defines root `inputs` or `input_policy`.
Variables resolve task values, root values, then the inherited process
environment, including explicitly loaded dotenv files. Callers must serialize
environment mutation with execution. Environment values obey the native value
budget and are read without inserting them into mutable task state.

Both execution entrypoints resolve per-command defaults once before starting
providers or tasks: initialized Session values (30000 ms, emission enabled),
inherited `PRAKTOR_TIMEOUT_MS` / `PRAKTOR_STREAM_OUTPUT`, then root `defaults`.
An explicit Shell `timeout` or `stream_output` overrides its corresponding
resolved field; lowered Command nodes inherit the same values. A present false
value overrides true. Environment timeout accepts decimal integer milliseconds
from 1 through 2147483647 and the stream flag accepts `true` or `false`.
Document defaults use integer milliseconds and boolean values; duration strings
such as `30s` are now invalid. Every provided environment/document value is
validated even when a later layer overrides it, returning INVALID before task
execution on failure. Environment mutation must remain serialized with execution.
These defaults leave the request/result ABI, overall workflow deadline and
independent provider `config.timeout_ms` unchanged. Emission uses the existing
Session hook; captured task stdout/stderr remain available when disabled.

Build, test and install from the repository root using
`llvm-mingw-x64-release-user` and `install-llvm-mingw-x64-release-user`; see the
[root build instructions](../README.md#building-and-qualification) for dependency
restoration and the required Windows developer environment.

The installed package exports `Praktor::Praktor`. Set `SALTS_BASE_ROOT` and
`CMETA_ROOT` to compatible installed split SDKs and find Praktor from its exact
installation prefix. Public CMeta descriptors remain borrowed immutable views;
the result JSON stays valid until result release. Runtime PATH must include the
Praktor, SaltsBase and manifest dependency `bin` directories.

The formal `praktor_c_api_test` exercises the shared library, successful and
failed Shell execution, JSON outputs and result release. `praktor_cli_test`
executes the built CLI against `praktor/test/data/cli-shell.yml` and verifies its
JSON and usage exit status, positive timeout bounds, invalid timeout arguments
and actual deadline cancellation. It also verifies dotenv inheritance, root
variables and task-local variable overrides through the shared-library API.
It also covers `--config`/`--task`, dependency selection, skipped tasks, unknown
names, missing/duplicate options and rejection of variable-option aliases.
Both tests live in `praktor/test` and run
through the root CTest user preset.

`praktor_service_test` covers the shared-library Tool path with independent
service files, two curl file transfers, variable binding, preflight refusal and
task failure/cancellation. Its white-box cases verify shared instance identity,
independent simultaneous jobs, live-job stop refusal, timeout and bounded text
capture. Service providers do not export a separate loading ABI.
