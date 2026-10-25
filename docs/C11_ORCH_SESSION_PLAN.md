# Praktor native Orch → C11 Session Plan qualification

This is a **source-only candidate** for [PR #79](https://github.com/qigao/praktor/pull/79). The accepted production WorkflowExecutor, WorkflowContext, DAG/trigger handling, C++ AST bridge and installed SDK ABI are unchanged.

## Single canonical pipeline

```
YAML source
  -> C11 workflow_snapshot / yaml_task_metadata
  -> C11 orchestration tree (owned CSTL AST)
  -> praktor_c11_orch_session_plan_create()
     -> iterative preorder traversal
     -> praktor_c11_compile_node()  [existing C11 compiler]
     -> owned action/parameter descriptors
  -> praktor_c11_orch_session_plan_init_session()
     -> praktor_c11_session_init() [existing Session clone/validation]
     -> existing C11 NativeHost + Session tick/cancel/destroy
```

No new YAML parser, BT interpreter, queue, thread pool, Process owner, C++ facade, or fallback is introduced.

The initial **fail-closed admitted subset** is Sequence, Fallback, Selector, Inverter, ForceSuccess, ForceFailure, SetVariable, CheckExitCode, ParseKeyValue, ParseRegex, ParseLines and Sleep. Those actions can use the existing native host and Session engine. Shell, Parallel, SubTree, dynamic/trigger scheduler operations, and all other unqualified actions return `SALTS_ENOTSUP`; no silent reinterpretation as a leaf is permitted. Admission of an AST is deliberately stricter than accepting the same YAML in the Legacy runtime.

## Canonical YAML compatibility

The existing YAML grammar deliberately **removed** the named `SetVariable:` spelling. A task or an actions sequence must use the established flat sibling form instead:

```yaml
actions:
  Sequence:
    - set_variable: answer
      value: '7'
    - CheckExitCode: {input_key: answer, expected: 7}
```

The native plan adapter does not re-enable retired Legacy node spellings. It also refuses `{{ Mustache }}` template text until the current C++ preprocessing semantics have an independently qualified C11 equivalent; silently treating double braces as native single-brace variable substitution would change the workflow contract.

## Ownership and bounds

- An immutable `praktor_orch_tree` is borrowed only for plan creation. All parameter text is copied with its size and terminating NUL. Catalog key/action strings are immutable process-lifetime literals. Embedded NUL values are explicitly rejected rather than truncated by Native Host leaf string handling. Mustache expressions are also rejected until expansion parity is established.
- The plan is uniquely owned and may survive destruction of the original YAML/Snapshot/Orch tree. Session initialization clones the compiled descriptors so plan destruction cannot invalidate a running Session.
- Admission uses the original **4098-node**, eight-parameters-per-node and **1 MiB copied text** limits, plus Salts BehaviorTree `max_nodes`, `max_depth` and control-iteration admission. Traversal is iterative. Every failure frees provisional nodes/parameters and leaves the caller's output pointer unchanged.
- Each Session/NativeHost is still **single-driver**, including tick, reset, cancel and destroy. The plan builder does not schedule work or decide which native worker owns the Session.

## Qualification tests

`actions_c11_orch_session_plan_test` checks actual YAML → Orch → Session compilation, SetVariable and CheckExitCode native effects, lifetime independence after destroying source/plan, running Sleep cancellation without a downstream SetVariable, Shell refusal without fallback, embedded-NUL refusal and bounded-node rejection. The test is included in normal CTest, lifecycle ASan/UBSan and complete-source TSan with a required-name admission assertion.

## Owner-driven native leaf DAG qualification

The next opt-in \`actions_c11_orch_dag_native_session_test\` starts from two
canonical YAML task bodies: a flat \`set_variable\` with a dependency, and a
named Sequence containing CheckExitCode and zero-duration Sleep. Each DAG task
gets a separately owned C11 Session and NativeHost. The existing CFlow
**ManualExecutor** invokes Session tick on the same thread that initializes,
cancels and destroys it; no Session is handed to a WorkerExecutor thread.

The first task changes only its private Host values. After its CFlow callback
finalizes, the application Owner copies the observable value into an existing
optimistic root transaction, uses the established CFlow/Process-retainer
commit barrier and TaskRegistry typed output publication, marks the DAG
predecessor complete and only then admits the dependent native Session.

A separate cancellation probe drives a real 10-second Sleep for one tick.
CFlow finishes/finalizes that single callback while the Session itself still
reports RUNNING. The owner cancels the Session and refuses both publication
and downstream admission. This proves that CFlow callback settlement is not
a substitute for native Session terminal state.

The manual text handoff between per-task Hosts is intentionally explicit and
does **not** constitute the future native WorkflowContext scope/merge contract.
Actual Shell/NativeIO tasks, event/trigger scheduling, and worker-thread
Session transfer remain out of scope.

These are first-stage **native leaf** tests, not proof that the C11 DAG now executes YAML leaves end-to-end. The subsequent integration gate must select ready tasks from `workflow_dag_state`, use an independently owned Session/NativeHost for each admitted task, relay run/cancel/finalize into a single owner turn, and publish TaskRegistry/transaction results only after CFlow and Process close barriers. Trigger/Each/dynamic task semantics, action-owner policies, file-based WorkflowPlan provenance, actual Shell/NativeIO lifecycle, and application parity remain outside this stage.


## Root → task scope → explicit text output handoff

The source-only WorkflowContext bridge now composes existing primitives instead
of introducing a second variable store or implicit merge rule:

1. The canonical root `praktor_c11_value_store` contains admitted workflow
   variables and previously published selected outputs.
2. A fresh NativeHost is initialized with matching key/value/capacity limits.
   `praktor_c11_workflow_load_task_text_scope()` reuses the qualified
   `praktor_c11_workflow_fork_task_text_variables()` to overlay that task's
   `vars` on an independent root clone, then uses
   `praktor_c11_value_store_replace_clone()` for **atomic admission** to
   the already initialized Host value slot. No Session may be active during
   this replacement. No private Host variables can modify the root directly.
3. After the task's CFlow callback has finalized and the Session has actually
   reached terminal success, the single Owner calls
   `praktor_c11_workflow_prepare_task_text_outputs()` with an explicit list
   of selected output keys. This prepares a canonical optimistic transaction
   but publishes **nothing**. Missing/duplicate/oversize/OOM output fails
   without modifying root or the caller's output transaction handle. Limits
   are 128 selected keys and 1 MiB aggregate copied text.
4. The existing two-phase TaskRegistry/Process barrier publishes the prepared
   transaction and typed task outputs. Only after successful publication does
   the Owner finalize TaskRegistry/DAG, admitting successors. A failed/cancelled
   Session aborts the transaction and cannot publish local state.

The new integration test uses a root `seed=9` and a task-local
`seed=7`: native SetVariable produces `answer=7`, but root `seed`
remains `9`. After commit, the dependent native CheckExitCode Session is
automatically initialized from the updated root and sees `answer=7`
without the previous manual injection. A pending Sleep's task-local `late`
override likewise stays private on cancellation.

This is intentionally a **text-only**, materialized task scope, not the
full structured WorkflowValue/lexical-context provider, automatic output
mapping, concurrent WorkerExecutor Session migration, or file-based
WorkflowPlan cutover. Shared Root/TaskRegistry mutation remains one
serialized Owner turn, with no native persistence or crash-atomic guarantee.

## Explicit native Shell/Process DAG qualification

The default `praktor_c11_orch_session_plan_create()` **still rejects** Shell
and retains all previous fail-closed/Legacy-refusal tests. A separate opt-in
`praktor_c11_orch_session_plan_create_native_shell()` admits **exactly one**
canonical task-level Shell node, not an arbitrary Orchestration graph or
implicit fallback. Its plan records that a real, initialized
`cflow_process_retainer` is required. Session admission fails closed if that
owner is absent. The only execution implementation remains the existing
C11 Session, SaltsUtils Process driver and NativeIO recovery owner.

`actions_c11_orch_dag_shell_process_test` drives the real pipeline:
canonical YAML → Orch Shell → explicit C11 Session plan → owner-affine
CFlow ManualExecutor callback → real OS child → Process close/retainer →
explicitly selected `shell_exit_code` transaction plus typed TaskRegistry
group publication → dependent native CheckExitCode. No DAG successor may
run until the process barrier reports zero pending recovery work.

A separate cancellation test starts a long-running real child on each
platform, ticks it once, closes CFlow callback admission, cancels the
still-running Session with zero native close budget, destroys its Session/
NativeHost, and drains any adopted Process through the sole retainer.
Neither the root store nor TaskRegistry may publish shell outputs and the
dependent task remains blocked.

This is an opt-in **test-only single-Shell admission**, not a blanket Shell
capability declaration. It is selected in ordinary tests, lifecycle
ASan/UBSan and complete-source TSan (with immutable fully instrumented
dependencies). Consumer-only TSan is deliberately *not* the qualification
for real Process/NativeIO because released dependency binaries are not
fully instrumented.

## Canonical single-command C11 Process qualification

The new test-only `praktor_c11_orch_session_plan_create_native_command()`
admits the **exact existing C11 Command projection** only when it is a
two-node `Sequence → Shell` with one command and *no postprocessor*.
The default Orch-to-Session converter still rejects the Shell child.
The separate single-Shell admission remains independently bounded.
A real initialized Process retainer is required before Session admission.

`actions_c11_orch_dag_shell_process_test` now covers both:
a flat task-level `shell:` runner and a normal `command: echo ...`
runner. In each case real CFlow ManualExecutor/Session/Process callbacks
must settle, then the Process barrier and selected output transaction
publish `shell_exit_code`; the dependent CheckExitCode Session runs only
after the DAG predecessor commits.

Command+postprocessor input such as
`command: ...` + `parse_json: ...` is explicitly rejected by this
single-command gate. A canonical command with more than one Shell child
is also ineligible. This does **not** assert ParseJson parity, command
sequence ownership, Mustache expansion, or a production cutover.

## Bounded C11 ParseJson / canonical JSON command

The original C++ `OutputParser::parseJson()` uses SaltsUtils JSON DOM and
JSONPath. The native C11 Host now calls those same installed parser APIs:
trim/normalize paths, select the **first** JSONPath match, emit raw text for
JSON strings, and canonical serialized JSON for numbers, arrays, objects,
booleans or null. Missing input, invalid JSON, bad paths and absent matches
return a leaf FAILURE without modifying previously published output. An
embedded-NUL JSON string is explicitly `ENOTSUP` because the existing C11
ValueStore is text-only; oversize/capacity failures remain strong and bounded.

A separate opt-in `praktor_c11_orch_session_plan_create_native_command_json()`
admits only the canonical three-node `Sequence -> Shell -> ParseJson` form
with a single Shell, exactly the established `stdout` input/output binding,
and a real initialized SaltsUtils Process retainer. Default Plan creation
still refuses command-derived Shell; `create_native_command()` remains
strictly the two-node single command without postprocessors. Native
`ParseJson` itself is allowed as a separately qualified leaf with a
NativeHost.

The actual `command: echo 7` plus `parse_json: {path: '$', output_key: parsed}`
DAG test drives real CFlow ManualExecutor and OS process, then stages only
`parsed=7` for Process/TaskRegistry group publication. The dependent
CheckExitCode task can run only after root visibility and DAG admission.
`parse_regex`, multi-command execution, script, arbitrary JSONPath
streaming, Mustache and general command postprocessors have **not** been
admitted by this narrow command-JSON profile.

## Canonical command + ParseKeyValue — narrowly admitted native path

A new source-only, opt-in
`praktor_c11_orch_session_plan_create_native_command_keyvalue()`
admits **exactly** the canonical `Sequence → Shell → ParseKeyValue` shape
(one command, one parser, no extra nodes), and explicitly requires the
original `stdout` output/input binding. The existing C11 NativeHost
`ParseKeyValue` invokes the canonical C11 output scanner and bounded
JSON encoder; no additional parser, worker pool, Process owner, or
automatic Legacy fallback has been introduced.

The real-process DAG integration test executes
`command: echo KEY=42` with
`parse_keyvalue: {output_key: parsed}`, checks that the private task scope
contains `parsed={"KEY":"42"}`, and commits only the selected `parsed`
value after the CFlow callback, native Process cleanup, and existing
TaskRegistry/Process-retainer barrier. Only then is a dependent
`set_variable` leaf allowed to inherit it. Another test rejects a
noncanonical Shell `output_key` binding and verifies that admission
failure does not corrupt a previously accepted plan.

The default Orch plan converter still refuses all Shell access. The
existing opt-in Shell, plain-command and JSON-command entry points keep
their previous strict behavior; this new API does not admit regex/lines
postprocessors or arbitrary command sequences. This is a single-owner
test qualification, not a general production WorkflowContext/WorkflowPlan
or crash-atomic execution guarantee.

PR #79 stays **Draft / do not merge** until full cutover gates pass.
