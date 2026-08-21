import json
import pathlib
import re
import sys


try:
    import yaml
    from jsonschema import Draft7Validator
except ImportError as exc:
    print(f"schema_validation_test: missing dependency: {exc}", file=sys.stderr)
    sys.exit(1)


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCHEMA_PATH = REPO_ROOT / "grammar.schema.json"
FORCE_TERMINATE_CORPUS_PATH = (
    REPO_ROOT / "praktor" / "test" / "data" / "force_terminate_corpus.tsv"
)


def load_validator():
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    return Draft7Validator(schema)


def validate_text(validator, name, text, should_pass):
    data = yaml.safe_load(text)
    errors = sorted(validator.iter_errors(data), key=lambda err: (list(err.path), err.message))
    passed = not errors

    if passed != should_pass:
        print(f"{name}: expected {'pass' if should_pass else 'reject'}", file=sys.stderr)
        if errors:
            for error in errors[:5]:
                path = ".".join(str(part) for part in error.path) or "<root>"
                print(f"  {path}: {error.message}", file=sys.stderr)
        sys.exit(1)


def validate_file(validator, relative_path):
    path = REPO_ROOT / relative_path
    validate_text(validator, str(relative_path), path.read_text(encoding="utf-8"), True)


def require_contains(relative_path, needle):
    path = REPO_ROOT / relative_path
    text = path.read_text(encoding="utf-8")
    if needle not in text:
        print(f"{relative_path}: missing required snippet: {needle}", file=sys.stderr)
        sys.exit(1)


def require_matches(relative_path, pattern):
    path = REPO_ROOT / relative_path
    text = path.read_text(encoding="utf-8")
    if re.search(pattern, text, re.MULTILINE) is None:
        print(f"{relative_path}: missing required pattern: {pattern}", file=sys.stderr)
        sys.exit(1)


def validate_structured_http_contracts():
    require_contains(pathlib.Path("praktor/templates/llm-openai.yml"), 'import("net");')
    require_contains(pathlib.Path("praktor/templates/llm-gemini.yml"), 'import("net");')
    require_contains(pathlib.Path("praktor/templates/llm-claude.yml"), 'import("net");')
    require_contains(pathlib.Path("examples/polymarket-dashboard.yml"), 'import("net");')

    require_contains(pathlib.Path("praktor/templates/llm-openai.yml"), 'var body = json.stringify(map{')
    require_contains(pathlib.Path("praktor/templates/llm-gemini.yml"), 'var body = json.stringify(map{')
    require_contains(pathlib.Path("praktor/templates/llm-claude.yml"), 'var body = json.stringify(map{')

    require_matches(
        pathlib.Path("praktor/templates/llm-openai.yml"),
        r'http\.post\([\s\S]*?body,\s*map\{',
    )
    require_matches(
        pathlib.Path("praktor/templates/llm-gemini.yml"),
        r'http\.post\([\s\S]*?body,\s*map\{',
    )
    require_matches(
        pathlib.Path("praktor/templates/llm-claude.yml"),
        r'http\.post\([\s\S]*?body,\s*map\{',
    )
    require_matches(
        pathlib.Path("examples/polymarket-dashboard.yml"),
        r'http\.get\([\s\S]*?map\{\}',
    )


def validate_force_terminate_forms(validator):
    expected_types = {"bool": bool, "int": int, "str": str, "null": type(None)}
    lines = FORCE_TERMINATE_CORPUS_PATH.read_text(encoding="utf-8").splitlines()
    cases = [line.split("\t") for line in lines if line and not line.startswith("#")]

    for fields in cases:
        if len(fields) != 5:
            print(f"invalid force_terminate corpus row: {fields!r}", file=sys.stderr)
            sys.exit(1)
        name, scalar, yaml_type, disposition, _ = fields
        expected_type = expected_types[yaml_type]
        should_pass = disposition == "pass"
        text = f"""
name: force-terminate-{name}
tasks:
  - name: query_worker
    managed_process:
      operation: status
      identity:
        image_name: worker.exe
      force_terminate: {scalar}
"""
        loaded = yaml.safe_load(text)
        actual = loaded["tasks"][0]["managed_process"]["force_terminate"]
        if type(actual) is not expected_type:
            print(
                f"{name}: expected YAML type {expected_type.__name__}, "
                f"got {type(actual).__name__}",
                file=sys.stderr,
            )
            sys.exit(1)
        validate_text(validator, f"force_terminate_{name}", text, should_pass)


