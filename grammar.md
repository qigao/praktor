# Workflow grammar and execution boundary

The maintained grammar sources are `schema/workflow.schema` and
`schema/workflow.editor.json`; `grammar.schema.json` is their generated editor
artifact. [Schema maintenance](schema/README.md) documents its build and tests.

The YAML/data modules expose a broader immutable metadata and AST model than
the application runtime executes. Parsing a field or describing it in an editor
schema does not authorize its execution.

For the installed CLI and SDK, use the admitted Command, Shell and SetVariable
subset in [the user guide](USER_GUIDE.md) and [runtime contract](docs/RUNTIME_RESET.md).
Unsupported application features return an explicit failure.
