# Native Actions

`praktor/src/actions` owns task-internal Session execution, native leaf dispatch
and bounded output scanners. Their headers are in `praktor/include/actions`.
The application runtime owns workflow DAG readiness, task state and publication.

Session compiles a preorder plan, copies its action/parameter storage and drives
`Salts::BehaviorTree`. The primary host supplies values, leaf tick/halt and any
branch-aware control hooks. Native Shell uses the shared process driver over
SaltsBase `process_async`; cleanup and output publication wait for native drain.

The C API, ownership and module tests are described in [ACTIONS_RUNTIME.md](ACTIONS_RUNTIME.md).
The complete runtime is built through the root `llvm-mingw-x64-release-user`
preset. `BUILD_TESTS` adds this module's formal CTest entries. The installed SDK's
admitted workflow subset is documented in [the C API guide](C_API.md).
