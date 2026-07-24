# Pistol Examples (BT-Native)

This directory contains Behavior Tree (BT) powered operational workflows.

Operational scripts import TurboScript's `os` module; child-process calls pass an executable and separate arguments without shell parsing. Result schemas are shared through `result_models.tbs` and imported relative to each workflow file.

Serialized outputs use two typed envelopes with a shared nested action model:

- `ActionResult`: `action`, `target`, `success`, `running`, and `details`
- `StatusResult`: `status` and `action: ActionResult`
- `ProcessResult`: `status`, `action: ActionResult`, `process`, `pid`, and `output`

Use `make_action_result(...)` from the same module to construct the nested action and normalize script conditions to mapper `bool` values.

## Service Operations

- `service-status-example.yml`
  - `os.service_status` query with a structured result.
- `service-start-example.yml`
  - `os.service_start` operation with explicit success reporting.
- `service-stop-example.yml`
  - `os.service_stop` operation with explicit success reporting.
- `service-restart-example.yml`
  - `os.service_stop` followed by `os.service_start` with both results preserved.

## Process Operations

- `process-status-example.yml`
  - `os.process_start` query for a process PID, followed by wait/read/close.
- `process-stop-example.yml`
  - `os.process_start` invocation of the platform process terminator.

## Event-Driven Operations

- `ops-event-handler-example.yml`
  - BT Sequence + `wait_event` + `parse_json` + `if` branching. Demonstrates reactive operational workflow triggered by JSON events.

## Host & Session Operations

- `host-shutdown-example.yml`
  - `os.shutdown` with the native action result.
- `host-reboot-example.yml`
  - `os.reboot` with the native action result.
- `host-suspend-example.yml`
  - `os.process_start` with platform-specific executable arguments.
- `host-hibernate-example.yml`
  - `os.process_start` with platform-specific executable arguments.
- `power-profile-example.yml`
  - `os.process_start` query for the platform power profile.
- `session-lock-example.yml`
  - `os.process_start` invocation of the platform session locker.
- `session-logoff-example.yml`
  - `os.process_start` invocation of the platform session logoff command.

## Pattern & Validation

```yaml
tasks:
  - name: process_ops
    sequence:
      - wait_event:
          event: "ops_dispatch_event"
          timeout: "30000"
      - parse_json:
          input_key: "event_payload"
          path: "$.action"
          output_key: "requested_action"
      - if: "{requested_action} == 'status'"
        then:
          - set_variable: "ops_status"
            value: "completed"
```

Validate workflow:
```powershell
praktor validate -f pistol\examples\ops-event-handler-example.yml
```

The reboot and shutdown examples call the native immediate power APIs. They can
change external system state and may require administrator/root privileges;
inspect the returned `success` field before treating the request as accepted.
