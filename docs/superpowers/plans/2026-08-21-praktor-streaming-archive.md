# Praktor Streaming Archive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded execution-local byte streams and ZIP file/stream extraction and creation backed by TurboHTTP and libarchive.

**Architecture:** A workflow-owned `ResourceRegistry` stores strongly typed, single-consumer source/sink resources outside JSON context. HTTP and archive callbacks exchange owned byte copies through a fixed-capacity, cancelable SPSC byte bridge; archive writes only to validated staging paths.

**Tech Stack:** C++20, TurboUtils bounded byte buffer and synchronization, TurboHTTP streaming facade, libarchive, TurboParser YAML, Catch2.

**Spec:** `docs/superpowers/specs/2026-08-21-praktor-system-actions-streaming-archive-design.md`

## Global Constraints

- Resources are execution-local and never cross `Praktor.dll` C ABI or serialized `WorkflowValue` storage.
- Resource lifecycle is exactly `Ready -> Acquired -> Completed|Failed|Cancelled`.
- Source and sink handles are strongly typed, generation-scoped, and single-consumer.
- Default bridge capacity is 1 MiB; default workflow limits are 16 handles and 16 MiB retained bytes; all values are startup-validated and configurable.
- The bridge has exactly one producer and one consumer, preserves FIFO bytes, blocks with cancellation when full/empty, and never drops data.
- HTTP body callback memory is borrowed and copied before the callback returns.
- No retry or redirect replay is allowed after the first request-body or response-body byte.
- Only ZIP store/deflate is enabled; encrypted, multipart, symlink/reparse, absolute, traversal, duplicate, and over-limit entries are rejected.
- Failed extraction deletes only the validated staging directory created by that task.

---

### Task 1: Add libarchive and typed stream/archive schema

**Files:**
- Modify: `vcpkg.json`
- Modify: `CMakeLists.txt`
- Modify: `praktor/CMakeLists.txt`
- Modify: `praktor/include/yml/task_types.hpp`
- Modify: `praktor/include/yml/task.hpp`
- Modify: `praktor/src/yml/task_yaml_internal.hpp`
- Modify: `praktor/src/yml/task_field_parser.cpp`
- Modify: `praktor/src/yml/task_yaml.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Test: `praktor/test/task_parser_test.cpp`

**Interfaces:**
- Produces: `StreamParams`, `ArchiveParams`, `ArchiveEndpoint`, `ArchiveLimits`, `WorkflowResourceLimits`; `TaskAction::Stream` and `TaskAction::Archive`.
- Depends on: structured errors from the system-actions plan.

- [ ] **Step 1: Write parser tests for the four endpoint combinations**

Test file→directory extraction, stream→directory extraction, directory→file creation, and directory→stream creation. Verify the stream task accepts exactly one of `open_http_source` or `open_http_sink`. Parse a top-level `resources` map with `max_handles`, `max_retained_bytes`, and `bridge_capacity_bytes`.

- [ ] **Step 2: Add strict invalid-schema tests**

Reject mixed file/stream endpoints, extract without directory destination, create without file/stream destination, unknown format/compression, zero or overflowing limits, non-HTTPS URLs, source/sink direction mismatch, and resource limits where `bridge_capacity_bytes > max_retained_bytes`.

- [ ] **Step 3: Run parser tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target task_parser_test.praktor_lib`

Expected: compile/assertion failures because the types and parsers do not exist.

- [ ] **Step 4: Add typed contracts**

```cpp
enum class StreamDirection { HttpSource, HttpSink };
struct StreamParams {
  StreamDirection direction;
  std::string url;
  std::string method{"PUT"};
  std::string sha256;
  int timeout_ms{300000};
  std::string output{"body"};
};

struct ArchiveEndpoint {
  enum class Kind { File, Directory, Stream } kind;
  std::string value;
};

struct ArchiveLimits {
  std::uint64_t max_entries{10000};
  std::uint64_t max_entry_bytes{1073741824};
  std::uint64_t max_total_bytes{4294967296};
  std::uint32_t max_compression_ratio{200};
};

struct ArchiveParams {
  enum class Operation { Extract, Create } operation;
  ArchiveEndpoint source;
  ArchiveEndpoint destination;
  ArchiveLimits limits;
  std::string format{"zip"};
  std::string compression{"deflate"};
};

struct WorkflowResourceLimits {
  std::size_t max_handles{16};
  std::size_t max_retained_bytes{16u * 1024u * 1024u};
  std::size_t bridge_capacity_bytes{1u * 1024u * 1024u};
};
```

