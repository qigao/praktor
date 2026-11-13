# Native Actions

`praktor/src/actions` owns task-internal Session execution, native leaf dispatch
and bounded output scanners. Their headers are in `praktor/include/actions`.
The application runtime owns workflow DAG readiness, task state and publication.

Session compiles a preorder plan, copies its action/parameter storage and drives
`Salts::BehaviorTree`. The primary host supplies values, leaf tick/halt and any
branch-aware control hooks. Native Shell uses the shared process driver over
SaltsBase `process_async`; cleanup and output publication wait for native drain.

Reusable tools are declared in independent service YAML files. The workflow's
`services` map assigns names to those files; `tool` tasks bind a name once and
provide scalar `with` inputs. Configurator creates one execution-service instance
per workflow/name. Each activation owns a separate job. The runtime has no
download/archive catalog: executable choice and argv templates live in YAML.

The implementation reuses CMeta Interface/DataDesc and Configurator for service
admission/lifecycle, the existing process driver for native IO, and TaskRegistry
for outputs. A process adapter was chosen over operation-specific native services
so new command-line operations require configuration rather than runtime changes.
DLL, embedded TurboScript and WASM require their own admitted adapters; only
`kind: exe` is currently supported. There is no implicit adapter selection.

Declarations are loaded and task references/templates validated before task
execution. Each `poll` performs bounded progress; cancellation closes process
admission, drains IO, and releases the job before stopping the service. Borrowed
declarations and service interfaces remain alive through that release. Workflow
execution remains serial, and existing Command/Shell/SetVariable behavior and
the public request/result layouts remain unchanged. Removing `services` and
`tool` declarations restores the earlier workflow subset without migration of
stored task data. See [the user guide](../USER_GUIDE.md) for the file format.

The C API, ownership and module tests are described in [ACTIONS_RUNTIME.md](ACTIONS_RUNTIME.md).
The complete runtime is built through the root `llvm-mingw-x64-release-user`
preset. `BUILD_TESTS` adds this module's formal CTest entries. The installed SDK's
admitted workflow subset is documented in [the C API guide](C_API.md).
