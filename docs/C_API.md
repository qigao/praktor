# Praktor C API (ABI v3)

The embedded API is declared in [praktor.h](../praktor/include/praktor.h), with
its wrapper implementation in `praktor/src/runtime/praktor_api.c`. Production
execution is C11; callers supply inline YAML, explicit limits and an optional
cancellation probe. The installed Praktor interface is this single header;
`praktor/runtime.h` is private and is no longer installed. Existing request,
result and limits layouts and the execution entrypoints remain unchanged.
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
Shell and single-Command roots. Scripts, includes, dynamic plans and application
Parallel execution remain unsupported.
[hello-world.yml](../examples/hello-world.yml) is a runnable example of the
admitted subset. The CLI accepts one workflow file: `praktor workflow.yml`.

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
Both tests live in `praktor/test` and run
through the root CTest user preset.
