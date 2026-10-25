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

These are first-stage **native leaf** tests, not proof that the C11 DAG now executes YAML leaves end-to-end. The subsequent integration gate must select ready tasks from `workflow_dag_state`, use an independently owned Session/NativeHost for each admitted task, relay run/cancel/finalize into a single owner turn, and publish TaskRegistry/transaction results only after CFlow and Process close barriers. Trigger/Each/dynamic task semantics, action-owner policies, file-based WorkflowPlan provenance, actual Shell/NativeIO lifecycle, and application parity remain outside this stage.

PR #79 stays **Draft / do not merge** until full cutover gates pass.
