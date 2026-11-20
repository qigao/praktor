# Workflow schema sources

`workflow.schema` supplies logical field shapes through SaltsUtils IDL/Schema.
`workflow.editor.json` supplies YAML unions and cross-field editor policies.
The editor artifact `docs/grammar.schema.json` is generated from those sources.

`shared/schema` consumes the public `Salts::IDL` and `Salts::Schema` metadata
APIs. It does not infer native layouts or implement another IDL parser. Parsed
metadata stays alive through projection; unsupported declarations and conflicting
policies fail explicitly.

Runtime YAML parsing uses SaltsBase `Salts::CYaml`. The editor schema checks
the executable workflow shape. The installed runtime also validates dependency
graphs, bindings, capacities and values. See the [user guide](../docs/USER_GUIDE.md#当前执行范围). The task
`outputs` list is part of the native runtime and selects values to publish on
successful task completion. The root output contract describes parser metadata.
The native runtime also expands local `includes`, `uses`, and `dynamic_tasks`
before building the task graph. The editor checks local shape, while admission
resolves library files, JSON arrays, names, and dependency edges.
[Workflow composition](../docs/grammar.md#workflow-composition)

In the configured Windows developer environment:

```sh
cmake --build --preset llvm-mingw-x64-release-user --target update-workflow-schema
ctest --preset llvm-mingw-x64-release-user -R schema_projection
```

The normal full build includes the maintenance tool and its formal tests.
Cross builds consume the committed schema artifact and do not run a target
executable on the host. The maintenance tool is not part of the installed API.
