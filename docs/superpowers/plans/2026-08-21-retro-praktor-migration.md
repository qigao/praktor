# Retro Praktor Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate Retro service, managed-process, and package workflows to the new Praktor actions, then remove duplicated process-control and ZIP extraction code.

**Architecture:** Trident remains the MQTT-to-workflow adapter, Praktor owns task/resource execution, the operating system owns process/service truth, and Package Installer remains the only owner of target overlay and marker state. Package bytes stream into a Praktor staging directory before installer `apply-directory` begins.

**Tech Stack:** Retro C++20, Praktor YAML, TurboScript workflow scripts, Package Installer, TinyTest-style Retro tests, CMake/CTest.

**Spec:** `C:/projects/cpp/Praktor/docs/superpowers/specs/2026-08-21-praktor-system-actions-streaming-archive-design.md`

## Global Constraints

- Execute this plan only after both Praktor plans pass their complete verification gates and the new Praktor runtime is available to Retro.
- Trident maps MQTT commands to workflows; it must not become owner of service/process/install state.
- Package Installer owns pending/success/failed markers and target overlay.
- Extraction completes in staging before any managed process/service is stopped.
- HTTP/archive failure must leave target files and package marker untouched.
- Existing overlay remains non-atomic; do not describe workflow rollback as file rollback.
- Delete `trident_process_control` and installer ZIP support only after `rg.exe` finds no remaining callers and regression tests pass.

---

### Task 1: Add Package Installer `apply-directory`

**Files:**
- Modify: `Retro/package_installer/src/package_installer.cpp`
- Modify: `Retro/package_installer/CMakeLists.txt`
- Modify: `Retro/package_installer/tests/package_installer_tests.cpp`
- Modify: `Retro/package_installer/tests/CMakeLists.txt`

**Interfaces:**
- Produces: CLI command `apply-directory --source-directory <path>` using the same overlay and marker implementation as existing `apply --archive-path`.
- Preserves: package ID/version/dispatch/resource/service/marker arguments and marker JSON semantics.

- [ ] **Step 1: Write failing CLI/domain tests**

Create a source directory with nested files, apply it to a temporary target, and verify bytes plus pending→success marker fields. Add missing source, source outside expected type, overlay failure, and marker-write failure cases.

- [ ] **Step 2: Run the focused test and confirm failure**

Run: `cmake --build --preset win-dev-user --target package_installer_tests`

Expected: failure because `apply-directory` is not recognized.

- [ ] **Step 3: Split extraction from overlay without duplicating overlay logic**

```cpp
InstallResult ApplyPackageDirectoryToRoot(
    const std::filesystem::path& source_directory,
    const std::filesystem::path& target_root,
    const PackageMetadata& metadata,
    const std::filesystem::path& marker_path);
```

Existing archive apply may call extraction followed by this function during migration. `apply-directory` calls it directly. Validate source and target before marker mutation.

- [ ] **Step 4: Verify marker ownership and partial-write reporting**

On overlay failure, preserve the failed marker and include the relative file/phase in the error. Do not automatically restore overwritten files or report atomicity.

- [ ] **Step 5: Run Package Installer tests**

Run: `cmake --build --preset win-dev-user --target package_installer_tests retro_package_installer`

Run: `ctest --preset win-dev-user -R package_installer --output-on-failure`

Expected: old archive and new directory paths both pass.

- [ ] **Step 6: Commit directory apply**

```powershell
git add Retro/package_installer/src/package_installer.cpp Retro/package_installer/CMakeLists.txt Retro/package_installer/tests/package_installer_tests.cpp Retro/package_installer/tests/CMakeLists.txt
git commit -m "feat(retro): apply packages from staging directories"
```

### Task 2: Migrate service workflows

**Files:**
- Modify: `Retro/trident/tasks/service-start.yml`
- Modify: `Retro/trident/tasks/service-stop.yml`
- Modify: `Retro/trident/tasks/service-status.yml`
- Modify: `Retro/trident/tasks/service-restart.yml`
- Modify: `Retro/trident/tests/trident_core_tests.cpp`