Store `WorkflowResourceLimits resources;` on `Workflow`. `parse_workflow` accepts the top-level `resources` map and validates all values with checked conversion to `size_t`.

- [ ] **Step 5: Link libarchive through vcpkg**

Add dependency name `libarchive`, use `find_package(LibArchive REQUIRED)`, link `LibArchive::LibArchive`, and preserve `$<TARGET_RUNTIME_DLLS>` copying for tests and CLI.

- [ ] **Step 6: Add all new fields to the action hash and run tests**

Run: `cmake --build --preset win-dev-user --target task_parser_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "task_parser|schema_validation" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 7: Commit schema and dependency**

```powershell
git add vcpkg.json CMakeLists.txt praktor/CMakeLists.txt praktor/include/yml/task_types.hpp praktor/include/yml/task.hpp praktor/src/yml/task_yaml_internal.hpp praktor/src/yml/task_field_parser.cpp praktor/src/yml/task_yaml.cpp praktor/src/dag/workflow_executor.cpp praktor/test/task_parser_test.cpp
git commit -m "feat: define stream and archive actions"
```

### Task 2: Execution-local ResourceRegistry

**Files:**
- Create: `praktor/include/resources/resource_handle.hpp`
- Create: `praktor/include/resources/resource_registry.hpp`
- Create: `praktor/src/resources/resource_registry.cpp`
- Modify: `praktor/include/dag/workflow_context.hpp`
- Modify: `praktor/src/workflow_runner.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/resource_registry_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `WorkflowResourceLimits` parsed in Task 1.
- Produces: `ResourceHandle {id, generation, type}`, `ByteSource`, `ByteSink`, `ResourceRegistry::register/acquire/complete/fail/cancelAll`.
- Ownership: `WorkflowContext` root owns a shared registry; `fork()` shares the same registry and generation.

- [ ] **Step 1: Write lifecycle and type tests**

```cpp
TEST_CASE("resource handle is one-shot and generation scoped") {
  ResourceRegistry registry({.max_handles=16,
                             .max_retained_bytes=16u * 1024u * 1024u});
  auto handle = registry.registerSource(
      std::make_shared<FakeByteSource>(), 1u * 1024u * 1024u);
  REQUIRE(registry.acquireSource(handle));
  CHECK(registry.acquireSource(handle).error() == ResourceError::AlreadyConsumed);
  auto stale = handle;
  ++stale.generation;
  CHECK(registry.acquireSource(stale).error() == ResourceError::NotFound);
}
```

- [ ] **Step 2: Add capacity and cleanup tests**

Cover 0/1/16/17 handles, retained-byte checked addition overflow, wrong source/sink type, complete/fail/cancel terminal states, and registry destruction with unconsumed resources.

