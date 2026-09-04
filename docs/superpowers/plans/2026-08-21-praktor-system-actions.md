# Praktor System Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add typed, idempotent `service` and `managed_process` workflow actions while keeping service control process-backed.

**Architecture:** YAML is parsed into typed request structs. Thin executors delegate to a process-backed service controller and a platform managed-process backend; operating-system state remains authoritative and every transition is confirmed by querying that state.

**Tech Stack:** C++20, SaltsUtils YAML, Salts, Praktor Shell `ProcessExecutor`, Catch2, Windows process APIs.

**Spec:** `docs/superpowers/specs/2026-08-21-praktor-system-actions-streaming-archive-design.md`

## Global Constraints

- `service` must invoke command profiles through `ProcessExecutor`; it must not call Windows SCM APIs.
- Command arguments are argv entries and must never be concatenated into a shell command.
- Supported operations are exactly `status`, `start`, `stop`, and `restart`.
- Default service timeout is 30000 ms and default polling interval is 200 ms.
- `program` remains blocking; `managed_process` owns long-lived process lifecycle semantics.
- Existing `program`, `download`, and TurboScript `os.service_*` behavior remains unchanged.
- The old Retro helper is retained until the third plan completes its regression gate.
- Invalid schema, unknown profiles, illegal state transitions, timeout, and unsupported platforms fail explicitly.

---

### Task 1: Structured task failure metadata

**Files:**
- Create: `praktor/include/dag/task_error.hpp`
- Modify: `praktor/include/dag/task_executor.hpp`
- Modify: `praktor/include/dag/task_failure_context.hpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Test: `praktor/test/core_improvements_test.cpp`

**Interfaces:**
- Produces: `Praktor::Execution::TaskErrorCode`, `taskErrorCodeName(TaskErrorCode)`, and `TaskResult::fail(TaskErrorCode, std::string, std::string, WorkflowValue)`.
- Preserves: `TaskResult.success`, `error_message`, `exit_code`, stdout/stderr, and existing failure-context fields.

- [ ] **Step 1: Write failing tests for structured failures**

```cpp
TEST_CASE("TaskResult failure keeps legacy message and structured metadata") {
  auto details = WorkflowValue::object();
  details["state"] = "start_pending";
  auto result = TaskResult::fail(
      Praktor::Execution::TaskErrorCode::Timeout,
      "poll", "service did not reach running",
      std::move(details));
  CHECK_FALSE(result.success);
  CHECK(result.error_message == "service did not reach running");
  CHECK(result.error_code == "timeout");
  CHECK(result.error_phase == "poll");
  CHECK(result.error_details["state"].as<std::string>() == "start_pending");
}
```

- [ ] **Step 2: Run the focused test and confirm it fails to compile**

Run: `cmake --build --preset win-dev-user --target core_improvements_test.praktor_lib`

Expected: failure because `TaskResult::fail` and structured fields do not exist.

- [ ] **Step 3: Add the stable error enum and compatible fields**

```cpp
enum class TaskErrorCode {
  None, SchemaInvalid, ResourceNotFound, ResourceTypeMismatch,
  ResourceAlreadyConsumed, ResourceLimitExceeded, TransportFailed,
  HttpStatusFailed, ArchiveFormatInvalid, ArchivePolicyRejected,
  FilesystemFailed, ProcessSpawnFailed, ServiceStateFailed,
  Timeout, Cancelled, UnsupportedPlatform
};

