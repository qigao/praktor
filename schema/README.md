# Workflow schema sources

`workflow.schema` supplies logical field shapes through SaltsUtils IDL/Schema.
`workflow.editor.json` supplies YAML unions and cross-field editor policies.
The root `grammar.schema.json` is generated from those sources.

`shared/schema` consumes the public `Salts::IDL` and `Salts::Schema` metadata
APIs. It does not infer native layouts or implement another IDL parser. Parsed
metadata stays alive through projection; unsupported declarations and conflicting
policies fail explicitly.

Runtime YAML parsing uses SaltsBase `Salts::CYaml`. The schema describes the
parser/editor model; the installed workflow runtime admits a narrower subset,
documented in the root README. Schema presence does not enable execution.

In the configured Windows developer environment:

```sh
cmake --build --preset llvm-mingw-x64-release-user --target update-workflow-schema
ctest --preset llvm-mingw-x64-release-user -R schema_projection
```

The normal full build includes the maintenance tool and its formal tests.
Cross builds consume the committed schema artifact and do not run a target
executable on the host. The maintenance tool is not part of the installed API.