**Interfaces:**
- Consumes: Praktor `service` action and output fields `name`, `operation`, `state`, `changed`, `duration_ms`.
- Removes from templates: fixed settle delay and manual `sc.exe` status parsing.

- [ ] **Step 1: Update tests to require native workflow actions**

Assert each file contains exactly one `service:` runner with the correct operation and does not contain `program:`, `sc.exe`, `sleep:`, `SERVICE_SETTLE_DELAY`, or `": 4 "`/`": 1 "` parsing.

- [ ] **Step 2: Run the focused Trident tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target trident_core_tests`

Run: `ctest --preset win-dev-user -R trident_core --output-on-failure`

Expected: assertions fail against current templates.

- [ ] **Step 3: Replace each workflow body**

```yaml
tasks:
  - name: change_service_state
    service:
      operation: start
      name: "{{ SERVICE_NAME }}"
      profile: windows_scm
      arguments: []
      timeout_ms: 30000
      poll_interval_ms: 200
```

Use the matching operation per file. Emit result from `tasks.change_service_state.outputs` without reinterpreting the state.

- [ ] **Step 4: Run workflow and Trident tests**

Run: `cmake --build --preset win-dev-user --target trident_core_tests`

Run: `ctest --preset win-dev-user -R trident_core --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 5: Commit service migration**

```powershell
git add Retro/trident/tasks/service-start.yml Retro/trident/tasks/service-stop.yml Retro/trident/tasks/service-status.yml Retro/trident/tasks/service-restart.yml Retro/trident/tests/trident_core_tests.cpp
git commit -m "refactor(retro): use Praktor service actions"
```

### Task 3: Migrate managed application workflows

**Files:**
- Modify: `Retro/trident/tasks/managed-app-start.yml`
- Modify: `Retro/trident/tasks/managed-app-stop.yml`
- Modify: `Retro/trident/tasks/managed-app-status.yml`
- Modify: `Retro/trident/tasks/managed-app-restart.yml`
- Modify: `Retro/trident/tasks/process-start.yml`
- Modify: `Retro/trident/tasks/process-stop.yml`
- Modify: `Retro/trident/tests/trident_core_tests.cpp`

**Interfaces:**
- Consumes: Praktor `managed_process` action.
- Preserves: executable path, app root, process arguments, process identity, and maximized-start behavior through explicit action fields/arguments.

- [ ] **Step 1: Add migration assertions**

Require `managed_process:` and reject `PROCESS_CONTROL_PATH`, `trident_process_control`, and helper-specific CLI flags in all six templates.

- [ ] **Step 2: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target trident_core_tests`

Expected: current helper references violate the new assertions.

- [ ] **Step 3: Replace helper calls with typed actions**

```yaml
- name: start_process
  managed_process:
    operation: start
    executable: "{{ EXECUTABLE_PATH }}"
    arguments: ["{{ PROCESS_ARGUMENTS }}"]
    working_directory: "{{ APP_ROOT }}"
    identity:
      image_name: "{{ PROCESS_NAME }}"
    startup_timeout_ms: 5000
    stop_timeout_ms: 5000
    force_terminate: true
```

If `PROCESS_ARGUMENTS` is currently a command-line string, normalize it into an argv list at the Trident input adapter before workflow execution; do not split quoted command lines inside YAML.

- [ ] **Step 4: Verify start/status/stop/restart against real fixture apps**

Use the Praktor managed-process integration fixture or a Retro test executable. Assert the identity remains the same process, repeated start is idempotent, and test cleanup leaves no process.

- [ ] **Step 5: Run Trident regressions**

Run: `cmake --build --preset win-dev-user --target trident_core_tests`

Run: `ctest --preset win-dev-user -R "trident_core|managed_process" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 6: Commit managed process migration**

