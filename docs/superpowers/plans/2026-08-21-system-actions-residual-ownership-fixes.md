# System Actions Residual Ownership Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the two remaining system-actions merge blockers by compensating timed-out process starts with stable identity and publishing immutable, single-assignment task failure snapshots.

**Architecture:** `IManagedProcessBackend::start()` returns the stable snapshot observed from the created process handle, allowing `ManagedProcessController` to own and safely compensate the exact instance when startup times out. `TaskRegistry` remains the sole terminal failure owner; nested failure trees use shared immutable storage and a terminal latch rejects overwrite attempts.

**Tech Stack:** C++20, Win32 process APIs, TurboParser `WorkflowValue`, Catch2, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-08-21-system-actions-residual-ownership-fixes-design.md`

## Global Constraints

- OS process state is authoritative; no result may report the pre-start `not_running/pid=0` snapshot after a successful spawn.
- A successful backend start returns PID, current session ID, creation-time instance token, and canonical image basename.
- Startup timeout compensates only the exact returned instance and confirms cleanup with a bounded deadline.
- Compensation failure supersedes the primary timeout because the resource state is not known to be safe.
- `TaskRegistry` owns one terminal failure snapshot per task; trigger context is only a scoped projection.
- Nested failure storage is immutable after publication.
- Existing YAML, workflow outputs, service execution, `program`, `download`, and TurboScript behavior remain unchanged.
- No new public WorkflowRunner executor injection API, no SCM API, and no privileged SCM test.
- Tests must demonstrate RED before production changes and must leave zero `managed_process_fixture` processes.

---

### Task 1: Stable start identity and timeout compensation

**Files:**
- Modify: `praktor/include/system/managed_process.hpp`
- Modify: `praktor/src/system/managed_process.cpp`
- Modify: `praktor/src/system/managed_process_win.cpp`
- Test: `praktor/test/managed_process_executor_test.cpp`

**Interfaces:**
- Changes: `IManagedProcessBackend::start(const ManagedProcessParams&)` returns `ManagedProcessResult<ManagedProcessSnapshot>` instead of `ManagedProcessResult<std::uint32_t>`.
- Consumes: existing `ManagedProcessSnapshot`, `queryProcessEvidence`, `terminate`, `query`, `startup_timeout_ms`, and `stop_timeout_ms`.
- Produces: timeout results whose snapshot reflects the latest OS fact and whose exact started instance has been compensated or explicitly reported as still running.

- [ ] **Step 1: Convert the fake backend contract in the tests**

Change the fake declarations before production headers so the focused target fails to compile:

```cpp
ManagedProcessResult<ManagedProcessSnapshot> start(
    const ManagedProcessParams& params) override;

std::function<ManagedProcessResult<ManagedProcessSnapshot>()> start_action;
ManagedProcessResult<ManagedProcessSnapshot> start_result = success(running());
```

Also add an action hook to the existing fake termination method so each test can model cleanup state explicitly:

```cpp
ManagedProcessCommandResult terminate(
    const ManagedProcessSnapshot& snapshot) override {
  terminated_snapshots.push_back(snapshot);
  if (terminate_action) {
    return terminate_action(snapshot);
  }
  return terminate_result;
}

std::function<ManagedProcessCommandResult(
    const ManagedProcessSnapshot&)> terminate_action;
```

Keep `running()` deterministic by populating stable evidence for compensation assertions:

```cpp
ManagedProcessSnapshot running(std::uint32_t pid = kFixturePid) {
  return {ManagedProcessState::Running, pid, 7, 9001,
          "managed_process_fixture.exe"};
}
```

- [ ] **Step 2: Run the focused build and verify RED**

Run:

```powershell
cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib
```

Expected: compilation fails because `IManagedProcessBackend::start()` still returns `ManagedProcessResult<std::uint32_t>`.

- [ ] **Step 3: Change the backend interface and Windows start result**

Update the interface exactly:

```cpp
virtual ManagedProcessResult<ManagedProcessSnapshot> start(
    const ManagedProcessParams& params) = 0;
