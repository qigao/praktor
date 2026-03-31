# Pistol

`pistol/` is the reusable workflow and API bundle for Praktor.

Use it for three things:

- call workflows through the C API in `api/`
- reuse task templates from `templates/`
- learn the wiring pattern from `examples/`

## Layout

- `api/`
  - C-facing wrapper around the core workflow runner
- `templates/`
  - reusable workflows grouped by operation type
- `examples/`
  - minimal `uses` wrappers that show how to call templates
- `hello-world.yml`
  - smallest runnable workflow in this directory

## Template Groups

### Service

- `templates/service-status.yml`
- `templates/service-start.yml`
- `templates/service-stop.yml`
- `templates/service-restart.yml`
- `templates/service-check.yml`

### Process

- `templates/process-status.yml`
- `templates/process-stop.yml`

### File And Archive

- `templates/file-download.yml`
- `templates/file-upload.yml`
- `templates/archive-create.yml`
- `templates/archive-extract.yml`

### Host And Session

- `templates/host-shutdown.yml`
- `templates/host-reboot.yml`
- `templates/host-suspend.yml`
- `templates/host-hibernate.yml`
- `templates/power-profile.yml`
- `templates/session-lock.yml`
- `templates/session-logoff.yml`

### Monitoring And Ops

- `templates/monitor-host.yml`
- `templates/monitor-processes.yml`
- `templates/remote-cmd-receiver.yml`
- `templates/ops-agent.yml`
- `templates/ops-loop.ps1`
- `templates/ops-loop.sh`

## Examples

Start with `examples/README.md`.

Available example wrappers:

- `examples/service-status-example.yml`
- `examples/service-start-example.yml`
- `examples/service-stop-example.yml`
- `examples/service-restart-example.yml`
- `examples/process-status-example.yml`
- `examples/process-stop-example.yml`
- `examples/host-shutdown-example.yml`
- `examples/host-reboot-example.yml`
- `examples/host-suspend-example.yml`
- `examples/host-hibernate-example.yml`
- `examples/power-profile-example.yml`
- `examples/session-lock-example.yml`
- `examples/session-logoff-example.yml`

## Minimal Use Pattern

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
.\build\Msvc-ASan\bin\praktor.exe validate -f pistol\templates\service-status.yml
.\build\Msvc-ASan\bin\praktor.exe validate -f pistol\examples\service-status-example.yml
```

## Safety Notes

- Shutdown, reboot, suspend, hibernate, lock, and logoff templates affect the current machine or session.
- Power templates report accepted intent, not guaranteed final state.
- File and archive templates are explicit operations. Do not replace them with generic `run_command`.
- If a job only needs one narrow action, use one narrow template. Do not build a fake universal template.