- [ ] **Step 3: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target resource_registry_test.praktor_lib`

Expected: missing resource interfaces.

- [ ] **Step 4: Implement opaque handles and registry**

The handle serialized into task output is a bounded opaque string such as `praktor-resource:<generation>:<id>:source`; resolution always goes through the registry. The registry owns `shared_ptr<Resource>` and is the only component that mutates lifecycle state.

- [ ] **Step 5: Share registry across workflow forks**

Construct it in `WorkflowRunner` from `prepared.workflow.resources`, inject it into the root `WorkflowContext`, and copy the shared registry pointer in `fork()`. Existing direct `WorkflowContext` construction uses the documented defaults. Do not put resource objects in `VariableScope` or `TaskRegistry`.

- [ ] **Step 6: Run focused and context regression tests**

Run: `cmake --build --preset win-dev-user --target resource_registry_test.praktor_lib workflow_context_test.praktor_lib concurrency_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "resource_registry|workflow_context|concurrency" --output-on-failure`

Expected: all selected tests pass.

- [ ] **Step 7: Commit the registry**

```powershell
git add praktor/include/resources/resource_handle.hpp praktor/include/resources/resource_registry.hpp praktor/src/resources/resource_registry.cpp praktor/include/dag/workflow_context.hpp praktor/src/workflow_runner.cpp praktor/CMakeLists.txt praktor/test/resource_registry_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add workflow resource registry"
```

### Task 3: Cancelable bounded byte bridge

**Files:**
- Create: `praktor/include/resources/bounded_byte_pipe.hpp`
- Create: `praktor/src/resources/bounded_byte_pipe.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/bounded_byte_pipe_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Produces: `PipeResult`, `BoundedBytePipe::write`, `read`, `close`, `fail`, `cancel`, metrics snapshot.
- Topology: exactly one fixed producer thread and one fixed consumer thread.

- [ ] **Step 1: Write protocol tests before implementation**

Test FIFO, wrap/compaction, exact capacity, capacity+1 blocking, consumer wake, producer wake, EOF distinct from error/cancel, partial read/write, and destructor after both roles quiesce.

- [ ] **Step 2: Add concurrency failure tests**

Block producer on full then cancel; block consumer on empty then fail. Both must return within 500 ms. Verify no byte is silently dropped and peak bytes never exceed capacity.

- [ ] **Step 3: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target bounded_byte_pipe_test.praktor_lib`

Expected: missing pipe interfaces.

- [ ] **Step 4: Implement the bounded protocol**

Use one `turbo_byte_buffer_t` with a hard maximum, protected by one mutex and two condition variables. Callback input is copied into owned storage. Full waits for space; empty waits for bytes or a terminal state. All waits re-check terminal state after wake.

```cpp
enum class PipeStatus { Ok, Eof, Failed, Cancelled };
struct PipeResult { PipeStatus status; std::size_t bytes; std::string message; };
PipeResult write(std::span<const std::byte> input);
PipeResult read(std::span<std::byte> output);
```

- [ ] **Step 5: Add bounded observability**

Expose current bytes, peak bytes, producer wait count, consumer wait count, total bytes written/read, and terminal state. Do not log per chunk.

- [ ] **Step 6: Run correctness tests and sanitizer configuration where available**

Run: `cmake --build --preset win-dev-user --target bounded_byte_pipe_test.praktor_lib`

Run: `ctest --preset win-dev-user -R bounded_byte_pipe --output-on-failure`

Expected: all tests pass. If MSVC ASan is configured, also run the target under that preset and record whether race tooling is unavailable.

- [ ] **Step 7: Commit the bridge**

```powershell
git add praktor/include/resources/bounded_byte_pipe.hpp praktor/src/resources/bounded_byte_pipe.cpp praktor/CMakeLists.txt praktor/test/bounded_byte_pipe_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add bounded byte stream bridge"
```

### Task 4: Lazy TurboHTTP source and sink resources

**Files:**
- Create: `praktor/include/resources/http_stream_resource.hpp`
- Create: `praktor/src/resources/http_stream_resource.cpp`
- Create: `praktor/include/executors/stream_executor.hpp`
- Create: `praktor/src/executors/stream_executor.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/http_stream_resource_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `StreamParams`, `ResourceRegistry`, `BoundedBytePipe`, `turbo_http_request_stream_sync`.
- Produces: lazy `HttpByteSource`/`HttpByteSink`; `StreamExecutor` writes an opaque handle to the configured output key.

- [ ] **Step 1: Add local-server tests for lazy open**

Verify the `stream` task registers a handle without making a request. The first acquire starts one request; a second acquire fails before network I/O.

- [ ] **Step 2: Add download and upload streaming tests**

Download a payload larger than bridge capacity and verify byte equality. Upload through `http_data_read_cb` and verify server bytes. For a source with `sha256`, verify the digest at EOF and fail the resource on mismatch. Assert 4xx, 5xx, disconnect, timeout, cancel, digest mismatch, and short body produce distinct structured failures.