```

In the Windows backend, keep the process handle alive through the existing non-blocking immediate-exit probe, then obtain the full snapshot from the handle:

```cpp
auto evidence = queryProcessEvidence(process.get(), process_info.dwProcessId);
if (!evidence.ok) {
  static_cast<void>(TerminateProcess(process.get(), kStartupEvidenceFailureExitCode));
  return failure<ManagedProcessSnapshot>(
      evidence.native_error, std::move(evidence.message));
}
if (evidence.value.state != ManagedProcessState::Running ||
    evidence.value.session_id != current_session) {
  static_cast<void>(TerminateProcess(process.get(), kSessionMismatchExitCode));
  return failure<ManagedProcessSnapshot>(
      ERROR_INVALID_DATA, "managed process started with invalid identity evidence");
}
return evidence;
```

Use existing named exit constants or add one named `constexpr DWORD`; do not add a magic exit code. This step changes only the return contract and evidence capture; it does not add timeout compensation.

- [ ] **Step 4: Run the focused build and verify the interface migration is GREEN**

Run:

```powershell
cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib
ctest --preset win-dev-user -R "managed_process_executor" --output-on-failure
```

Expected: the focused target and existing managed-process tests pass with the full-snapshot start contract.

- [ ] **Step 5: Add failing compensation behavior tests**

Add three fake-backend tests. The success case must assert exact snapshot identity, not only call count:

```cpp
TEST_CASE("startup timeout compensates the exact started instance") {
  FakeManagedProcessBackend backend;
  const ManagedProcessSnapshot started = running(5151);
  bool spawned = false;
  bool terminated = false;
  backend.start_action = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    spawned = true;
    return success(started);
  };
  backend.query_action = [&] {
    return success(spawned && !terminated ? started : notRunning());
  };
  backend.terminate_action = [&](const ManagedProcessSnapshot& snapshot) {
    CHECK(snapshot.pid == started.pid);
    CHECK(snapshot.instance_token == started.instance_token);
    CHECK(snapshot.canonical_image_name == started.canonical_image_name);
    terminated = true;
    return commandSuccess();
  };

  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;
  request.stop_timeout_ms = 25;
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));
  const auto result = controller.execute(request);

  CHECK_FALSE(result.ok);
  CHECK(result.error == ManagedProcessError::Timeout);
  CHECK(result.phase == "start_poll");
  CHECK(result.snapshot.state == ManagedProcessState::NotRunning);
  REQUIRE(backend.terminated_snapshots.size() == 1);
}
```

Add variants with these literal expectations:

```cpp
// terminate returns {false, 5, "cleanup denied"}
CHECK(result.error == ManagedProcessError::BackendFailure);
CHECK(result.phase == "start_timeout_terminate");
CHECK(result.snapshot.pid == started.pid);

// terminate succeeds but every subsequent query returns started
CHECK(result.error == ManagedProcessError::Timeout);
CHECK(result.phase == "start_timeout_cleanup_poll");
CHECK(result.snapshot.pid == started.pid);
```

- [ ] **Step 6: Run focused tests and verify behavioral RED**

Run:

```powershell
cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib
build\Msvc\bin\managed_process_executor_test.praktor_lib.exe "startup timeout*"
```

Expected: all three new compensation cases fail because the controller returns before reconciling or terminating the started instance.

- [ ] **Step 7: Implement one startup compensation path in the controller**

After successful start, assign `execution.snapshot = result.value`. Replace the current immediate timeout return with a helper whose contract is:

```cpp
auto compensateTimedOutStart = [&]()
    -> std::optional<ManagedProcessExecutionResult> {
  const ManagedProcessSnapshot started = execution.snapshot;

  auto observed = backend_.query(params.identity);
  if (!observed.ok) {
    return fail(backendError(observed.message), "start_timeout_query",
                std::move(observed.message), observed.native_error);
  }
  execution.snapshot = observed.value;
  if (execution.snapshot.state == ManagedProcessState::NotRunning) {
    return fail(ManagedProcessError::Timeout, "start_poll",
                "managed process startup exceeded the deadline");
  }

  auto terminated = backend_.terminate(started);
  if (!terminated.ok) {
    return fail(backendError(terminated.message), "start_timeout_terminate",
                std::move(terminated.message), terminated.native_error);
  }

  if (auto cleanup_failure = pollFor(
          ManagedProcessState::NotRunning,
          Clock::now() + Milliseconds(params.stop_timeout_ms),
          "start_timeout_cleanup_poll")) {
    return cleanup_failure;
  }
  return fail(ManagedProcessError::Timeout, "start_poll",
              "managed process startup exceeded the deadline");
};
```

The helper must never terminate `observed.value` merely because its image name matches. It terminates only the stable snapshot returned by this invocation of `start()`; the backend performs action-time identity revalidation.

Make `pollFor()` support a timeout callback or split startup polling so deadline expiration routes through this helper exactly once. Do not recursively call `pollFor()` with startup compensation enabled.

- [ ] **Step 8: Tighten the real Windows timeout test**

Replace the test-side best-effort implication with terminal ownership assertions:

```cpp
CHECK_FALSE(started.ok);
CHECK(started.error == ManagedProcessError::Timeout);
CHECK(started.phase == "start_poll");
CHECK(started.snapshot.state == ManagedProcessState::NotRunning);

