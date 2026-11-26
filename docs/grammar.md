# Workflow grammar and execution boundary

The maintained grammar sources are `schema/workflow.schema` and
`schema/workflow.editor.json`. The generated editor artifact is
`docs/grammar.schema.json`. See [schema maintenance](../schema/README.md).

The editor schema checks the executable YAML shape. Runtime admission also
checks dependencies, service bindings, and value capacities. The internal
YAML/data modules expose metadata and AST models for additional parser tests.

The CLI and SDK admit Command, Shell, SetVariable, Tool and action trees
with optional workflow and task statecharts. The CLI runs executable Tool
services. Embedded callers can run WASM Tool services through
`praktor_execute_with_wasm`. [WASM service syntax](#wasm-services)
See the [user guide](USER_GUIDE.md) and
[runtime syntax](../README.md#current-admitted-subset).

Root `inputs` and `input_policy` are invalid keys. External variable values come
from dotenv/inherited environment; explicit root/task variables remain internal
workflow state. The CLI selects execution with
`praktor --config build.yml --task build`; it has no `--input`/`--var` channel.

## Syntax checking

`praktor --config workflow.yml --check` checks the whole workflow and its
referenced YAML files. It returns 0 on success. A diagnostic goes to stderr
with exit code 2 on failure. `--task` and `--timeout-ms` apply to execution.

The editor schema describes document shapes. The CLI check also uses the
native parser and plan builders to resolve includes, expand dynamic tasks,
validate dependencies and statecharts, and check each Tool's service and
inputs. Dynamic task expansion uses values from the workflow and inherited
environment. The check ends before component activation or task execution.
Dotenv loading, WASM module authorization, and external event binding take
place in the execution entrypoints. The C SDK exposes the same check through
`praktor_check`. [Public header](../praktor/include/praktor.h)

## DAG parallelism

Set root `parallelism: 2` to run up to two ready tasks in a group. Values
range from 1 to 16 and default to 1. The DAG admits a group in task declaration
order after its predecessors have committed. The manual CFlow executor drives
one BT tick at a time on the workflow owner thread. Running tasks can overlap
process I/O and waits. Each task owns its BT, task Statechart, Tool job, and
private value scope seeded from the same committed workflow values.

The owner closes executor admission, waits for callback finalization and
drains the shared Shell retainer before publishing. A group commits all of its
successful outputs together. A failure, cancellation or deadline discards
that group's staged results and stops new admission. Duplicate group output
keys are rejected before its actions start.
Earlier groups keep their published results. External process effects have
their own lifecycle. Document Statechart lifecycle notifications run on the
owner thread, with `task.finished` after group settlement.

Use distinct task output keys for tasks admitted together. Use `outputs: []`
for tasks that only need side effects. A previous runtime can use serial
admission by removing `parallelism`. Concurrent BT ticks on multiple threads
would need additional service and Statechart synchronization and are outside
this admission model.

## Task outputs

Declare a task-level list such as `outputs: [$summary, $finish_reason]`.
On success, every listed value must exist in the final task scope. The
workflow owner commits the selected values and task result together before
running successors or dispatching `task.finished`. An empty list suppresses
successful output publication. Root-level `outputs` is outside the native
execution profile.

An executable Tool can report a failed process's exit code once it has
received a process result. Argument resolution can fail at launch and
produces a diagnostic.

Without a list, a direct SetVariable publishes its destination and process
tasks publish the default result fields `stdout`, `stderr`, and `exit_code`.
A successful root WASM Tool publishes one JSON text value under its
`config.output_key`. For an `actions` tree, declare that key in task
`outputs` to make the value available to successor tasks.
A directly bound Shell may customize its three distinct store keys with
`output_key`, `stderr_key`, and `exit_code_key`. The result uses the
canonical field names when the task uses implicit outputs. The runtime
reserves `shell_error` for the process diagnostic.
[Offline summary workflow](../examples/llm/summary-workflow.yml)

## Workflow composition

The root `includes` mapping names local task libraries. Paths are relative to
the process working directory, like service declarations. A library can
declare `name`, `description`, `variables`, and `tasks`. Its tasks use the
calling workflow's services, execution defaults, and document Statechart.

A task such as `{name: check, uses: audit.verify}` instantiates the library
entry `verify` and its transitive dependencies. The entry is named `check`,
and its prerequisites are named `check.<library-task>`. The caller's
`depends_on` applies to the entire instance. Caller `vars` override task and
library variables. Caller `outputs` replace the entry's export list when
specified. Caller `statechart` replaces the entry's task chart when specified.
Each instance shares the workflow output store, so use distinct output keys
for simultaneous instances.

`dynamic_tasks` reads a JSON array using `items_variable: $name` from the
declaring task's `vars`, document `variables`, or loaded environment, in that
order. Expansion runs at admission. Values published later in the run cannot
alter its task graph. A template declares a normal task runner. Its `name`
uses `$index` for the zero-based index and may use `$item` for an identifier
segment. Each generated task receives `item` as text or serialized JSON and
`index` as decimal text. Both override template and caller variables of the
same name. Caller variables override other template variables. The generated
task name is prefixed with the declaring task's name and a dot. Each generated
task inherits the caller and template `depends_on` lists. The declaring task
depends on all generated tasks and uses a BT success assertion as its runner.
A zero-item array leaves the declaring task's original dependencies.

Set `collect: $results` inside `dynamic_tasks` to publish one JSON array
when the declaring task settles successfully. Each array element is one
generated task's output object, in input order. A task with no exports
contributes `{}`. An empty input publishes `[]`. In this mode, member
outputs remain available in their task results. The join publishes
`$results` alongside any other explicit outputs it declares.

The workflow owner reads committed member results from the task registry
during join settlement. It borrows each object only while encoding it into
a buffer limited to 4096 bytes. The join uses the same output transaction
as an ordinary task. An oversized array fails settlement with a diagnostic.
A failed or cancelled member prevents the join from running. For workflows
that publish member values directly to the shared scope, omit `collect`
and use distinct output keys. On an older runtime, fixed tasks with named
outputs and an explicit join can represent the same dependency graph.

The snapshot owns the parsed libraries, copied metadata and compiled task
trees until destruction. Materialization precedes task selection, service
activation and DAG construction. The combined YAML input is limited to
1 MiB, with up to 16 libraries. Generated tasks count toward the workflow's
task limit. An array and each serialized item are limited to 4096 bytes.
Library aliases, local task names, invocation names, and rendered template
names use letters, digits, underscores, and hyphens. The full generated name
is limited to 128 bytes. The graph validates duplicate names, missing
dependencies, and cycles before executing. Templates and libraries contain
ordinary runners. An older runtime can run the same workflow by declaring the
expanded tasks, variables, and dependencies explicitly. [Device fleet example](../examples/workflow-composition.yml)

## Command values

Praktor DSL variable references always start with `$`. Task `vars` override
root `variables`, which override the dotenv or inherited environment.
Names start with an ASCII letter or underscore. Digits can follow the first
character, and dotted names use the same rule for each segment. Whole-value
references and Selector fields also accept hyphens after the first character
of a segment. Variable declarations and output destinations use the same names
without the leading `$`, up to 128 bytes. YAML quotes are optional where the
YAML scalar permits them.

```yaml
variables:
  project: praktor-lib
  notify_command: echo Build and test completed successfully
tasks:
  - name: notify
    command: $notify_command
  - name: report
    shell:
      cmd: echo Build for "$project"
```

Both `command` and `shell.cmd` expand a whole command value or inline
`$name` occurrences before starting the shell. An inline name ends at the first
character outside the identifier syntax, including a hyphen. Praktor inserts
values as command text in one pass. The workflow author supplies shell quoting
and validates values used in commands. A missing variable fails the command
before the shell starts. Migrate `{name}`, `${name}`, `{{ name }}`, and
`$vars.name` references to `$name`. [Build workflow](../build.yml)

Use whole-value `$name` references in other runtime ports that accept a
value, such as `set_variable.value` and `ParseRegex.pattern`. Write
`from: $source`, `input_key: $report`, and `switch: $choice` for explicit
reads. Guard expressions use `match: '$phase == "ready"'`,
`expr: '$count > 0'`, and `has($name)`. An assertion can report a guard
field with `actual: $name`. Declare variable names and the `fields` list
with plain identifiers, and name output destinations with plain keys.
Selector reserves the names `has`, `capability`, `in`, and `not`.
[Upstream version checks](../examples/upstream-version-checks.yml)

## Switch cases

A sequence of `cases` uses the zero-based integer selector. A mapping of
`cases` uses a `$name` selector and compares its text to each case key,
case-sensitively. An optional sibling `default` handles an existing value
that matches no key. The selected branch determines the node result.
A missing selector is an evaluation error. An unmatched selector without
`default` fails the node.

```yaml
switch: $route
cases:
  shadow: {set_variable: selected, value: shadow}
  canary: {set_variable: selected, value: canary}
default: {set_variable: selected, value: stable}
```

Both forms work as task runners and inside `actions`. To migrate string
cases to an older runtime, use the existing `if` and Selector match syntax.
[Release routing example](../examples/statechart-branching.yml)

## Command defaults

Root `defaults` accepts `timeout` (integer milliseconds, 1..2147483647) and
`stream_output` (boolean). Shell/Command configuration inherits built-in values,
then `PRAKTOR_TIMEOUT_MS` / `PRAKTOR_STREAM_OUTPUT` from the loaded environment,
then document defaults, then explicit Shell parameters. Missing fields inherit;
false overrides true. All supplied environment/document values must be valid.
The command timeout is independent of the overall workflow deadline and of
timeouts declared by independent provider services. The formerly accepted but
unused duration string form of `defaults.timeout` must be migrated to integer
milliseconds; removing defaults restores inherited/built-in behavior.
Use `shell.timeout` and `shell.working_dir` for an individual process, and
`timeout` with a sibling `child` for a BT decorator. The native runtime
rejects standalone task metadata `timeout` and `working_dir` before startup.
Task-level `args` and `stdin` are rejected at admission. Use an executable
Tool service for standalone argv arguments.

## Named execution services

Root `services` maps service names to independent YAML declaration files. A
`tool: <service-name>` task supplies an optional `with` map of scalar
values. Values are passed as text, with a whole-value `$name` resolved at
invocation. The workflow editor schema validates the paths in `services`
and the Tool invocation shape. Runtime admission loads and validates each
service declaration. A service with `provider.kind: exe` uses
`provider.path` to select the executable and `config.args` to bind argv.

An optional `capabilities` list in a service declaration supplies names for
Selector's `capability("name")` predicate in `guard.match`. Labels are explicit
so workflows can select a branch by a shared capability even when deployments
use different service aliases. Configurator owns the admitted service
definitions for the run. Each Session borrows that active context for guard
evaluation and releases its tree before Configurator stops. Capability lookup
is case-sensitive and succeeds when any active service declares the label.
Existing service files can omit the list. To run the same configuration on a
release without this feature, remove the list and replace its capability
predicates with the branch conditions appropriate for that deployment.

### WASM services

Declare the service in a separate YAML file.

```yaml
provider:
  kind: wasm
  module: llm.measure
config:
  export: run
  fuel: 100000
  timeout_ms: 1000
  output_key: argument_metrics
```

`provider.module` is a logical module name resolved by the host. The
required `config` map accepts the following optional fields.

| Field | Default | Meaning |
| --- | --- | --- |
| `export` | `run` | Guest entry approved by the host |
| `fuel` | `1000000` | Total fuel requested for one invocation |
| `timeout_ms` | `1000` | Per-invocation timeout in milliseconds |
| `output_key` | `wasm_output` | Task variable holding the guest result |

Module and export names use 1 to 128 ASCII letters, digits, underscores,
dots or hyphens. Fuel and timeout are positive integers. `output_key` is
a literal workflow variable name of up to 128 bytes without a leading `$`.
The runtime reserves
`chart.*`, `event.*`, and `shell_error`.

For a root task, declare `tool: measure` and supply an optional `with` map.
For a behavior-tree leaf, use `Tool: {service: measure, with: {arguments: $payload}}`.
A WASM tool receives up to 64 resolved fields as one UTF-8 JSON object with
string values, limited to 64 KiB. A successful invocation writes one UTF-8
JSON document of up to 4,096 bytes to the configured output key. A later
action can read the result with `ParseJson`. [Example workflow](../examples/llm/wasm-tool.yml)

Use `praktor_execute_with_wasm` to supply the host. The host approves the
module, export, requested fuel and timeout during admission. The reference
TurboWasm host reads its authorization policy from a deployment-owned YAML
file. A workflow declaring a WASM service requires a host binding.
The standard CLI reports unsupported input at admission and exits with code 2.
[Host contract and sandbox policy](WASM.md#embed-the-host)

### Shell and Tool actions inside a behavior tree

Use `Shell: {cmd: ...}` or `Tool: {service: name, with: {...}}` as leaf
actions. A Tool uses a service declared in the document and resolves its
inputs from the current task scope at activation. Shell and Tool results
enter the task scope. Use task `outputs` to publish selected values when
the tree succeeds.

Before execution, the workflow binds every selected Tool leaf, including
leaves in untaken branches. The task driver owns one active service job and
its binding, and borrows each definition from Configurator. The owner
retains decoded inputs through task teardown. The provider resolves its
inputs at start and lends result buffers until release. The driver copies
completed results into the task store before release. Halting an active
leaf cancels, drains and releases its job through the same bounded service
protocol. Tool leaves require serial BT branches. Workflow `parallelism`
can admit independent Tool tasks into the same group.
Shell leaves use the existing Session process driver and workflow retainer.
Admission, execution and cleanup still run through the serial task driver.

To run these workflows on older runtimes, move Shell and Tool actions into
direct tasks and use dependency edges and task outputs to pass data.

### Model calls and response validation

Declare a provider in `services` and call it from a task using `tool` and
`with`. A `config.args` entry such as `$request` binds the invocation's
`request` scalar to one argv entry. The corresponding YAML value can be
literal JSON or a whole-value reference such as `with: {request: $payload}`.
Provider-specific options and any expansions performed by curl belong in the
service declaration.

A successful executable tool task publishes its stdout to the workflow value
`shell_output`. Its CLI result exposes the same bytes as the task's
`outputs.stdout`. Give the validating task a `depends_on` edge to the call,
then use `ParseJson` and `assert` in `actions`. Use `result: one` when the
contract calls for exactly one selected value. A document-level Statechart
can observe the validation task's result through `task.finished`. [Live service example](../examples/llm/llm-openai.yml) · [Offline validation workflow](../examples/llm/summary-workflow.yml)

Declare tasks and dependency edges before execution. Model replies enter the
workflow as data. A task can read model results after execution begins using
BT nodes such as ParseJson. [Usage guide](USER_GUIDE.md#llm-工作流)

## Statechart scopes

Each task declares one BT runner. `actions` specifies an explicit BT. Task
`command`, `tool` and the supported flat action forms also lower to BT.
`depends_on` declares the task graph. The BT outcome settles the task.

An optional `statechart` next to a task's runner observes that task. An
optional root `statechart` observes one workflow run. Both declare `initial`,
`states` and optional `events`. Transitions require `target` and may use a
read-only `guard` with `fields` and `match` or `expr`.

| DSL | Effect |
| --- | --- |
| `'on': task.started` / `'on': task.finished` | Observe runtime task lifecycle events using `$event.data.task` and `$event.data.status`. Successful outputs are visible on `task.finished`. |
| `'on': <event>` | Consume a custom event declared in the receiving chart. |
| `raise: {scope: workflow, event: sensor.ready}` | Deliver a typed internal event to the document chart. The default scope is `task`. |
| `source: external` | Receive typed host events through the embedded SDK polling entrypoint. |
| `after_ms` / `on_done` | Observe a state timer or completion of a composite or parallel state. |
| `$chart.task.<id>` / `$chart.workflow.<id>` | Read state membership as `1` or `0` in a guard. |

A top-level final state stops its chart. The task's BT continues and determines
its result. Statechart declarations with `on_entry`, `on_exit`, `do`, transition
`actions`, or final `result` are rejected. Move those actions into the task's
BT. [Statechart grammar and migration](STATECHART.md)

### Composing state decisions with BT actions

In the release example, BT emits a simulated ACK. The task chart checks its
attempt number against the current attempt and commits the `accepted` state.
BT reads `$chart.task.accepted` before choosing a route with `if` and `switch`.

```yaml
# In the task's actions.Sequence, with a declared deploy.ack event.
- raise: {event: deploy.ack, with: {attempt_id: $attempt_id}}
- assert:
    guard:
      fields: [chart.task.accepted]
      expr: '$chart.task.accepted == 1'
```

The device example combines a DAG dependency, a task chart for reports and
timers, and a workflow chart read by a successor task. Ready tasks can also
exchange state signals through the document chart. The receiver sees the
committed chart transition while the group is still running. [Device assessment](../examples/statechart.yml) · [Release routing](../examples/statechart-branching.yml) · [Sensor handshake](../examples/statechart-signals.yml)
