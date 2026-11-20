#include <tinytest.h>
#include <praktor/runtime.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#define BLOCKING_COMMAND "ping -n 3 127.0.0.1 > NUL"
#else
#define BLOCKING_COMMAND "sleep 2"
#endif
static praktor_runtime_status execute_constant_guard(const char *expr,
                                                       praktor_runtime_result *out) {
    char yaml[1024];
    int length=snprintf(yaml,sizeof yaml,
        "tasks:\n"
        "  - name: verify\n"
        "    assert: {guard: {fields: [], expr: '%s'}}\n",expr);
    check(length>0&&(size_t)length<sizeof yaml);
    return praktor_runtime_execute_yaml(yaml,(size_t)length,NULL,NULL,NULL,out);
}

spec("C11 Configurator CFlow CMeta runtime") {
    static praktor_runtime_result execution;
    before_each() { execution=(praktor_runtime_result){0}; }
    after_each() { praktor_runtime_result_destroy(&execution); }
    it("filters stale typed events and exposes the committed task state to the BT") {
        const char yaml[] =
            "variables: {attempt: '9007199254740993'}\n"
            "tasks:\n"
            "  - name: chart\n"
            "    statechart:\n"
            "      initial: waiting\n"
            "      events: {deploy.ack: {source: internal, fields: {attempt: int64}}}\n"
            "      states:\n"
            "        waiting:\n"
            "          transitions:\n"
            "            - 'on': deploy.ack\n"
            "              guard: {fields: [attempt, event.data.attempt], expr: '$event.data.attempt == $attempt'}\n"
            "              target: accepted\n"
            "        accepted: {type: final}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - raise: {event: deploy.ack, with: {attempt: '9007199254740992'}}\n"
            "        - assert: {guard: {fields: [chart.task.waiting], expr: '$chart.task.waiting == 1'}}\n"
            "        - raise: {scope: task, event: deploy.ack, with: {attempt: $attempt}}\n"
            "        - assert: {guard: {fields: [chart.task.accepted], expr: '$chart.task.accepted == 1'}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"chart\":{\"status\":\"success\"}");
    }
    it("lets one ready task signal the document chart using its own typed values") {
        const char yaml[]=
            "parallelism: 2\n"
            "variables: {reading: '3'}\n"
            "statechart:\n"
            "  initial: waiting\n"
            "  events: {sensor.ready: {source: internal, fields: {reading: int64}}}\n"
            "  states:\n"
            "    waiting:\n"
            "      transitions:\n"
            "        - 'on': sensor.ready\n"
            "          guard: {fields: [reading, event.data.reading], expr: '$reading == 3 && $event.data.reading == 9007199254740993'}\n"
            "          target: ready\n"
            "    ready: {type: final}\n"
            "tasks:\n"
            "  - name: receive\n"
            "    wait_until:\n"
            "      guard: {fields: [chart.workflow.ready], expr: '$chart.workflow.ready == 1'}\n"
            "      timeout_ms: 500\n"
            "  - name: send\n"
            "    sequence:\n"
            "      - {set_variable: reading, value: '9007199254740993'}\n"
            "      - raise: {scope: workflow, event: sensor.ready, with: {reading: $reading}}\n"
            "      - assert: {guard: {fields: [chart.workflow.ready], expr: '$chart.workflow.ready == 1'}}\n"
            "  - {name: finish, depends_on: [receive, send], set_variable: confirmed, value: ready}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
            "finish",&execution)==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"receive\":{\"status\":\"success\"}");
        check_contains(execution.json,"\"send\":{\"status\":\"success\"}");
        check_contains(execution.json,"\"confirmed\":\"ready\"");
    }
    it("rejects missing, reserved or invalid workflow event destinations during admission") {
        static const struct {const char *chart,*scope,*event,*error;} cases[]={
            {"","workflow","sensor.ready","requires a document statechart"},
            {"","another","sensor.ready","scope must be task or workflow"},
            {"statechart: {initial: waiting, states: {waiting: {}}}\n",
             "workflow","task.finished","declared internal event"}
        };
        for(size_t i=0u;i<sizeof cases/sizeof *cases;++i) {
            char yaml[512];
            int n=snprintf(yaml,sizeof yaml,
                "%stasks:\n"
                "  - {name: setup, command: echo unexpected}\n"
                "  - name: notify\n"
                "    depends_on: [setup]\n"
                "    actions: {Sequence: [{raise: {scope: %s, event: %s}}]}\n",
                cases[i].chart,cases[i].scope,cases[i].event);
            check(n>0&&(size_t)n<sizeof yaml);
            check_equal(praktor_runtime_execute_yaml(yaml,(size_t)n,NULL,NULL,NULL,&execution),
                        PRAKTOR_RUNTIME_INVALID);
            check_null(execution.json);
            check_contains(execution.error,cases[i].error);
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("uses one guard snapshot for disjoint parallel transitions and observes state completion") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - raise: {event: go}\n"
            "        - assert: {guard: {fields: [chart.task.done], expr: '$chart.task.done == 1'}}\n"
            "    statechart:\n"
            "      initial: both\n"
            "      events: {go: {source: internal}}\n"
            "      states:\n"
            "        both:\n"
            "          type: parallel\n"
            "          transitions: [{on_done: both, target: done}]\n"
            "          states:\n"
            "            left:\n"
            "              initial: left-waiting\n"
            "              states:\n"
            "                left-waiting:\n"
            "                  transitions: [{'on': go, target: left_done}]\n"
            "                left_done: {type: final}\n"
            "            right:\n"
            "              initial: right_waiting\n"
            "              states:\n"
            "                right_waiting:\n"
            "                  transitions:\n"
            "                    - 'on': go\n"
            "                      guard: {fields: [chart.task.left-waiting], match: '$chart.task.left-waiting == \"1\"'}\n"
            "                      target: right_done\n"
            "                right_done: {type: final}\n"
            "        done: {type: final}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"chart\":{\"status\":\"success\"}");
    }
    it("restores the previously active descendant through deep history") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - raise: {event: step}\n"
            "        - raise: {event: pause}\n"
            "        - raise: {event: resume}\n"
            "        - assert: {guard: {fields: [chart.task.second], expr: '$chart.task.second == 1'}}\n"
            "    statechart:\n"
            "      initial: running\n"
            "      events: {step: {source: internal}, pause: {source: internal}, resume: {source: internal}}\n"
            "      states:\n"
            "        running:\n"
            "          initial: job\n"
            "          transitions: [{'on': pause, target: paused}]\n"
            "          states:\n"
            "            checkpoint: {type: history, mode: deep, default: job}\n"
            "            job:\n"
            "              initial: first\n"
            "              states:\n"
            "                first:\n"
            "                  transitions: [{'on': step, target: second}]\n"
            "                second: {}\n"
            "        paused:\n"
            "          transitions: [{'on': resume, target: checkpoint}]\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"chart\":{\"status\":\"success\"}");
    }
    it("takes the second same-deadline chart guard while the BT waits") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    actions:\n"
            "      wait_until:\n"
            "        guard: {fields: [chart.task.done], expr: '$chart.task.done == 1'}\n"
            "        timeout_ms: 1000\n"
            "    statechart:\n"
            "      initial: waiting\n"
            "      states:\n"
            "        waiting:\n"
            "          transitions:\n"
            "            - {after_ms: 2, guard: {fields: [], expr: 'false'}, target: failed}\n"
            "            - {after_ms: 2, target: done}\n"
            "        failed: {type: final}\n"
            "        done: {type: final}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"chart\":{\"status\":\"success\"}");
    }
    it("publishes document chart lifecycle state before a successor BT executes") {
        const char yaml[] =
            "statechart:\n"
            "  initial: waiting\n"
            "  states:\n"
            "    waiting:\n"
            "      transitions:\n"
            "        - 'on': task.started\n"
            "          guard: {fields: [event.data.task], match: '$event.data.task == \"prepare\"'}\n"
            "          target: checking\n"
            "    checking:\n"
            "      transitions:\n"
            "        - 'on': task.finished\n"
            "          guard: {fields: [event.data.task, event.data.status, answer], match: '$event.data.task == \"prepare\" && $event.data.status == \"success\" && $answer == \"43\"'}\n"
            "          target: checked\n"
            "    checked: {type: final}\n"
            "tasks:\n"
            "  - {name: prepare, set_variable: answer, value: '43'}\n"
            "  - name: chart\n"
            "    depends_on: [prepare]\n"
            "    assert: {guard: {fields: [chart.workflow.checked], expr: '$chart.workflow.checked == 1'}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"chart\":{\"status\":\"success\"}");
    }
    it("keeps running the task BT when its chart reaches a final state") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - assert: {guard: {fields: [chart.task.running], expr: '$chart.task.running == 1'}}\n"
            "        - raise: {event: stop}\n"
            "        - assert: {guard: {fields: [], expr: 'false'}, message: BT still runs}\n"
            "    statechart:\n"
            "      initial: idle\n"
            "      events: {stop: {source: internal}}\n"
            "      states:\n"
            "        idle: {transitions: [{'on': task.started, target: running}]}\n"
            "        running: {transitions: [{'on': stop, target: done}]}\n"
            "        done: {type: final}\n"
            "  - name: successor\n"
            "    depends_on: [chart]\n"
            "    assert: {guard: {fields: [], expr: 'true'}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"BT still runs");
        check_contains(execution.json,"\"chart\":{\"status\":\"failed\"}");
        check_contains(execution.json,"\"successor\":{\"status\":\"pending\"}");
    }
    it("rejects a chart without a BT task body") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    statechart: {initial: ready, states: {ready: {}}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(execution.json);
        check_contains(execution.error,"statechart requires a task runner");
    }
    it("rejects external ingress without a host binding") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    assert: {guard: {fields: [], expr: 'true'}}\n"
            "    statechart:\n"
            "      initial: done\n"
            "      states: {done: {type: final}}\n"
            "      events: {ack: {source: external}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_contains(execution.error,"external event source has no host binding");
        check_null(execution.json);
    }
    it("rejects NUL in a literal BT event payload at admission") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    actions: {raise: {event: notice, with: {message: \"one\\0two\"}}}\n"
            "    statechart:\n"
            "      initial: waiting\n"
            "      events: {notice: {source: internal, fields: {message: text}}}\n"
            "      states: {waiting: {}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(execution.json);
    }
    it("rejects undeclared event fields in transition guards before task start") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    assert: {guard: {fields: [], expr: 'true'}}\n"
            "    statechart:\n"
            "      initial: waiting\n"
            "      events: {notice: {source: internal}}\n"
            "      states:\n"
            "        waiting:\n"
            "          transitions:\n"
            "            - 'on': notice\n"
            "              target: done\n"
            "              guard: {fields: [event.data.message], match: 'has($event.data.message)'}\n"
            "        done: {type: final}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(execution.json);
        check_contains(execution.error,"guard references an unavailable event field");
    }
    it("fails an unconditional statechart cycle within the stabilization budget") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    assert: {guard: {fields: [], expr: 'true'}}\n"
            "    statechart:\n"
            "      initial: looping\n"
            "      states: {looping: {transitions: [{target: looping}]}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"stabilization step limit exceeded");
    }
    it("rejects action hooks in a task statechart") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    assert: {guard: {fields: [], expr: 'true'}}\n"
            "    statechart:\n"
            "      initial: ready\n"
            "      states:\n"
            "        ready:\n"
            "          on_entry: {set_variable: phase, value: active}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(execution.json);
        check_contains(execution.error,"unknown key 'on_entry'");
    }
    it("prevents a BT action from writing projected state") {
        const char yaml[] =
            "tasks:\n"
            "  - name: chart\n"
            "    statechart: {initial: ready, states: {ready: {}, done: {}}}\n"
            "    actions: {set_variable: chart.task.done, value: '1'}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.json,"\"chart\":{\"status\":\"failed\"}");
    }
    it("resolves a whole command reference from task vars before root variables") {
        const char yaml[]=
            "variables: {notify_command: 'echo root-command'}\n"
            "tasks:\n"
            "  - name: notify\n"
            "    vars: {notify_command: 'echo task-command'}\n"
            "    command: $notify_command\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"stdout\":\"task-command");
    }
    it("rejects retired brace-form command references during workflow admission") {
        const char yaml[]=
            "variables: {notify_command: 'echo ready'}\n"
            "tasks: [{name: notify, command: '{notify_command}'}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_null(execution.json);
        check_contains(execution.error,"Use command: $name");
    }
    it("rejects retired qualified command references during workflow admission") {
        const char yaml[]=
            "variables: {notify_command: 'echo ready'}\n"
            "tasks: [{name: notify, command: $vars.notify_command}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_null(execution.json);
        check_contains(execution.error,"Use command: $name");
    }
    it("expands several inline references in Shell and honors task variable precedence") {
        const char yaml[]=
            "variables: {PRESET: root, suffix: checked}\n"
            "tasks:\n"
            "  - name: preset\n"
            "    vars: {PRESET: task}\n"
            "    shell: {cmd: echo $PRESET-$suffix-$PRESET}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"stdout\":\"task-checked-task");
    }
    it("fails a command before starting the shell if an inline variable is missing") {
        const char yaml[]="tasks: [{name: missing, command: echo $PRAKTOR_ABSENT_INLINE_VARIABLE}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"PRAKTOR_ABSENT_INLINE_VARIABLE");
    }
    it("rejects braced interpolation in a command") {
        const char yaml[]=
            "variables: {PRESET: ready}\n"
            "tasks: [{name: old, command: 'echo ${PRESET}'}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"Use $name in shell commands");
    }
    it("rejects bare names in variable read ports and Selector comparisons") {
        const char *const examples[]={
            "tasks: [{name: old, set_variable: key, from: source}]\n",
            "tasks: [{name: old, switch: source, cases: [{Shell: {cmd: echo ready}}]}]\n",
            "tasks: [{name: old, actions: {assert: {guard: {fields: [source], match: 'source == \"ready\"'}}}}]\n",
            "tasks: [{name: old, actions: {assert: {guard: {fields: [source], match: '$has($source)'}}}}]\n"
        };
        for(size_t i=0u;i<sizeof examples/sizeof examples[0];++i) {
            check(praktor_runtime_execute_yaml(examples[i],strlen(examples[i]),NULL,NULL,NULL,&execution)
                  != PRAKTOR_RUNTIME_OK);
            check_null(execution.json);
            check_contains(execution.error,"$name");
        }
    }
    it("preserves quoted dollar text inside a Selector guard") {
        const char yaml[]=
            "variables: {message: '$literal'}\n"
            "tasks: [{name: check, actions: {assert: {guard: {fields: [message], match: '$message == \"$literal\"'}}}}]\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK,"%s",execution.error);
    }
    it("applies the environment command timeout when the document omits it") {
        const char yaml[]="tasks:\n  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.json,"\"workflow_status\":\"failed\"");
    }
    it("overrides an environment timeout with document defaults for lowered Command nodes") {
        const char yaml[]="defaults: {timeout: 5000, stream_output: false}\ntasks:\n"
            "  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"workflow_status\":\"success\"");
    }
    it("lets an explicit Shell timeout override the document timeout") {
        const char yaml[]="defaults: {timeout: 50}\ntasks:\n  - name: slow\n"
            "    shell:\n      cmd: " BLOCKING_COMMAND "\n      timeout: 5000\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==PRAKTOR_RUNTIME_OK);
    }
    it("keeps the workflow deadline independent of inherited command timeout") {
        const char yaml[]="defaults: {timeout: 5000}\ntasks:\n"
            "  - name: slow\n    command: " BLOCKING_COMMAND "\n";
        praktor_runtime_limits limits=praktor_runtime_limits_default();limits.timeout_ms=50;
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,&limits,NULL,NULL,&execution)==PRAKTOR_RUNTIME_DEADLINE);
    }
    it("runs a two-task native SetVariable DAG") {
        const char yaml[]=
            "tasks:\n"
            "  - name: first\n    set_variable: answer\n    value: ready\n"
            "  - name: second\n    depends_on: [first]\n"
            "    set_variable: done\n    from: $answer\n";
        praktor_runtime_result result={0};
        check(praktor_runtime_limits_reflection()!=NULL);
        check(praktor_runtime_execute_yaml(yaml,sizeof(yaml)-1u,
            NULL,NULL,NULL,&result)==PRAKTOR_RUNTIME_OK);
        check(result.json!=NULL);
        if(result.json)check(strstr(result.json,"\"workflow_status\":\"success\"")!=NULL);
        praktor_runtime_result_destroy(&result);
    }
    it("matches committed predecessor values in an if guard") {
        /* Exercise a nested guard and a later leaf that consumes its local value. */
        const char yaml[]=
            "tasks:\n"
            "  - {name: prepare, set_variable: phase, value: ready}\n"
            "  - name: guarded\n"
            "    depends_on: [prepare]\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - if: {guard: {match: '$phase in [\"ready\", \"waiting\"]', fields: [phase]}}\n"
            "          then: {set_variable: result, value: '0'}\n"
            "          else: {set_variable: result, value: '1'}\n"
            "        - CheckExitCode: {input_key: $result, expected: 0}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
              "guarded",&execution)==PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"guarded\":{\"status\":\"success\"}");
    }
    it("uses the else branch for a negated comparison on a missing field") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    if: {guard: {match: '!($phase == \"ready\")', fields: [phase]}}\n"
            "    then: {CheckExitCode: {input_key: $missing, expected: 0}}\n"
            "    else: {set_variable: result, value: skipped}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"guarded\":{\"status\":\"success\"}");
    }
    it("reevaluates a while guard after the body changes a local variable") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    vars: {phase: waiting, result: '1'}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - while: {guard: {match: '$phase == \"waiting\"', fields: [phase]}}\n"
            "          do:\n"
            "            - {set_variable: phase, value: ready}\n"
            "            - {set_variable: result, value: '0'}\n"
            "        - Precondition:\n"
            "            condition: {guard: {match: '$phase == \"ready\"', fields: [phase]}}\n"
            "            child: {CheckExitCode: {input_key: $result, expected: 0}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"guarded\":{\"status\":\"success\"}");
    }
    it("rejects a field outside the guard bindings during preflight") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    precondition: {guard: {match: '$other == \"ready\"', fields: [phase]}}\n"
            "    child: {set_variable: result, value: ready}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)==
              PRAKTOR_RUNTIME_UNSUPPORTED);
        check(execution.json==NULL);
        check_contains(execution.error,"guard match");
        check_contains(execution.error,"field is not allowed by schema");
    }
    it("compares predecessor output as an exact signed 64-bit integer") {
        const char yaml[]=
            "tasks:\n"
            "  - {name: prepare, set_variable: count, value: '9007199254740993'}\n"
            "  - name: guarded\n"
            "    depends_on: [prepare]\n"
            "    precondition:\n"
            "      guard:\n"
            "        fields: [count]\n"
            "        expr: '$count > 9007199254740992 && -9223372036854775808 < $count'\n"
            "    child: {set_variable: result, value: precise}\n";
        check_equal(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
                    "guarded",&execution), PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"guarded\":{\"status\":\"success\"}");
    }
    it("short circuits numeric conditions before reading missing operands") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    precondition:\n"
            "      guard:\n"
            "        fields: [probe]\n"
            "        expr: '!(has($probe) && $probe > 5) || $probe < 0'\n"
            "    child: {set_variable: result, value: ready}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
    }
    it("accepts a constant numerical guard with no variable bindings") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    precondition: {guard: {fields: [], expr: '2 >= +2 && !(2 != 2)'}}\n"
            "    child: {set_variable: result, value: ready}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
    }
    it("rejects a numeric literal outside int64 during preflight") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    precondition: {guard: {fields: [], expr: 'true || 9223372036854775808 > 0'}}\n"
            "    child: {set_variable: result, value: ready}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_null(execution.json);
        check_contains(execution.error,"signed 64-bit decimal integer");
    }
    it("fails instead of taking else when a numeric operand is invalid") {
        const char yaml[]=
            "tasks:\n"
            "  - name: guarded\n"
            "    vars: {count: '3.5'}\n"
            "    if: {guard: {fields: [count], expr: '$count >= 2'}}\n"
            "    then: {set_variable: result, value: high}\n"
            "    else: {set_variable: result, value: low}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"guard expr field 'count'");
    }
    it("computes exact capacity ranges and observes arithmetic precedence") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    vars: {used: '9007199254740992', requested: '1', limit: '9007199254740993'}\n"
            "    assert:\n"
            "      guard:\n"
            "        fields: [used, requested, limit]\n"
            "        expr: '0 <= $used + $requested && $used + $requested == $limit && "
            "$used + $requested > $used && 2 + 3 * 4 == 14 && (2 + 3) * 4 == 20 && "
            "-($requested - 3) == 2 && -17 / 5 == -3 && -17 % 5 == -2'\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
    }
    it("evaluates bit flags with 64-bit patterns and zero-filled shifts") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    vars: {flags: '-9223372036854775808'}\n"
            "    assert:\n"
            "      guard:\n"
            "        fields: [flags]\n"
            "        expr: '$flags & 1 << 63 != 0 && $flags >> 63 == 1 && "
            "1 << 63 == -9223372036854775808 && ~(1 | 2) == -4 && (7 ^ 3) == 4'\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
    }
    it("reports arithmetic overflow, division by zero and invalid shifts") {
        static const struct { const char *expr,*error; } cases[]={
            {"9223372036854775807 + 1 > 0","signed 64-bit integer overflow"},
            {"-9223372036854775808 - 1 > 0","signed 64-bit integer overflow"},
            {"9223372036854775807 * -2 > 0","signed 64-bit integer overflow"},
            {"-(-9223372036854775808) > 0","signed 64-bit integer overflow"},
            {"-9223372036854775808 / -1 == 0","signed 64-bit integer overflow"},
            {"-9223372036854775808 % -1 == 0","signed 64-bit integer overflow"},
            {"1 / 0 == 0","division by zero"},
            {"1 % 0 == 0","division by zero"},
            {"1 << 64 == 0","shift count must be between 0 and 63"},
            {"1 >> -1 == 0","shift count must be between 0 and 63"}
        };
        for(size_t i=0u;i<sizeof cases/sizeof cases[0];++i) {
            check_equal(execute_constant_guard(cases[i].expr,&execution),PRAKTOR_RUNTIME_FAILED);
            check_contains(execution.error,cases[i].error);
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("short circuits arithmetic but still rejects non-predicate expressions during preflight") {
        check_equal(execute_constant_guard("!(false && 1 / 0 == 0) || 1 << 64 == 0",&execution),
                    PRAKTOR_RUNTIME_OK);
        praktor_runtime_result_destroy(&execution);
        check_equal(execute_constant_guard("1 + 2",&execution),PRAKTOR_RUNTIME_UNSUPPORTED);
        check_contains(execution.error,"must produce a predicate");
    }
    it("runs nested wait_until and assert leaves using the task context") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    vars: {status: ready, count: '0'}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - wait_until:\n"
            "            guard: {fields: [status], match: '$status == \"ready\"'}\n"
            "            timeout_ms: 10\n"
            "        - assert:\n"
            "            guard: {fields: [count], expr: '$count == 0'}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"verify\":{\"status\":\"success\"}");
    }
    it("reports a false assertion with the explicitly selected actual value") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    vars: {count: '2'}\n"
            "    assert:\n"
            "      guard: {fields: [count], expr: '$count == 0'}\n"
            "      message: jobs failed\n"
            "      actual: $count\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"assert: jobs failed (count=2)");
    }
    it("reports a wait_until timeout as a task failure") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    wait_until: {guard: {fields: [], expr: 'false'}, timeout_ms: 10}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"wait_until timed out after 10 ms");
    }
    it("rejects a nonpositive wait_until timeout before execution") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    wait_until: {guard: {fields: [], expr: 'true'}, timeout_ms: 0}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_null(execution.json);
        check_contains(execution.error,"timeout_ms must be a positive");
    }
    it("binds JSONPath scalars and filtered match counts to guards") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    vars:\n"
            "      report: '{\"health\":{\"status\":\"ready\"},\"exit_codes\":[0,2,3],\"total\":9007199254740993}'\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - ParseJson: {input_key: $report, path: '$.health.status', output_key: health, result: one}\n"
            "        - ParseJson: {input_key: $report, path: '$.total', output_key: total, result: one}\n"
            "        - ParseJson: {input_key: $report, path: '$.exit_codes[?@ != 0]', output_key: failures, result: count}\n"
            "        - assert: {guard: {fields: [health], match: '$health == \"ready\"'}}\n"
            "        - assert: {guard: {fields: [failures, total], expr: '$failures == 2 && $total > 9007199254740992'}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"verify\":{\"status\":\"success\"}");
    }
    it("rejects a nonstreamable cardinality query during plan admission") {
        const char yaml[]=
            "tasks:\n"
            "  - name: verify\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - ParseJson: {input_key: $report, path: '$..status', result: count}\n"
            "        - assert: {guard: {fields: [], expr: 'true'}}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_UNSUPPORTED);
        check_null(execution.json);
        check_contains(execution.error,"ParseJson");
    }
    it("rejects unsupported metadata before any selected task runs") {
        static const struct { const char *yaml,*message; } cases[]={
            {"env: {MODE: global}\ntasks: [{name: check, set_variable: observed, value: ran}]\n",
             "workflow field 'env'"},
            {"outputs: {required_result: {type: string, required: true}}\n"
             "tasks: [{name: check, set_variable: observed, value: ran}]\n",
             "workflow field 'outputs'"},
            {"tasks: [{name: check, env: {MODE: task}, set_variable: observed, value: ran}]\n",
             "task 'check': field 'env'"},
            {"tasks: [{name: check, when: 'false', set_variable: observed, value: ran}]\n",
             "task 'check': field 'when'"},
            {"tasks:\n"
             "  - {name: check, triggers: {on_success: [after]}, set_variable: observed, value: ran}\n"
             "  - {name: after, set_variable: after, value: ran}\n",
             "task 'check': field 'triggers'"},
            {"tasks: [{name: check, timeout: 1, command: echo ran}]\n","field 'timeout'"},
            {"tasks: [{name: check, working_dir: missing, command: echo ran}]\n","field 'working_dir'"},
            {"tasks: [{name: check, dotEnv: [missing.env], command: echo ran}]\n","field 'dotEnv'"},
            {"tasks: [{name: check, silent: true, command: echo ran}]\n","field 'silent'"},
            {"tasks: [{name: check, sources: [source], command: echo ran}]\n","field 'sources'"},
            {"tasks: [{name: check, generates: [artifact], command: echo ran}]\n","field 'generates'"},
            {"tasks: [{name: check, each: {items: [a, b]}, command: echo ran}]\n","field 'each'"},
            {"tasks: [{name: check, retries: {count: 1}, command: echo ran}]\n","field 'retries'"},
            {"tasks: [{name: check, finally: clean, command: echo ran}, {name: clean, command: echo cleanup}]\n",
             "field 'finally'"}
        };
        for(size_t i=0u;i<sizeof cases/sizeof cases[0];++i) {
            check_equal(praktor_runtime_execute_task_yaml(cases[i].yaml,strlen(cases[i].yaml),
                        NULL,NULL,NULL,"check",&execution),PRAKTOR_RUNTIME_UNSUPPORTED);
            check_null(execution.json);
            check_contains(execution.error,cases[i].message);
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("settles custom Shell output keys with canonical result labels") {
        const char yaml[]=
            "tasks:\n"
            "  - name: emit\n"
            "    shell: {cmd: echo ready, output_key: captured, stderr_key: errors, exit_code_key: code}\n"
            "  - name: verify\n"
            "    depends_on: [emit]\n"
            "    actions: {CheckExitCode: {input_key: $code, expected: 0}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"stdout\":\"ready");
        check_contains(execution.json,"\"exit_code\":\"0\"");
    }
    it("reports a failed Shell exit code through its custom key") {
        const char yaml[]="tasks: [{name: emit, shell: {cmd: exit 7, exit_code_key: code}}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.json,"\"exit_code\":\"7\"");
    }
    it("exports only declared BT values to successors after success") {
        const char yaml[]=
            "tasks:\n"
            "  - name: make\n"
            "    outputs: [$summary]\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - {set_variable: summary, value: ready}\n"
            "        - {set_variable: local_only, value: hidden}\n"
            "  - name: verify\n"
            "    depends_on: [make]\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - assert: {guard: {fields: [summary], match: '$summary == \"ready\"'}}\n"
            "        - assert: {guard: {fields: [local_only], expr: '!has($local_only)'}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"outputs\":{\"summary\":\"ready\"}");
    }
    it("fails an incomplete export without publishing earlier values") {
        const char yaml[]=
            "tasks: [{name: make, outputs: [$summary, $missing], "
            "actions: {Sequence: [{set_variable: summary, value: ready}]}}]\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"output 'missing' is unavailable");
        check(strstr(execution.json,"\"summary\"")==NULL);
    }
    it("validates export declarations before running the workflow") {
        const char *const lists[]={"[summary]", "[$summary, $summary]", "$summary"};
        for(size_t i=0u;i<sizeof lists/sizeof lists[0];++i) {
            char yaml[192];
            int length=snprintf(yaml,sizeof yaml,
                "tasks: [{name: make, command: echo ready, outputs: %s}]",lists[i]);
            check(length>0&&(size_t)length<sizeof yaml);
            check(praktor_runtime_execute_yaml(yaml,(size_t)length,NULL,NULL,NULL,&execution)!=
                  PRAKTOR_RUNTIME_OK);
            check_null(execution.json);
            check_contains(execution.error,"output");
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("executes previously private Repeat, Delay and RunOnce controls") {
        const char yaml[]=
            "tasks:\n"
            "  - name: controls\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - repeat: 2\n"
            "          child:\n"
            "            run_once:\n"
            "              child:\n"
            "                delay: 1\n"
            "                child: {set_variable: result, value: ready}\n"
            "        - assert: {guard: {fields: [result], match: '$result == \"ready\"'}}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
    }
    it("enforces a Timeout decorator inside a workflow behavior tree") {
        const char yaml[]=
            "tasks: [{name: controls, actions: {timeout: 1, child: {Sleep: {duration: 2s}}}}]\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_FAILED,"%s",execution.error);
        check_contains(execution.json,"\"controls\":{\"status\":\"failed\"}");
    }
    it("executes a plain sequence and the selected positional switch case") {
        const char yaml[]=
            "tasks:\n"
            "  - name: check\n"
            "    vars: {mode: '1'}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - switch: $mode\n"
            "          cases:\n"
            "            - {set_variable: result, value: '1'}\n"
            "            - {set_variable: result, value: '0'}\n"
            "        - CheckExitCode: {input_key: $result, expected: 0}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
        check_contains(execution.json,"\"check\":{\"status\":\"success\"}");
    }
    it("uses the control iteration budget for a while with a scalar condition") {
        const char yaml[]=
            "tasks:\n"
            "  - name: check\n"
            "    vars: {again: 'true'}\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - while: $again\n"
            "          do:\n"
            "            - {set_variable: result, value: '0'}\n"
            "            - {set_variable: again, value: 'false'}\n"
            "        - CheckExitCode: {input_key: $result, expected: 0}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_OK);
    }
    it("executes selected nested Shell leaves and publishes their task-local outputs") {
        const char yaml[]=
            "tasks:\n"
            "  - name: check\n"
            "    outputs: [$captured]\n"
            "    actions:\n"
            "      Fallback:\n"
            "        - Shell: {cmd: exit 7}\n"
            "        - Shell: {cmd: echo nested-ready, output_key: captured}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"captured\":\"nested-ready");
    }
    it("rejects an unknown Tool before any action even in an untaken branch") {
        const char yaml[]=
            "tasks:\n"
            "  - name: check\n"
            "    actions:\n"
            "      Fallback:\n"
            "        - Shell: {cmd: echo ready}\n"
            "        - Tool: {service: absent}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_INVALID);
        check_null(execution.json);
        check_contains(execution.error,"unknown tool service 'absent'");
    }
    it("matches string case labels exactly and uses default only when no case matches") {
        const char yaml[]=
            "tasks:\n"
            "  - name: label\n"
            "    vars: {route: '2'}\n"
            "    outputs: [$selected]\n"
            "    switch: $route\n"
            "    cases:\n"
            "      preview: {set_variable: selected, value: incorrect}\n"
            "      '2': {set_variable: selected, value: matched}\n"
            "    default: {set_variable: selected, value: defaulted}\n"
            "  - name: unmatched\n"
            "    depends_on: [label]\n"
            "    vars: {route: PREVIEW}\n"
            "    outputs: [$fallback]\n"
            "    actions:\n"
            "      switch: $route\n"
            "      cases:\n"
            "        preview: {set_variable: fallback, value: incorrect}\n"
            "      default: {set_variable: fallback, value: defaulted}\n";
        check(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"selected\":\"matched\"");
        check_contains(execution.json,"\"fallback\":\"defaulted\"");
    }
    it("does not execute the default after a matching case fails") {
        const char yaml[]=
            "tasks:\n"
            "  - name: check\n"
            "    vars: {route: preview}\n"
            "    switch: $route\n"
            "    cases:\n"
            "      preview: {assert: {guard: {fields: [], expr: 'false'}}}\n"
            "    default: {set_variable: selected, value: defaulted}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"assert");
    }
    it("rejects duplicate labels and a default on positional cases") {
        const char *const cases[]={
            "{preview: {Sleep: 1ms}, preview: {Sleep: 1ms}}",
            "[{Sleep: 1ms}], default: {Sleep: 1ms}"
        };
        for(size_t i=0u;i<sizeof cases/sizeof cases[0];++i) {
            char yaml[256];
            const int length=snprintf(yaml,sizeof yaml,
                "tasks: [{name: check, switch: $route, cases: %s}]",cases[i]);
            check(length>0&&(size_t)length<sizeof yaml);
            check(praktor_runtime_execute_yaml(yaml,(size_t)length,NULL,NULL,NULL,&execution)
                  !=PRAKTOR_RUNTIME_OK);
            check_null(execution.json);
            check_contains(execution.error,i?"switch default":"Duplicate mapping key");
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("advances ready tasks together and publishes their outputs before lifecycle observation") {
        const char yaml[]=
            "parallelism: 2\n"
            "statechart:\n"
            "  initial: waiting\n"
            "  states:\n"
            "    waiting:\n"
            "      transitions:\n"
            "        - 'on': task.started\n"
            "          guard: {fields: [event.data.task], match: '$event.data.task == \"right\"'}\n"
            "          target: released\n"
            "    released:\n"
            "      transitions:\n"
            "        - 'on': task.finished\n"
            "          guard: {fields: [left_result, right_result], expr: 'has($left_result) && has($right_result)'}\n"
            "          target: published\n"
            "    published: {type: final}\n"
            "tasks:\n"
            "  - name: left\n"
            "    outputs: [$left_result]\n"
            "    actions:\n"
            "      Sequence:\n"
            "        - wait_until:\n"
            "            guard: {fields: [chart.workflow.released], expr: '$chart.workflow.released == 1'}\n"
            "            timeout_ms: 200\n"
            "        - {set_variable: left_result, value: left}\n"
            "  - {name: right, set_variable: right_result, value: right}\n"
            "  - name: join\n"
            "    depends_on: [left, right]\n"
            "    assert: {guard: {fields: [chart.workflow.published], expr: '$chart.workflow.published == 1'}}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,"join",&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"left_result\":\"left\"");
        check_contains(execution.json,"\"right_result\":\"right\"");
        check_contains(execution.json,"\"join\":{\"status\":\"success\"}");
    }
    it("discards a conflicting group while retaining the prior group's output") {
        const char yaml[]=
            "parallelism: 2\n"
            "tasks:\n"
            "  - {name: prepare, set_variable: baseline, value: ready}\n"
            "  - {name: left, depends_on: [prepare], set_variable: shared, value: one}\n"
            "  - {name: right, depends_on: [prepare], set_variable: shared, value: two}\n"
            "  - {name: join, depends_on: [left, right], set_variable: joined, value: unexpected}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"ready tasks share output key 'shared'");
        check_contains(execution.json,"\"baseline\":\"ready\"");
        check(strstr(execution.json,"\"shared\"")==NULL);
        check_contains(execution.json,"\"join\":{\"status\":\"pending\"}");
    }
    it("closes a running Shell when another ready task fails") {
        const char yaml[]=
            "parallelism: 2\n"
            "tasks:\n"
            "  - name: prepare\n"
            "    command: echo prepared\n"
            "  - name: waiting\n"
            "    depends_on: [prepare]\n"
            "    command: " BLOCKING_COMMAND "\n"
            "  - name: rejecting\n"
            "    depends_on: [prepare]\n"
            "    assert: {guard: {fields: [], expr: 'false'}, message: rejected cohort}\n";
        check_equal(praktor_runtime_execute_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"rejected cohort");
        check_contains(execution.json,"\"waiting\":{\"status\":\"failed\"}");
        check_contains(execution.json,"\"prepare\":{\"status\":\"success\"");
    }
    it("settles two ready Shell tasks with independent exported keys") {
        const char yaml[]=
            "parallelism: 2\n"
            "tasks:\n"
            "  - name: left\n"
            "    outputs: [$left_log]\n"
            "    shell: {cmd: echo left-ready, output_key: left_log}\n"
            "  - name: right\n"
            "    outputs: [$right_log]\n"
            "    shell: {cmd: echo right-ready, output_key: right_log}\n"
            "  - name: verify\n"
            "    depends_on: [left, right]\n"
            "    assert: {guard: {fields: [left_log, right_log], expr: 'has($left_log) && has($right_log)'}}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,"verify",&execution)
              ==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"left_log\":\"left-ready");
        check_contains(execution.json,"\"right_log\":\"right-ready");
    }
    it("rejects invalid DAG parallelism before executing any task") {
        const char *const widths[]={"0","17","1.5"};
        for(size_t i=0u;i<sizeof widths/sizeof widths[0];++i) {
            char yaml[128];
            const int n=snprintf(yaml,sizeof yaml,
              "parallelism: %s\ntasks: [{name: go, command: echo ready}]",widths[i]);
            check(n>0&&(size_t)n<sizeof yaml);
            check_equal(praktor_runtime_execute_yaml(yaml,(size_t)n,NULL,NULL,NULL,&execution),
                        PRAKTOR_RUNTIME_INVALID);
            check_null(execution.json);
            check_contains(execution.error,"parallelism");
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("selects a library entry with its scoped closure and caller dependencies") {
        const char yaml[]=
            "includes: {devices: '" PRAKTOR_DEVICE_AUDIT_LIBRARY "'}\n"
            "variables: {minimum_voltage: '1', mode: idle}\n"
            "tasks:\n"
            "  - {name: prepare, set_variable: configured, value: yes}\n"
            "  - {name: unused, command: echo skipped}\n"
            "  - name: commissioning\n"
            "    depends_on: [prepare]\n"
            "    vars: {mode: commissioning}\n"
            "    uses: devices.verify\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
            "commissioning",&execution)==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"commissioning.prepare\":{\"status\":\"success\"");
        check_contains(execution.json,"\"prepare\":{\"status\":\"success\"");
        check_contains(execution.json,"\"unused\":{\"status\":\"skipped\"}");
        check_contains(execution.json,"\"audit_mode\":\"commissioning\"");
        check_contains(execution.json,"\"audit_ready\":\"ready\"");
    }
    it("selects a generated task with its dependencies and uses isolated template variables") {
        const char yaml[]=
            "variables: {list: '[\"wrong\"]'}\n"
            "tasks:\n"
            "  - {name: setup, set_variable: setup_ok, value: ready}\n"
            "  - {name: template_setup, set_variable: template_ok, value: ready}\n"
            "  - name: inspect\n"
            "    depends_on: [setup]\n"
            "    vars: {list: '[\"north\",\"south\"]', item: invalid, setting: caller}\n"
            "    dynamic_tasks:\n"
            "      items_variable: $list\n"
            "      template:\n"
            "        name: $item-$index\n"
            "        depends_on: [template_setup]\n"
            "        vars: {setting: template}\n"
            "        actions:\n"
            "          Sequence:\n"
            "            - assert: {guard: {fields: [setup_ok, template_ok, setting], match: '$setup_ok == \"ready\" && $template_ok == \"ready\" && $setting == \"caller\"'}}\n"
            "            - assert: {guard: {fields: [index, item], match: '$index == \"1\" && $item == \"south\"'}}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
            "inspect.south-1",&execution)==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"inspect.south-1\":{\"status\":\"success\"}");
        check_contains(execution.json,"\"inspect.north-0\":{\"status\":\"skipped\"}");
        check_contains(execution.json,"\"inspect\":{\"status\":\"skipped\"}");
    }
    it("collects parallel member outputs in input order without sharing their keys") {
        const char yaml[]=
            "parallelism: 3\n"
            "variables: {list: '[1,2]'}\n"
            "tasks:\n"
            "  - {name: peer, set_variable: ordinary_result, value: 3}\n"
            "  - name: matrix\n"
            "    dynamic_tasks:\n"
            "      items_variable: $list\n"
            "      collect: $results\n"
            "      template: {name: item-$index, set_variable: matrix_reading, from: $item}\n"
            "  - name: confirm\n"
            "    depends_on: [matrix, peer]\n"
            "    sequence:\n"
            "      - ParseJson: {input_key: $results, path: '$[0].matrix_reading', output_key: first, result: one}\n"
            "      - ParseJson: {input_key: $results, path: '$[1].matrix_reading', output_key: second, result: one}\n"
            "      - assert: {guard: {fields: [first, second, matrix_reading, ordinary_result], expr: '$first == 1 && $second == 2 && $ordinary_result == 3 && !has($matrix_reading)'}}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
            "confirm",&execution)==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"matrix.item-0\":{\"status\":\"success\",\"outputs\":{\"matrix_reading\":\"1\"}}");
        check_contains(execution.json,"\"matrix.item-1\":{\"status\":\"success\",\"outputs\":{\"matrix_reading\":\"2\"}}");
        check_contains(execution.json,"\"results\":\"[");
    }
    it("fails join settlement when the collected array exceeds the value budget") {
        char text[3001],yaml[4096];
        memset(text,'x',sizeof text-1u);
        text[sizeof text-1u]='\0';
        const int n=snprintf(yaml,sizeof yaml,
            "variables: {list: '[1,2]', large: '%s'}\n"
            "parallelism: 2\n"
            "tasks:\n"
            "  - name: matrix\n"
            "    dynamic_tasks:\n"
            "      items_variable: $list\n"
            "      collect: $results\n"
            "      template: {name: item-$index, set_variable: collected, from: $large}\n",
            text);
        check(n>0&&(size_t)n<sizeof yaml);
        check_equal(praktor_runtime_execute_yaml(yaml,(size_t)n,NULL,NULL,NULL,&execution),
                    PRAKTOR_RUNTIME_FAILED);
        check_contains(execution.error,"exceed 4096 bytes");
        check_contains(execution.json,"\"matrix\":{\"status\":\"failed\"}");
        check_null(strstr(execution.json,"\"results\""));
    }
    it("runs the dynamic join with its prerequisite for an empty array") {
        const char yaml[]=
            "variables: {list: '[]'}\n"
            "tasks:\n"
            "  - {name: setup, set_variable: ready, value: yes}\n"
            "  - name: inspect\n"
            "    depends_on: [setup]\n"
            "    dynamic_tasks: {items_variable: $list, collect: $results, template: {name: item-$index, command: echo unused}}\n"
            "  - {name: confirm, depends_on: [inspect], set_variable: done, from: $ready}\n";
        check(praktor_runtime_execute_task_yaml(yaml,sizeof yaml-1u,NULL,NULL,NULL,
            "confirm",&execution)==PRAKTOR_RUNTIME_OK,"%s",execution.error);
        check_contains(execution.json,"\"inspect\":{\"status\":\"success\",\"outputs\":{\"results\":\"[]\"}}");
        check_contains(execution.json,"\"done\":\"yes\"");
        check(strstr(execution.json,"inspect.item-")==NULL);
    }
    it("rejects oversized, duplicate and cyclic expansions at admission") {
        static const struct {
            const char *list,*name,*after,*diagnostic;
            int capacity;
        } cases[]={
            {"[1,2]","job-$index","setup","capacity",3},
            {"[1,2]","same","setup","Duplicate task",64},
            {"[1]","job-$index","inspect","Cycle detected",64},
            {"{\"invalid\":1}","job-$index","setup","JSON array",64},
            {"[1]","'${index}'","setup","invalid dynamic task name",64}
        };
        for(size_t i=0u;i<sizeof cases/sizeof *cases;++i) {
            char yaml[640];
            const int n=snprintf(yaml,sizeof yaml,
                "variables: {list: '%s'}\ntasks:\n"
                "  - {name: setup, command: echo unexpected}\n"
                "  - name: inspect\n"
                "    depends_on: [%s]\n"
                "    dynamic_tasks: {items_variable: $list, template: {name: %s, command: echo generated}}\n",
                cases[i].list,cases[i].after,cases[i].name);
            check(n>0&&(size_t)n<sizeof yaml);
            praktor_runtime_limits limits=praktor_runtime_limits_default();
            limits.max_tasks=cases[i].capacity;
            check_equal(praktor_runtime_execute_yaml(yaml,(size_t)n,&limits,NULL,NULL,&execution),
                        PRAKTOR_RUNTIME_INVALID);
            check_null(execution.json);
            check_contains(execution.error,cases[i].diagnostic);
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("rejects ineffective runner fields and unreadable variable names at admission") {
        static const struct {const char *yaml,*diagnostic;} cases[]={
            {"tasks: [{name: go, command: echo unexpected, args: [extra]}]","'args'"},
            {"tasks: [{name: go, command: echo unexpected, stdin: extra}]","'stdin'"},
            {"variables: {vars.legacy: old}\ntasks: [{name: go, command: echo unexpected}]",
             "invalid variable name in variables"},
            {"tasks: [{name: go, vars: {1bad: invalid}, command: echo unexpected}]",
             "invalid variable name in vars"},
            {"tasks: [{name: go, set_variable: '1bad', value: text}]","literal variable name"},
            {"tasks: [{name: go, sequence: [{Shell: {cmd: echo unexpected, output_key: 'a b'}}]}]",
             "literal variable name"}
        };
        for(size_t i=0u;i<sizeof cases/sizeof *cases;++i) {
            check(praktor_runtime_execute_yaml(cases[i].yaml,strlen(cases[i].yaml),
                NULL,NULL,NULL,&execution)!=PRAKTOR_RUNTIME_OK);
            check_null(execution.json);
            check_contains(execution.error,cases[i].diagnostic);
            praktor_runtime_result_destroy(&execution);
        }
    }
    it("refuses old C++ script workflows without fallback") {
        const char yaml[]="tasks:\n  - name: old\n    script: print(1)\n";
        praktor_runtime_result result={0};
        check(praktor_runtime_execute_yaml(yaml,sizeof(yaml)-1u,
            NULL,NULL,NULL,&result)!=PRAKTOR_RUNTIME_OK);
        praktor_runtime_result_destroy(&result);
    }
}
