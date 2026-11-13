# Workflow grammar and execution boundary

The maintained grammar sources are `schema/workflow.schema` and
`schema/workflow.editor.json`; `grammar.schema.json` is their generated editor
artifact. [Schema maintenance](schema/README.md) documents its build and tests.

The YAML/data modules expose a broader immutable metadata and AST model than
the application runtime executes. Parsing a field or describing it in an editor
schema does not authorize its execution.

For the installed CLI and SDK, use the admitted Command, Shell, SetVariable and Tool
subset in [the user guide](USER_GUIDE.md) and [runtime contract](docs/RUNTIME_RESET.md).
Unsupported application features return an explicit failure.

Root `inputs` and `input_policy` are invalid keys. External variable values come
from dotenv/inherited environment; explicit root/task variables remain internal
workflow state. The CLI selects execution with
`praktor --config build.yml --task build`; it has no `--input`/`--var` channel.

Root `defaults` accepts `timeout` (integer milliseconds, 1..2147483647) and
`stream_output` (boolean). Shell/Command configuration inherits built-in values,
then `PRAKTOR_TIMEOUT_MS` / `PRAKTOR_STREAM_OUTPUT` from the loaded environment,
then document defaults, then explicit Shell parameters. Missing fields inherit;
false overrides true. All supplied environment/document values must be valid.
The command timeout is independent of the overall workflow deadline and of
timeouts declared by independent provider services. The formerly accepted but
unused duration string form of `defaults.timeout` must be migrated to integer
milliseconds; removing defaults restores inherited/built-in behavior.

Root `services` maps service names to independent YAML declaration files. A
`tool: <service-name>` task supplies scalar `with` inputs. The declaration's
`provider.kind: exe`, `provider.path` and `config.args` describe generic process
execution; operation names and command-line meanings remain entirely in YAML.