struct TaskResult {
  // existing fields remain first and retain their meaning
  std::string error_code;
  std::string error_phase;
  WorkflowValue error_details{WorkflowValue::object()};
  static TaskResult fail(TaskErrorCode code, std::string phase,
                         std::string message,
                         WorkflowValue details = WorkflowValue::object());
};
```

Propagate these fields into `TaskFailureContext` and `buildFailureContext`; do not remove the legacy message.

- [ ] **Step 4: Run the focused and full Praktor tests**

Run: `cmake --build --preset win-dev-user --target core_improvements_test.praktor_lib`

Run: `ctest --preset win-dev-user --output-on-failure`

Expected: all tests pass and old failure consumers still receive `error_message`.

- [ ] **Step 5: Commit the error contract**

```powershell
git add praktor/include/dag/task_error.hpp praktor/include/dag/task_executor.hpp praktor/include/dag/task_failure_context.hpp praktor/src/dag/workflow_executor.cpp praktor/test/core_improvements_test.cpp
git commit -m "feat: add structured task failure metadata"
```

### Task 2: Parse typed system actions

**Files:**
- Modify: `praktor/include/yml/task_types.hpp`
- Modify: `praktor/src/yml/task_yaml_internal.hpp`
- Modify: `praktor/src/yml/task_field_parser.cpp`
- Modify: `praktor/src/yml/task_yaml.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Test: `praktor/test/task_parser_test.cpp`

**Interfaces:**
- Produces: `SystemOperation`, `ServiceParams`, `ManagedProcessIdentity`, `ManagedProcessParams`.
- Extends: `TaskAction` with `Service` and `ManagedProcess`; extends `TaskSpecifics` with both request types.

- [ ] **Step 1: Add parser tests for valid minimal actions**

```cpp
TEST_CASE("parse service and managed_process tasks") {
  auto path = writeTempWorkflow("system_actions.yml", R"(
tasks:
  - name: start_camera
    service:
      operation: start
      name: RetroCamera
      profile: windows_scm
      arguments: [--verbose]
      timeout_ms: 30000
      poll_interval_ms: 200
  - name: start_saver
    managed_process:
      operation: start
      executable: RetroScreenSaver.exe
      arguments: [--fullscreen]
      identity:
        image_name: RetroScreenSaver.exe
      startup_timeout_ms: 5000
      stop_timeout_ms: 5000
      force_terminate: true
)");
  auto workflow = TaskParser::parseFile(path.string());
  CHECK(workflow.tasks[0].action == TaskAction::Service);
  CHECK(std::get<ServiceParams>(workflow.tasks[0].specifics).name == "RetroCamera");
  CHECK(workflow.tasks[1].action == TaskAction::ManagedProcess);
}
```

- [ ] **Step 2: Add rejection tests**

Cover unknown operation/profile keys, empty service/process names, missing executable on start, zero/negative timeouts, missing identity, and multiple runner declarations. Require the source line in every parse error.

- [ ] **Step 3: Run parser tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target task_parser_test.praktor_lib`

Expected: compile or assertion failure because the action types and parsers do not exist.

- [ ] **Step 4: Implement strict parsers**

```cpp
enum class SystemOperation { Status, Start, Stop, Restart };

struct ServiceParams {
  SystemOperation operation{SystemOperation::Status};
  std::string name;
  std::string profile{"windows_scm"};
  StrList arguments;
  int timeout_ms{30000};
  int poll_interval_ms{200};
};

struct ManagedProcessIdentity { std::string image_name; };

struct ManagedProcessParams {
  SystemOperation operation{SystemOperation::Status};
  std::string executable;
  StrList arguments;
  std::string working_directory;
  ManagedProcessIdentity identity;
  int startup_timeout_ms{5000};
  int stop_timeout_ms{5000};
  bool force_terminate{false};
};
```

Use `check_unknown_keys` for both maps. Update the runner-required diagnostic to list the new runner names.

- [ ] **Step 5: Include every new field in `computeTaskActionHash`**

Add deterministic serialization of operation, strings, lists, booleans, and timeouts so cache hits cannot hide a changed system action.

- [ ] **Step 6: Run parser and schema regression tests**

Run: `cmake --build --preset win-dev-user --target task_parser_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "task_parser|schema_validation" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 7: Commit typed parsing**

```powershell
git add praktor/include/yml/task_types.hpp praktor/src/yml/task_yaml_internal.hpp praktor/src/yml/task_field_parser.cpp praktor/src/yml/task_yaml.cpp praktor/src/dag/workflow_executor.cpp praktor/test/task_parser_test.cpp
git commit -m "feat: parse typed system actions"
```