```powershell
git add Retro/trident/tasks/managed-app-start.yml Retro/trident/tasks/managed-app-stop.yml Retro/trident/tasks/managed-app-status.yml Retro/trident/tasks/managed-app-restart.yml Retro/trident/tasks/process-start.yml Retro/trident/tasks/process-stop.yml Retro/trident/tests/trident_core_tests.cpp
git commit -m "refactor(retro): use Praktor managed processes"
```

### Task 4: Stream package download into Praktor staging

**Files:**
- Modify: `Retro/trident/tasks/package-sync-apply.yml`
- Modify: `Retro/trident/tasks/package-sync-upgrade.yml`
- Modify: `Retro/trident/src/trident_station_command.cpp`
- Modify: `Retro/trident/src/praktor_runner.cpp`
- Modify: `Retro/trident/tests/trident_core_tests.cpp`

**Interfaces:**
- Consumes: `stream.open_http_source`, `archive.extract`, Package Installer `apply-directory`.
- Preserves: download URL, SHA-256 policy if provided, lifecycle branching, recovery triggers, marker lifecycle, and final MQTT task result.

- [ ] **Step 1: Add failing workflow-contract tests**

Require `stream:`, `archive: operation: extract`, `source.stream`, staging destination, and `apply-directory --source-directory`. Reject `download:`, `--archive-path`, and `PACKAGE_ARCHIVE_PATH` from the apply workflow.

- [ ] **Step 2: Add a pre-lifecycle failure test**

Serve a truncated or malicious ZIP and assert no service/process stop task executes, no target file changes, and no package marker is created.

- [ ] **Step 3: Run focused tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target trident_core_tests`

Run: `ctest --preset win-dev-user -R trident_core --output-on-failure`

Expected: current file-download workflow fails the new assertions.

- [ ] **Step 4: Rewrite the data-plane prefix**

```yaml
- name: package_source
  depends_on: [validate_request]
  stream:
    open_http_source:
      url: "{{ DOWNLOAD_URL }}"
      sha256: "{{ SHA256 }}"
      timeout_ms: 300000
    output: body

- name: extract_package
  depends_on: [package_source]
  archive:
    operation: extract
    format: zip
    source:
      stream: package_source.body
    destination:
      directory: "{{ PACKAGE_STAGING_ROOT }}"