def main():
    validator = load_validator()

    for folder in (pathlib.Path("examples"), pathlib.Path("pistol/examples")):
        for path in sorted((REPO_ROOT / folder).glob("*.yml")):
            validate_file(validator, folder / path.name)

    validate_file(validator, pathlib.Path("praktor/test/workflows/system-actions.yml"))

    validate_text(
        validator,
        "accept_service_action",
        """
name: valid-service-action
tasks:
  - name: query_service
    service:
      operation: status
      name: EventLog
      profile: windows_scm
      arguments: [--literal, "argument with spaces"]
      timeout_ms: 30000
      poll_interval_ms: 200
""",
        True,
    )

    validate_text(
        validator,
        "reject_invalid_service_action",
        """
name: invalid-service-action
tasks:
  - name: reload_service
    service:
      operation: reload
      name: EventLog
""",
        False,
    )

    validate_text(
        validator,
        "accept_managed_process_action",
        """
name: valid-managed-process-action
tasks:
  - name: start_worker
    managed_process:
      operation: start
      executable: worker.exe
      arguments: [--mode, "safe value"]
      working_directory: C:/workers
      identity:
        image_name: worker.exe
      startup_timeout_ms: 5000
      stop_timeout_ms: 5000
      force_terminate: true
""",
        True,
    )

    validate_text(
        validator,
        "reject_invalid_managed_process_action",
        """
name: invalid-managed-process-action
tasks:
  - name: start_worker
    managed_process:
      operation: start
      identity:
        image_name: worker.exe
""",
        False,
    )

    validate_force_terminate_forms(validator)

    validate_text(
        validator,
        "flat_bt_inline",
        """
name: verify-flat-bt
variables:
  BUILD: ok
tasks:
  - name: orchestrate
    if: true
    then:
      - set_variable: status
        value: ready
      - switch: 0
        cases:
          - shell: echo primary
    else:
      - shell: echo secondary
  - name: timed
    timeout: 100
    child:
      subtree: nested_tree
  - name: looped
    while: false
    do:
      - shell: echo never
  - name: repeated
    repeat: 2
    child:
      shell: echo repeat
  - name: delayed
    delay: 10
    child:
      shell: echo delay
  - name: copy_value
    set_variable: result
    from: source
  - name: postprocess
    command: echo hi
    script: |
      ctx.output("done", "yes");
""",
        True,
    )

    validate_text(
        validator,
        "accept_extended_bt_nodes",
        """
name: extended-bt
tasks:
  - name: fallback_task
    fallback:
      - shell: echo one
      - selector:
          - shell: echo two
  - name: pipeline_task
    pipeline_sequence:
      input_key: source
      output_key: result
      children:
        - shell: echo pipe
  - name: run_once_task
    run_once: true
    child:
      shell: echo once
  - name: consume_task
    consume_queue: jobs
    child:
      shell: echo item
  - name: precondition_task
    precondition: true
    child:
      shell: echo guarded
  - name: updated_task
    entry_updated: payload
    child:
      shell: echo changed
  - name: keep_running_task
    keep_running_until_failure: 2
    child:
      shell: echo keep-running
  - name: nested_decorators
    run_once: true
    child:
      keep_running_until_failure:
        max_iterations: 2
        child:
          consume_queue:
            queue_key: jobs
            item_key: job
            child:
              precondition:
                condition: true
                child:
                  entry_updated:
                    watch_key: payload
                    child:
                      shell: echo nested
""",
        True,
    )

    validate_text(
        validator,
        "reject_removed_retry",
        """
name: bad-retry
tasks:
  - name: bad
    retry: 3
    child:
      shell: echo retry
""",
        False,
    )

    validate_text(
        validator,
        "reject_removed_continue_on_error",
        """
name: bad-continue
tasks:
  - name: bad
    command: echo hi
    continue_on_error: true
""",
        False,
    )

    validate_text(
        validator,
        "reject_unknown_dynamic_template_key",
        """
name: bad-dynamic-template
tasks:
  - name: fanout
    dynamic_tasks:
      items_variable: tasks.query.outputs.data
      template:
        name: generated_{{ index }}
        command: echo generated
        timeuot: 5s
""",
        False,
    )

    validate_text(
        validator,
        "reject_multiple_command_post_processors",
        """
name: bad-command-post-processors
tasks:
  - name: ambiguous
    command: echo data
    parse_json:
      path: $.status
      output_key: status
    parse_lines:
      output_key: lines
""",
        False,
    )

    validate_text(
        validator,
        "accept_modular_workflow_fields",
        """
name: modular
includes:
  common: common-tasks.yml
tasks:
  - name: build
    command: echo build
""",
        True,
    )

    validate_text(
        validator,
        "accept_task_runtime_fields",
        """
name: task-runtime-fields
tasks:
  - name: cleanup
    command: echo cleanup
  - name: build
    sources:
      - src/**/*.cpp
    generates: build/app.exe
    finally: cleanup
    actions:
      sequence:
        - shell: echo build
""",
        True,
    )

    validate_text(
        validator,
        "reject_command_with_actions_runner",
        """
name: conflicting-actions
tasks:
  - name: build
    command: echo build
    actions:
      shell: echo duplicate
""",
        False,
    )

    validate_text(
        validator,
        "reject_unknown_top_level_key",
        """
name: bad-root
varibles:
  BUILD: release
tasks:
  - name: build
    command: echo build
""",
        False,
    )

    validate_text(
        validator,
        "reject_removed_imports",
        """
name: removed-imports
imports: []
tasks:
  - name: build
    command: echo build
""",
        False,
    )

    validate_text(
        validator,
        "reject_removed_embedded",
        """
name: removed-embedded
embedded: {}
tasks:
  - name: build
    command: echo build
""",
        False,
    )

    validate_text(
        validator,
        "reject_empty_runtime_values",
        """
name: empty-values
tasks:
  - name: ""
    command: echo invalid
  - name: invalid-finally
    command: echo invalid
    finally: ""
  - name: invalid-dynamic
    dynamic_tasks:
      items_variable: ""
      template:
        name: ""
        command: ""
""",
        False,
    )

    validate_text(
        validator,
        "reject_empty_command_array_entry",
        """
name: empty-command-entry
tasks:
  - name: build
    command: [echo build, ""]
""",
        False,
    )

    validate_text(
        validator,
        "reject_removed_native_modules",
        """
name: removed-native-modules
native_modules:
  - name: native
    path: native.dll
tasks:
  - name: build
    command: echo build
""",
        False,
    )

    validate_text(
        validator,
        "legacy_set_variable_inline",
        """
name: bad-legacy-setter
tasks:
  - name: bad
    set_variable:
      key: status
      value: ready
""",
        False,
    )

    validate_text(
        validator,
        "legacy_if_then_else_inline",
        """
name: bad-legacy-if
tasks:
  - name: bad
    if_then_else:
      condition: true
      then:
        - shell: echo hi
""",
        False,
    )

    validate_text(
        validator,
        "bad_multiple_runners_inline",
        """
name: bad-runners
tasks:
  - name: bad
    command: echo hi
    uses: ./other.yml
""",
        False,
    )

    validate_structured_http_contracts()

    print("schema_validation_test: PASS")


if __name__ == "__main__":
    main()