const auto after = backend->query(request.identity);
REQUIRE(after.ok);
CHECK(after.value.state == ManagedProcessState::NotRunning);
```

The cleanup guard remains as an assertion-failure fallback, but the production result must already prove cleanup.

- [ ] **Step 9: Run focused and affected regression tests**

Run:

```powershell
cmake --build --preset win-dev-user --target managed_process_executor_test.praktor_lib
ctest --preset win-dev-user -R "managed_process_executor|workflow_runner" --output-on-failure
```

Expected: all selected tests pass and no fixture process remains.

- [ ] **Step 10: Commit the managed-process ownership fix**

```powershell
git add praktor/include/system/managed_process.hpp praktor/src/system/managed_process.cpp praktor/src/system/managed_process_win.cpp praktor/test/managed_process_executor_test.cpp
git commit -m "fix: compensate timed out managed process starts"
```

### Task 2: Immutable single-assignment failure snapshots

**Files:**
- Modify: `praktor/include/dag/task_failure_context.hpp`
- Modify: `praktor/include/dag/task_registry.hpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Test: `praktor/test/task_registry_test.cpp`
- Test: `praktor/test/workflow_runner_test.cpp`

**Interfaces:**
- Changes: `TaskFailureContext::inner_failure` becomes `std::shared_ptr<const TaskFailureContext>`.
- Preserves: `TaskRegistry::getFailureSnapshot()` returns `std::optional<TaskFailureContext>` by value; existing legacy failure fields and nested `uses` variables remain unchanged.
- Enforces: `TaskRegistry::markFailed()` rejects overwrite of a task already in `Completed`, `Failed`, or `Skipped` state.

- [ ] **Step 1: Add compile-time immutability and terminal-latch tests**

At the top of `task_registry_test.cpp`, add:

```cpp
#include <type_traits>

using InnerFailureElement =
    typename decltype(TaskFailureContext{}.inner_failure)::element_type;
static_assert(std::is_const_v<InnerFailureElement>);
```

Add these behavior tests:

```cpp
TEST_CASE("TaskRegistry failure snapshot is assigned once") {
  TaskRegistry registry;
  registry.startTask("task_a");

  TaskFailureContext first;
  first.task_name = "task_a";
  first.error_code = "timeout";
  first.error_message = "first";
  registry.markFailed("task_a", first);

  TaskFailureContext second;
  second.task_name = "task_a";
  second.error_code = "service_state_failed";
  second.error_message = "second";
  CHECK_THROWS_AS(registry.markFailed("task_a", second), std::logic_error);

  const auto stored = registry.getFailureSnapshot("task_a");
  REQUIRE(stored.has_value());
  CHECK(stored->error_code == "timeout");
  CHECK(stored->error_message == "first");
}
```

Add sections proving `markFailed()` after `markCompleted()` and after
`setStatus("task_a", "skipped")` throws while preserving the original terminal state.

- [ ] **Step 2: Run the focused build and verify RED**

Run:

```powershell
cmake --build --preset win-dev-user --target task_registry_test.praktor_lib
```

Expected: compilation fails at the static assertion because `inner_failure` points to mutable `TaskFailureContext`.

- [ ] **Step 3: Make nested failure storage const**

Change the field:

```cpp
std::shared_ptr<const TaskFailureContext> inner_failure;
```

Change construction in `buildFailureContext()`:

```cpp
failure.inner_failure =
    std::make_shared<const TaskFailureContext>(nested);
```

No `const_cast`, mutable alias, or second mutable nested copy is allowed.

- [ ] **Step 4: Add the terminal latch to `markFailed()`**

Under the registry mutex, reject terminal states before changing either map:

```cpp
const TaskState current = getStateInternal(task_name);
if (current == TaskState::Completed || current == TaskState::Failed ||
    current == TaskState::Skipped) {
  throw std::logic_error(
      "Task '" + task_name + "' is already finalized as " +
      taskStateToString(current));
}
task_state_[task_name] = TaskState::Failed;
failure_snapshots_.emplace(task_name, std::move(failure));
```

Use `emplace`/`try_emplace`; do not use `operator[]` to overwrite the stored snapshot. Re-entry remains supported through the existing `startTask()`, which clears terminal state and the previous snapshot before a new execution generation begins.

- [ ] **Step 5: Run registry and nested workflow tests**

Run:

```powershell
cmake --build --preset win-dev-user --target task_registry_test.praktor_lib workflow_runner_test.praktor_lib
ctest --preset win-dev-user -R "task_registry|workflow_runner" --output-on-failure
```

Expected: terminal-latch tests pass; existing task re-entry passes; nested `uses` continues to expose legacy and structured fields.

- [ ] **Step 6: Run the complete verification gate**

Set the existing runtime PATH and run:

```powershell
cmake --build --preset win-dev-user
ctest --preset win-dev-user --output-on-failure
git diff --check
```

Then verify:

```powershell
@(Get-Process -Name managed_process_fixture -ErrorAction SilentlyContinue).Count
```

Expected: build succeeds, all tests pass, diff check is clean, and fixture count is `0`.

- [ ] **Step 7: Commit immutable terminal snapshots**

```powershell
git add praktor/include/dag/task_failure_context.hpp praktor/include/dag/task_registry.hpp praktor/src/dag/workflow_executor.cpp praktor/test/task_registry_test.cpp praktor/test/workflow_runner_test.cpp
git commit -m "fix: freeze terminal task failure snapshots"
```
