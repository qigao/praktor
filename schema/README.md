# Workflow schema sources

`workflow.schema` is the Salts IDL source for the workflow's typed fields,
presence, defaults, names and scalar/list constraints. `workflow.editor.json`
contains Praktor's YAML-specific policies: scalar/sequence shorthand, runner
exclusivity, string-valued enums, expressions, and unrestricted JSON values such
as host-tool arguments and contract defaults. The root `grammar.schema.json` is
the generated editor artifact. Do not edit that artifact directly.

The maintenance tool uses the installed SDK's public `idl_contract_parse` and
`schema_cmeta_field_resolve` APIs through `Salts::IDL` and `Salts::Schema`. It
does not parse IDL itself or infer native layouts. Parsed contracts own their
metadata until projection finishes. Unsupported declarations, annotations and
shapes fail explicitly. An editor policy cannot repeat or replace an IDL-owned
keyword; independent required fields are combined with duplicate detection.

This integrates DataBind's logical IDL/Schema layers for editor generation.
It does not migrate the runtime YAML parser or `WorkflowValue` storage to
DataBind. In SaltsUtils 4.3.0-rc.2, arbitrary JSON and YAML unions do not have a
general DataBind native binding. Keeping those policies explicit preserves
existing workflow data and avoids inventing an opaque IDL type. The runtime
input/output JSON Schema API still describes each workflow's user-defined
contract; this file describes the workflow authoring language.

The SDK compiler's OpenAPI projection targets service contracts. A small editor
projection over public metadata is used here because the workflow grammar also
needs YAML unions and cross-field rules. Moving those rules into annotations or
rewriting workflow values into binary records would change their semantics.

After configuring a native preset with the matching installed SDKs, regenerate
and check the artifact (Windows example, in the developer environment):

```powershell
cmake --build --preset win-release-user --parallel 2
cmake --build --preset win-release-user --target update-workflow-schema
ctest --preset win-release-user -R schema_projection --output-on-failure
```

The normal CTest suite compares the generated schema with the committed artifact
and exercises IDL aliases, constraints, defaults, presence and rejection paths.
CI runs this test through its ordinary native build/test graph. Cross builds
consume the committed artifact and do not build or execute the maintenance tool.
The tool and its dependencies are not added to the installed Praktor API.

For a new typed field, add its IDL declaration and put only editor policy in the
corresponding JSON property. IDL message names map to `$defs`; `Workflow` maps
to the document root. This projection currently supports messages, their field
references, scalar fields and lists. Add support and tests explicitly before
using another IDL shape. To roll back this tooling, restore the previously
generated artifact and remove the `tools/schema` CMake subdirectory; workflow
files and runtime data need no migration.