```

All stop tasks must depend on `extract_package`, not the lazy `package_source` registration task.

- [ ] **Step 5: Switch installer invocation**

Invoke `apply-directory --source-directory {{ tasks.extract_package.outputs.path }}`. Pass `SHA256` to `open_http_source`; a mismatch fails the source and causes archive staging cleanup before lifecycle mutation. Keep recovery triggers and marker commands.

- [ ] **Step 6: Update station inputs**

Replace `PACKAGE_ARCHIVE_PATH` with `PACKAGE_STAGING_ROOT`. Validate it is a station-controlled directory before dispatch. Do not accept a backend-provided local path.

- [ ] **Step 7: Run package and Trident integration tests**

Run: `cmake --build --preset win-dev-user --target package_installer_tests trident_core_tests`

Run: `ctest --preset win-dev-user -R "package_installer|trident_core" --output-on-failure`

Expected: successful packages install and recover lifecycle; malformed/network-failed packages stop before lifecycle mutation.

- [ ] **Step 8: Commit streaming package migration**

```powershell
git add Retro/trident/tasks/package-sync-apply.yml Retro/trident/tasks/package-sync-upgrade.yml Retro/trident/src/trident_station_command.cpp Retro/trident/src/praktor_runner.cpp Retro/trident/tests/trident_core_tests.cpp
git commit -m "refactor(retro): stream packages into Praktor staging"
```

### Task 5: Remove duplicated helpers after caller audit

**Files:**
- Delete: `Retro/trident/src/process_control_main.cpp`
- Modify: `Retro/trident/CMakeLists.txt`
- Modify: `Retro/package_installer/src/package_installer.cpp`
- Modify: `Retro/package_installer/CMakeLists.txt`
- Modify: `Retro/package_installer/tests/package_installer_tests.cpp`
- Modify: `Retro/trident/tests/trident_core_tests.cpp`

**Interfaces:**
- Removes: `trident_process_control` target and Package Installer archive-input CLI.
- Preserves: `apply-directory`, marker commands, overlay behavior, and every migrated workflow.

- [ ] **Step 1: Prove there are no remaining helper callers**

Run: `rg.exe -n "trident_process_control|PROCESS_CONTROL_PATH|--archive-path|ApplyPackageArchiveToRoot" Retro`

Expected: matches exist only in the helper/legacy installer implementation and tests scheduled for deletion in this task. If a runtime caller remains, stop and migrate it before deletion.

- [ ] **Step 2: Add tests rejecting removed CLI paths**

Verify Package Installer returns a non-zero structured CLI error for `apply --archive-path` and continues to accept `apply-directory` and `mark`.

- [ ] **Step 3: Remove the process-control target and source**

Delete only the target/source after the caller audit. Remove installation and runtime DLL rules that mention that target.

- [ ] **Step 4: Remove installer ZIP extraction and dependency**

Delete archive parsing/extraction code and ZIP linkage from Package Installer. Keep directory overlay and marker code unchanged.

- [ ] **Step 5: Run the complete Retro verification gate**

Run: `cmake --build --preset win-dev-user`

Run: `ctest --preset win-dev-user --output-on-failure`

Run: `rg.exe -n "trident_process_control|PROCESS_CONTROL_PATH|--archive-path|ApplyPackageArchiveToRoot" Retro`

Run: `git diff --check`

Expected: build/tests pass, search returns no matches, and diff check is clean.

- [ ] **Step 6: Commit dead-code removal**

```powershell
git add -A -- Retro/trident/src/process_control_main.cpp Retro/trident/CMakeLists.txt Retro/package_installer/src/package_installer.cpp Retro/package_installer/CMakeLists.txt Retro/package_installer/tests/package_installer_tests.cpp Retro/trident/tests/trident_core_tests.cpp
git commit -m "refactor(retro): remove legacy process and archive helpers"
```

### Task 6: End-to-end upgrade and recovery evidence

**Files:**
- Modify: `Retro/trident/tests/trident_core_tests.cpp`
- Create: `Retro/trident/tests/data/package-stream-upgrade.yml`
- Modify: `Retro/trident/tests/CMakeLists.txt`
- Create: `Retro/trident/README.md`

**Interfaces:**
- Verifies: MQTT dispatch → workflow → stream/extract → lifecycle → apply-directory → recovery/final marker.

- [ ] **Step 1: Add an end-to-end success test**

Use a local HTTPS-compatible test adapter or injected HTTP resource to deliver a ZIP larger than the bridge capacity. Verify target bytes, lifecycle order, completed marker, correlation/dispatch ID, and final successful task result.

- [ ] **Step 2: Add failure/recovery tests**

Cover network interruption, archive rejection, installer partial overlay failure, target restart failure, and recovery failure. Verify which owner records each failure and that no result is reported successful prematurely.

- [ ] **Step 3: Document final ownership and operational diagnostics**

Document staging cleanup, marker locations, service/process truth, presigned URL redaction, non-replayable stream failures, and the known non-atomic overlay limitation.

- [ ] **Step 4: Run final builds and tests**

Run in `C:/projects/cpp/Praktor`: `cmake --build --preset win-dev-user && ctest --preset win-dev-user --output-on-failure`

Run in `C:/projects/project-cpp-template`: `cmake --build --preset win-dev-user && ctest --preset win-dev-user --output-on-failure`

Expected: both repositories build and all tests pass.

- [ ] **Step 5: Commit end-to-end evidence**

```powershell
git add Retro/trident/tests/trident_core_tests.cpp Retro/trident/tests/data/package-stream-upgrade.yml Retro/trident/tests/CMakeLists.txt Retro/trident/README.md
git commit -m "test(retro): verify streamed package upgrades"
```