### Task 3: Process-backed service controller

**Files:**
- Create: `praktor/include/system/service_controller.hpp`
- Create: `praktor/src/system/service_controller.cpp`
- Create: `praktor/include/executors/service_executor.hpp`
- Create: `praktor/src/executors/service_executor.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/service_executor_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ServiceParams`, `ProcessSpec`, `ProcessResult`, `TaskResult::fail`.
- Produces: `IProcessRunner::run(const ProcessSpec&)`, `ServiceCommandProfile`, `ServiceProfileRegistry`, `ServiceController::execute(const ServiceParams&)`.

- [ ] **Step 1: Write fake-runner tests for status and idempotency**

Test numeric `sc.exe` states `1` stopped, `2` start-pending, `3` stop-pending, `4` running. Verify start on state `4` and stop on state `1` issue only a query and return `changed=false`.

- [ ] **Step 2: Write transition tests**

Use a scripted fake runner returning `1 -> 2 -> 4` for start and `4 -> 3 -> 1` for stop. Verify argv exactly equals `{"query", name}`, `{"start", name, ...arguments}`, or `{"stop", name, ...arguments}` and never invokes a shell.

- [ ] **Step 3: Write timeout and malformed-output tests**

Assert `timeout`, `service_state_failed`, and `process_spawn_failed` are distinct. Unknown profile and status output without a numeric `STATE` field must fail rather than report stopped.

- [ ] **Step 4: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target service_executor_test.praktor_lib`

Expected: target/source failures because the controller is not implemented.

- [ ] **Step 5: Implement the injected process boundary and built-in profile**

```cpp
class IProcessRunner {
public:
  virtual ~IProcessRunner() = default;
  virtual Praktor::Shell::ProcessResult run(
      const Praktor::Shell::ProcessSpec& spec) = 0;
};

struct ServiceCommandProfile {
  std::string name;
  std::string program;
  StrList status_args{"query", "{service_name}"};
  StrList start_args{"start", "{service_name}", "{arguments}"};
  StrList stop_args{"stop", "{service_name}", "{arguments}"};
};
```

`{arguments}` is the only list-splice token; every other token renders one argv entry. Register `windows_scm` with program `sc.exe`. Tests inject additional profiles through `ServiceProfileRegistry::registerProfile` before execution starts.

- [ ] **Step 6: Implement bounded polling and outputs**

Use `steady_clock`. Query before mutation, request once, then poll at `poll_interval_ms` until the target state or deadline. Emit `name`, `operation`, `state`, `changed`, and `duration_ms` via `setCurrentTaskOutput`.

- [ ] **Step 7: Register the executor and run tests**

Run: `cmake --build --preset win-dev-user --target service_executor_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "service_executor|task_parser" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 8: Commit the service action**

```powershell
git add praktor/include/system/service_controller.hpp praktor/src/system/service_controller.cpp praktor/include/executors/service_executor.hpp praktor/src/executors/service_executor.cpp praktor/src/dag/workflow_executor.cpp praktor/CMakeLists.txt praktor/test/service_executor_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add process-backed service action"
```

### Task 4: Managed process backend and executor

**Files:**
- Create: `praktor/include/system/managed_process.hpp`
- Create: `praktor/src/system/managed_process.cpp`
- Create: `praktor/src/system/managed_process_win.cpp`
- Create: `praktor/include/executors/managed_process_executor.hpp`
- Create: `praktor/src/executors/managed_process_executor.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/managed_process_executor_test.cpp`
- Create: `praktor/test/helpers/managed_process_fixture.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ManagedProcessParams`, `TaskResult::fail`.
- Produces: `ManagedProcessSnapshot {state, pid}`, `IManagedProcessBackend::query/start/requestStop/terminate`, `ManagedProcessController::execute`.

- [ ] **Step 1: Write controller tests against a fake backend**