- [ ] **Step 3: Add sensitive-log tests**

Use `https://host/path?X-Amz-Signature=secret`; capture diagnostics and assert `secret` and the query string are absent.

- [ ] **Step 4: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target http_stream_resource_test.praktor_lib`

Expected: missing HTTP resource and stream executor.

- [ ] **Step 5: Implement lazy source/sink and one transfer worker per consumed resource**

Create the sync client with `turbo_http_create_sync`. Set `options.follow_redirects = 0`, `options.max_redirects = 0`, and `options.retry.max_retries = 0`. For download, pass a null request-body reader, copy response callbacks into the pipe, and update TurboNet Crypto SHA-256 before publishing each copied chunk. For upload, `http_data_read_cb` reads from the pipe; pass content length when known and `0` for TurboHTTP chunked streaming when unknown. Start no thread until acquire.

- [ ] **Step 6: Enforce non-replayable semantics**

Track `body_started`. Before it becomes true, return the direct transport error without hidden fallback; after it becomes true, explicitly disable retry/redirect replay and return `transport_failed`. Success requires transport success and 2xx final status.

- [ ] **Step 7: Register executor and run tests**

Run: `cmake --build --preset win-dev-user --target http_stream_resource_test.praktor_lib`

Run: `ctest --preset win-dev-user -R "http_stream_resource|resource_registry|bounded_byte_pipe" --output-on-failure`

Expected: all selected tests pass and no transfer thread survives test teardown.

- [ ] **Step 8: Commit HTTP resources**

```powershell
git add praktor/include/resources/http_stream_resource.hpp praktor/src/resources/http_stream_resource.cpp praktor/include/executors/stream_executor.hpp praktor/src/executors/stream_executor.cpp praktor/src/dag/workflow_executor.cpp praktor/CMakeLists.txt praktor/test/http_stream_resource_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add lazy HTTP stream resources"
```

### Task 5: Secure libarchive adapter for file endpoints

**Files:**
- Create: `praktor/include/archive/archive_adapter.hpp`
- Create: `praktor/src/archive/archive_adapter.cpp`
- Create: `praktor/include/archive/archive_path_policy.hpp`
- Create: `praktor/src/archive/archive_path_policy.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/archive_adapter_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ArchiveParams`, libarchive callback API, TurboUtils file APIs where applicable.
- Produces: `ArchiveReader`, `ArchiveWriter`, `validateArchiveEntryPath`, file→directory and directory/file-list→file operations.

- [ ] **Step 1: Create golden ZIP tests**

Create and extract store/deflate archives containing nested files and empty directories; compare exact relative paths and bytes.

- [ ] **Step 2: Create policy-rejection tests**

Generate archives with `../`, absolute/drive/UNC paths, duplicate normalized paths, file-directory conflicts, symlink/hardlink/reparse metadata, encryption flags, unsupported compression, too many entries, oversized entries, excessive total bytes, and ratio over 200.

- [ ] **Step 3: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target archive_adapter_test.praktor_lib`

Expected: missing adapter/policy interfaces.

- [ ] **Step 4: Implement format minimization and path policy**

Enable only ZIP format and store/deflate filters. Normalize each entry once, verify it remains below the validated staging root, reject duplicates before opening the destination, and use checked addition for expanded-byte counters.

- [ ] **Step 5: Implement staging cleanup ownership**

The adapter creates a uniquely named child under the requested staging parent and returns its resolved path. On failure it removes only that verified child. On success the caller owns the directory.

- [ ] **Step 6: Run file archive tests**

Run: `cmake --build --preset win-dev-user --target archive_adapter_test.praktor_lib`

Run: `ctest --preset win-dev-user -R archive_adapter --output-on-failure`

Expected: all valid cases pass and every unsafe archive is rejected before writing outside staging.

- [ ] **Step 7: Commit the secure adapter**

