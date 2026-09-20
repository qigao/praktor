# Praktor SDK

`sdk/` contains Praktor's stable embedding API plus runnable workflow examples. It is part of Praktor, not a separate product.

Use it for:

- call workflows through the C API in `api/`
- learn and run BT-native operational workflows from `examples/`

## Layout

- `api/`
  - C-facing wrapper around the core workflow runner
- `examples/`
  - standalone, BT-native operational workflows (service, process, host, event handler)
- `hello-world.yml`
  - smallest runnable workflow in this directory

## Example Workflows

### Service & Process

- `examples/service-status-example.yml`
- `examples/service-start-example.yml`
- `examples/service-stop-example.yml`
- `examples/service-restart-example.yml`
- `examples/process-status-example.yml`
- `examples/process-stop-example.yml`

### Reactive Event Handler

- `examples/ops-event-handler-example.yml`

### Host & Session

- `examples/host-shutdown-example.yml`
- `examples/host-reboot-example.yml`
- `examples/host-suspend-example.yml`
- `examples/host-hibernate-example.yml`
- `examples/power-profile-example.yml`
- `examples/session-lock-example.yml`
- `examples/session-logoff-example.yml`

## Validate

From the repository root:

```powershell
.\build\Msvc-ASan\bin\praktor.exe validate -f sdk\examples\service-status-example.yml
.\build\Msvc-ASan\bin\praktor.exe validate -f sdk\examples\ops-event-handler-example.yml
```
