# Pistol Examples

This directory contains minimal wrapper workflows for the reusable templates in `../templates`.

The rule is simple:

- Pick the smallest template that matches the job.
- Pass explicit variables.
- Read the nested `result` object from `tasks.<alias>.outputs.result`.

## Service Operations

- `service-status-example.yml`
  - Uses `../templates/service-status.yml`
  - Query whether a service is running
- `service-start-example.yml`
  - Uses `../templates/service-start.yml`
  - Start a service and verify it is running
- `service-stop-example.yml`
  - Uses `../templates/service-stop.yml`
  - Stop a service and verify it is stopped
- `service-restart-example.yml`
  - Uses `../templates/service-restart.yml`
  - Restart a service and verify it returns to running

## Process Operations

- `process-status-example.yml`
  - Uses `../templates/process-status.yml`
  - Query whether a process exists by exact name or PID
- `process-stop-example.yml`
  - Uses `../templates/process-stop.yml`
  - Stop a process by exact name or PID

## Host Operations

- `host-shutdown-example.yml`
  - Uses `../templates/host-shutdown.yml`
  - Schedule a host shutdown with an explicit delay
- `host-reboot-example.yml`
  - Uses `../templates/host-reboot.yml`
  - Schedule a host reboot with an explicit delay
- `host-suspend-example.yml`
  - Uses `../templates/host-suspend.yml`
  - Request host suspend / sleep
- `host-hibernate-example.yml`
  - Uses `../templates/host-hibernate.yml`
  - Request host hibernation
- `power-profile-example.yml`
  - Uses `../templates/power-profile.yml`
  - Request a host power profile change
- `session-lock-example.yml`
  - Uses `../templates/session-lock.yml`
  - Lock the current interactive session
- `session-logoff-example.yml`
  - Uses `../templates/session-logoff.yml`
  - Log off the current interactive session

## Other Reusable Templates

These templates do not yet have wrapper examples in this directory, but they are ready to use directly:

- `../templates/file-download.yml`
- `../templates/file-upload.yml`
- `../templates/archive-create.yml`
- `../templates/archive-extract.yml`
- `../templates/host-shutdown.yml`
- `../templates/host-reboot.yml`
- `../templates/host-suspend.yml`
- `../templates/host-hibernate.yml`
- `../templates/power-profile.yml`
- `../templates/session-lock.yml`
- `../templates/session-logoff.yml`
- `../templates/service-check.yml`
- `../templates/monitor-host.yml`
- `../templates/monitor-processes.yml`
- `../templates/remote-cmd-receiver.yml`
- `../templates/ops-agent.yml`

## Minimal Pattern

```yaml
tasks:
  - name: service_status
    uses: ../templates/service-status.yml
    vars:
      SERVICE_NAME: "{{ SERVICE_NAME }}"

  - name: emit_result
    depends_on: [service_status]
    script: |
      ctx.output("result", ctx.get("tasks.service_status.outputs.result"));
```

## Validate

From the repository root:

```powershell
.\build\Msvc-ASan\bin\praktor.exe validate -f pistol\examples\service-status-example.yml
.\build\Msvc-ASan\bin\praktor.exe validate -f pistol\templates\service-status.yml
```

## Notes

- These examples are intentionally small. They exist to show `uses` wiring, not to hide behavior.
- Service templates prefer native service managers and only fall back when necessary.
- Process templates support either exact process name or PID.
- Host power templates use `DELAY_MINUTES` instead of fake cross-platform seconds semantics.
- Suspend and hibernate templates are best-effort host power requests; they only report whether the command was accepted.
- Power profile templates only expose three portable modes: `balanced`, `powersave`, `performance`.
- Session templates are desktop-session actions, not server lifecycle operations.
- File and archive templates are explicit operations. Do not replace them with generic `run_command` garbage.