```powershell
git add praktor/include/archive/archive_adapter.hpp praktor/src/archive/archive_adapter.cpp praktor/include/archive/archive_path_policy.hpp praktor/src/archive/archive_path_policy.cpp praktor/CMakeLists.txt praktor/test/archive_adapter_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: add secure zip archive adapter"
```

### Task 6: Connect archive callbacks to stream resources

**Files:**
- Create: `praktor/include/executors/archive_executor.hpp`
- Create: `praktor/src/executors/archive_executor.cpp`
- Modify: `praktor/src/dag/workflow_executor.cpp`
- Modify: `praktor/CMakeLists.txt`
- Test: `praktor/test/archive_executor_test.cpp`
- Modify: `praktor/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ArchiveAdapter`, source/sink handles, `ResourceRegistry`, libarchive custom read/write callbacks.
- Produces: all four file/stream archive paths and outputs `path`, `entries`, `input_bytes`, `output_bytes`.

- [ ] **Step 1: Add stream→directory and directory→stream tests**

Use fake source/sink resources first. Transfer archives larger than bridge capacity and compare extracted/uploaded bytes.

- [ ] **Step 2: Add cross-side failure and cancellation tests**

Cover archive rejecting while HTTP produces, HTTP failing while archive reads, archive writer failing while HTTP consumes, workflow cancellation, and registry destruction. Each blocked side must wake and terminate.

- [ ] **Step 3: Run tests and confirm failure**

Run: `cmake --build --preset win-dev-user --target archive_executor_test.praktor_lib`

Expected: missing executor and callback adapters.

- [ ] **Step 4: Implement libarchive callbacks over ByteSource/ByteSink**

Read callback returns a view valid until the next callback by keeping one executor-owned chunk. Write callback copies into the sink bridge. Map EOF, cancel, and failure separately; never report failure as clean EOF.

- [ ] **Step 5: Register executor and finalize resource states**

Acquire once, execute, then call exactly one of `complete`, `fail`, or `cancel`. HTTP sink success is not committed until its final 2xx response has been joined and validated.

- [ ] **Step 6: Run the full streaming archive gate**

Run: `cmake --build --preset win-dev-user`

Run: `ctest --preset win-dev-user -R "archive|stream|resource|download" --output-on-failure`

Run: `git diff --check`

Expected: all selected tests pass, bounded-memory assertions hold, and diff check is clean.

- [ ] **Step 7: Commit archive execution**

```powershell
git add praktor/include/executors/archive_executor.hpp praktor/src/executors/archive_executor.cpp praktor/src/dag/workflow_executor.cpp praktor/CMakeLists.txt praktor/test/archive_executor_test.cpp praktor/test/CMakeLists.txt
git commit -m "feat: execute archives over files and streams"
```

### Task 7: Public documentation and installed-consumer regression

**Files:**
- Modify: `README.md`
- Create: `praktor/test/workflows/streaming-archive.yml`
- Modify: `pistol/test/CMakeLists.txt`
- Modify: `pistol/test/pistol_api_test.cpp`

**Interfaces:**
- Preserves: existing `Praktor.dll` workflow-path/input-JSON/output-JSON ABI.
- Documents: handle scope, archive limits, HTTP replay rules, and cleanup behavior.

- [ ] **Step 1: Add an installed/API consumer test**

Run a workflow containing stream/archive actions through the existing C API and verify only ordinary JSON outputs cross the boundary; no resource handle is returned as a reusable external capability.

- [ ] **Step 2: Document exact action examples and limits**

Include the approved download→extract and create→upload examples, one-shot handle errors, 1 MiB/16 handle/16 MiB defaults, presigned URL redaction, and ZIP restrictions.

- [ ] **Step 3: Run complete Praktor verification**

Run: `cmake --build --preset win-dev-user`

Run: `ctest --preset win-dev-user --output-on-failure`

Expected: all tests pass, including C API/installed-consumer coverage.

- [ ] **Step 4: Commit docs and ABI regression**

```powershell
git add README.md praktor/test/workflows/streaming-archive.yml pistol/test/CMakeLists.txt pistol/test/pistol_api_test.cpp
git commit -m "docs: document streaming archive workflows"
```