Cover status, idempotent start/stop, `not_running -> running`, graceful stop, restart, startup timeout, stop timeout, and force-termination disabled/enabled.

- [ ] **Step 2: Run the focused target and confirm failure**

Run: `cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib`

Expected: missing interfaces and target sources.

- [ ] **Step 3: Implement the controller state transitions**

```cpp
enum class ManagedProcessState { NotRunning, Running };
struct ManagedProcessSnapshot { ManagedProcessState state; std::uint32_t pid; };

class IManagedProcessBackend {
public:
  virtual ~IManagedProcessBackend() = default;
  virtual ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity&) = 0;
  virtual ManagedProcessResult<std::uint32_t> start(
      const ManagedProcessParams&) = 0;
  virtual ManagedProcessCommandResult requestStop(
      const ManagedProcessSnapshot&) = 0;
  virtual ManagedProcessCommandResult terminate(
      const ManagedProcessSnapshot&) = 0;
};
```

Define `ManagedProcessResult<T>` in `managed_process.hpp` as `{bool ok; T value; int native_error; std::string message;}` and `ManagedProcessCommandResult` as `{bool ok; int native_error; std::string message;}`. The platform backend sets `native_error`; only the executor converts it into `TaskResult` and logs it.

- [ ] **Step 4: Port the Windows behavior behind the backend**

Move only the proven identity lookup, current-session launch, `WM_CLOSE` request, state re-query, and explicit termination behavior from Retro's `process_control_main.cpp`. Do not copy CLI parsing, logging, or JSON formatting. Non-Windows construction returns `unsupported_platform`.

- [ ] **Step 5: Add the real fixture integration test**

Build `managed_process_fixture` as a small windowed/console process that remains alive until close. Verify start → query same identity → stop, and assert no fixture process remains after the test cleanup guard.

- [ ] **Step 6: Register the executor and run regression tests**

Run: `cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "managed_process_executor|service_executor|task_parser" --output-on-failure`

Expected: all selected tests pass; cleanup succeeds even after an assertion failure.

- [ ] **Step 7: Commit managed process support**

```powershell
git add praktor/include/system/managed_process.hpp praktor/src/system/managed_process.cpp praktor/src/system/managed_process_win.cpp praktor/include/executors/managed_process_executor.hpp praktor/src/executors/managed_process_executor.cpp praktor/src/dag/workflow_executor.cpp praktor/CMakeLists.txt praktor/test/managed_process_executor_test.cpp praktor/test/helpers/managed_process_fixture.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add managed process action"
```

### Task 5: System-action end-to-end contract

**Files:**
- Create: `praktor/test/workflows/system-actions.yml`
- Modify: `praktor/test/workflow_runner_test.cpp`
- Modify: `praktor/test/schema_validation_test.py`
- Modify: `README.md`

**Interfaces:**
- Consumes: registered `service` and `managed_process` executors.
- Produces: documented YAML and stable workflow output fields.

- [ ] **Step 1: Add an end-to-end workflow test using injected/fake platform boundaries**

Assert task statuses, outputs, structured failure fields, triggers, and cache invalidation when any action parameter changes.

- [ ] **Step 2: Add schema-validation corpus entries**

Add one valid and one invalid YAML sample for each action. The invalid samples must fail before process or OS access.

- [ ] **Step 3: Document the exact YAML and output contract**

Include the four operations, idempotent `changed` meaning, timeout semantics, `windows_scm` numeric-state parsing, `force_terminate`, and the distinction from `program`.

- [ ] **Step 4: Run the full verification gate**

Run: `cmake --build --preset win-dev-user`

Run: `ctest --preset win-dev-user --output-on-failure`

Run: `git diff --check`

Expected: build succeeds, all tests pass, and diff check is clean.

- [ ] **Step 5: Commit documentation and end-to-end tests**

```powershell
git add praktor/test/workflows/system-actions.yml praktor/test/workflow_runner_test.cpp praktor/test/schema_validation_test.py README.md
git commit -m "test: verify system action workflows"
```
