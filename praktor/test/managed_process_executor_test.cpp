#include "executors/managed_process_executor.hpp"
#include "system/managed_process.hpp"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace Praktor::System::WinDetail {

ManagedProcessResult<bool> matchesManagedProcessCandidate(
    std::string_view configured_image_name,
    std::string_view canonical_image_name,
    std::uint32_t candidate_session_id,
    std::uint32_t current_session_id);
ManagedProcessResult<bool> matchesManagedProcessSnapshot(
    const ManagedProcessSnapshot& expected,
    const ManagedProcessSnapshot& observed,
    std::uint32_t current_session_id);

}  // namespace Praktor::System::WinDetail
#endif
namespace {

using Praktor::System::IManagedProcessBackend;
using Praktor::System::ManagedProcessCommandResult;
using Praktor::System::ManagedProcessController;
using Praktor::System::ManagedProcessError;
using Praktor::System::ManagedProcessResult;
using Praktor::System::ManagedProcessSnapshot;
using Praktor::System::ManagedProcessState;

constexpr std::uint32_t kFixturePid = 4242;

ManagedProcessSnapshot running(std::uint32_t pid = kFixturePid) {
  return {ManagedProcessState::Running, pid, 7, 9001,
          "managed_process_fixture.exe"};
}

ManagedProcessSnapshot notRunning() {
  return {ManagedProcessState::NotRunning, 0};
}

template <typename T>
ManagedProcessResult<T> success(T value) {
  return {true, std::move(value), 0, {}};
}

ManagedProcessCommandResult commandSuccess() {
  return {true, 0, {}};
}

class FakeManagedProcessBackend final : public IManagedProcessBackend {
public:
  ManagedProcessResult<ManagedProcessSnapshot> query(
      const ManagedProcessIdentity& identity) override {
    queried_identities.push_back(identity);
    if (query_action) {
      return query_action();
    }
    return success(notRunning());
  }

  ManagedProcessResult<ManagedProcessSnapshot> start(
      const ManagedProcessParams& params) override {
    started_params.push_back(params);
    if (start_action) {
      return start_action();
    }
    return start_result;
  }

  ManagedProcessCommandResult requestStop(
      const ManagedProcessSnapshot& snapshot) override {
    stopped_snapshots.push_back(snapshot);
    return stop_result;
  }

  ManagedProcessCommandResult terminate(
      const ManagedProcessSnapshot& snapshot) override {
    terminated_snapshots.push_back(snapshot);
    if (terminate_action) {
      return terminate_action(snapshot);
    }
    return terminate_result;
  }

  std::function<ManagedProcessResult<ManagedProcessSnapshot>()> query_action;
  std::function<ManagedProcessResult<ManagedProcessSnapshot>()> start_action;
  ManagedProcessResult<ManagedProcessSnapshot> start_result = success(running());
  ManagedProcessCommandResult stop_result = commandSuccess();
  ManagedProcessCommandResult terminate_result = commandSuccess();
  std::function<ManagedProcessCommandResult(
      const ManagedProcessSnapshot&)> terminate_action;
  std::vector<ManagedProcessIdentity> queried_identities;
  std::vector<ManagedProcessParams> started_params;
  std::vector<ManagedProcessSnapshot> stopped_snapshots;
  std::vector<ManagedProcessSnapshot> terminated_snapshots;
};

ManagedProcessParams params(SystemOperation operation) {
  ManagedProcessParams value;
  value.operation = operation;
  value.executable = "C:/fixtures/managed_process_fixture.exe";
  value.arguments = {"--mode", "safe value"};
  value.working_directory = "C:/fixtures";
  value.identity.image_name = "managed_process_fixture.exe";
  value.startup_timeout_ms = 25;
  value.stop_timeout_ms = 25;
  return value;
}

}  // namespace

TEST_CASE("managed process status reports the queried snapshot") {
  FakeManagedProcessBackend backend;
  backend.query_action = [] { return success(running()); };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(params(SystemOperation::Status));

  REQUIRE(result.ok);
  CHECK(result.snapshot.state == ManagedProcessState::Running);
  CHECK(result.snapshot.pid == kFixturePid);
  CHECK_FALSE(result.changed);
  CHECK(backend.queried_identities.size() == 1);
}

TEST_CASE("managed process start and stop are idempotent at their target state") {
  SECTION("start when already running") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(running()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));

    const auto result = controller.execute(params(SystemOperation::Start));

    REQUIRE(result.ok);
    CHECK(result.snapshot.state == ManagedProcessState::Running);
    CHECK_FALSE(result.changed);
    CHECK(backend.started_params.empty());
  }

  SECTION("stop when already stopped") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(notRunning()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));

    const auto result = controller.execute(params(SystemOperation::Stop));

    REQUIRE(result.ok);
    CHECK(result.snapshot.state == ManagedProcessState::NotRunning);
    CHECK_FALSE(result.changed);
    CHECK(backend.stopped_snapshots.empty());
  }
}

TEST_CASE("managed process start waits for identity query to report running") {
  FakeManagedProcessBackend backend;
  std::size_t query_count = 0;
  backend.query_action = [&] {
    ++query_count;
    return success(query_count < 3 ? notRunning() : running());
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto request = params(SystemOperation::Start);
  const auto result = controller.execute(request);

  INFO(result.message);
  REQUIRE(result.ok);
  CHECK(result.snapshot.state == ManagedProcessState::Running);
  CHECK(result.changed);
  REQUIRE(backend.started_params.size() == 1);
  CHECK(backend.started_params.front().arguments == request.arguments);
  CHECK(query_count == 3);
}

TEST_CASE("managed process stop requests graceful close and confirms stopped") {
  FakeManagedProcessBackend backend;
  std::size_t query_count = 0;
  backend.query_action = [&] {
    ++query_count;
    return success(query_count < 3 ? running() : notRunning());
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(params(SystemOperation::Stop));

  INFO(result.message);
  REQUIRE(result.ok);
  CHECK(result.snapshot.state == ManagedProcessState::NotRunning);
  CHECK(result.changed);
  REQUIRE(backend.stopped_snapshots.size() == 1);
  CHECK(backend.stopped_snapshots.front().pid == kFixturePid);
  CHECK(backend.terminated_snapshots.empty());
}

TEST_CASE("managed process restart confirms stop before starting") {
  FakeManagedProcessBackend backend;
  std::size_t query_count = 0;
  backend.query_action = [&] {
    ++query_count;
    if (query_count == 1) {
      return success(running());
    }
    if (query_count == 2) {
      return success(notRunning());
    }
    if (query_count == 3) {
      return success(notRunning());
    }
    return success(running(4343));
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(params(SystemOperation::Restart));

  INFO(result.message);
  REQUIRE(result.ok);
  CHECK(result.snapshot.state == ManagedProcessState::Running);
  CHECK(result.snapshot.pid == 4343);
  CHECK(result.changed);
  REQUIRE(backend.stopped_snapshots.size() == 1);
  REQUIRE(backend.started_params.size() == 1);
  CHECK(query_count == 4);
}

TEST_CASE("managed process startup timeout is bounded by the startup deadline") {
  FakeManagedProcessBackend backend;
  backend.query_action = [] { return success(notRunning()); };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));
  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;

  const auto result = controller.execute(request);

  CHECK_FALSE(result.ok);
  CHECK(result.error == ManagedProcessError::Timeout);
  CHECK(result.phase == "start_poll");
  CHECK(result.snapshot.state == ManagedProcessState::NotRunning);
  CHECK(result.changed);
  CHECK(backend.started_params.size() == 1);
}

TEST_CASE("managed process backend start time consumes the startup budget") {
  FakeManagedProcessBackend backend;
  bool start_completed = false;
  bool terminated = false;
  backend.query_action = [&] {
    return success(start_completed && !terminated ? running() : notRunning());
  };
  backend.start_action = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    start_completed = true;
    return success(running());
  };
  backend.terminate_action = [&](const ManagedProcessSnapshot&) {
    terminated = true;
    return commandSuccess();
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));
  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;

  const auto result = controller.execute(request);

  CHECK_FALSE(result.ok);
  CHECK(result.error == ManagedProcessError::Timeout);
  CHECK(result.phase == "start_poll");
  CHECK(result.duration_ms >= 15);
  REQUIRE(backend.started_params.size() == 1);
}

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
    CHECK(snapshot.session_id == started.session_id);
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
  CHECK(result.snapshot.pid == 0);
  REQUIRE(backend.terminated_snapshots.size() == 1);
}

TEST_CASE("startup timeout reports compensation command failure") {
  FakeManagedProcessBackend backend;
  const ManagedProcessSnapshot started = running(5252);
  bool spawned = false;
  backend.start_action = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    spawned = true;
    return success(started);
  };
  backend.query_action = [&] {
    return success(spawned ? started : notRunning());
  };
  backend.terminate_action = [](const ManagedProcessSnapshot&) {
    return ManagedProcessCommandResult{false, 5, "cleanup denied"};
  };

  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;
  request.stop_timeout_ms = 25;
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(request);

  CHECK_FALSE(result.ok);
  CHECK(result.error == ManagedProcessError::BackendFailure);
  CHECK(result.phase == "start_timeout_terminate");
  CHECK(result.native_error == 5);
  CHECK(result.snapshot.state == ManagedProcessState::Running);
  CHECK(result.snapshot.pid == started.pid);
  CHECK(result.snapshot.instance_token == started.instance_token);
  REQUIRE(backend.terminated_snapshots.size() == 1);
}

TEST_CASE("startup timeout reports compensation confirmation timeout") {
  FakeManagedProcessBackend backend;
  const ManagedProcessSnapshot started = running(5353);
  bool spawned = false;
  backend.start_action = [&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    spawned = true;
    return success(started);
  };
  backend.query_action = [&] {
    return success(spawned ? started : notRunning());
  };
  backend.terminate_action = [](const ManagedProcessSnapshot&) {
    return commandSuccess();
  };

  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;
  request.stop_timeout_ms = 5;
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(request);

  CHECK_FALSE(result.ok);
  CHECK(result.error == ManagedProcessError::Timeout);
  CHECK(result.phase == "start_timeout_cleanup_poll");
  CHECK(result.snapshot.state == ManagedProcessState::Running);
  CHECK(result.snapshot.pid == started.pid);
  CHECK(result.snapshot.instance_token == started.instance_token);
  REQUIRE(backend.terminated_snapshots.size() == 1);
}

TEST_CASE("managed process stop timeout does not terminate unless explicitly enabled") {
  SECTION("force termination disabled") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(running()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));
    auto request = params(SystemOperation::Stop);
    request.stop_timeout_ms = 5;

    const auto result = controller.execute(request);

    CHECK_FALSE(result.ok);
    CHECK(result.error == ManagedProcessError::Timeout);
    CHECK(result.phase == "stop_poll");
    CHECK(result.snapshot.state == ManagedProcessState::Running);
    CHECK(backend.stopped_snapshots.size() == 1);
    CHECK(backend.terminated_snapshots.empty());
  }

  SECTION("force termination enabled") {
    FakeManagedProcessBackend backend;
    backend.query_action = [&] {
      return success(backend.terminated_snapshots.empty() ? running()
                                                          : notRunning());
    };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));
    auto request = params(SystemOperation::Stop);
    request.stop_timeout_ms = 5;
    request.force_terminate = true;

    const auto result = controller.execute(request);

    INFO(result.message);
    REQUIRE(result.ok);
    CHECK(result.error == ManagedProcessError::None);
    CHECK(result.phase.empty());
    CHECK(result.message.empty());
    CHECK(result.snapshot.state == ManagedProcessState::NotRunning);
    CHECK(result.changed);
    REQUIRE(backend.terminated_snapshots.size() == 1);
    CHECK(backend.terminated_snapshots.front().pid == kFixturePid);
  }
}

TEST_CASE("managed process executor maps outputs and structured failures") {
  SECTION("successful status") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(running()); };
    Praktor::Execution::ManagedProcessExecutor executor(
        backend, std::chrono::milliseconds(1));

    Task task;
    task.name = "query_fixture";
    task.action = TaskAction::ManagedProcess;
    task.declared_runner = "managed_process";
    task.specifics = params(SystemOperation::Status);
    WorkflowContext context;

    const TaskResult result = executor.execute(task, context);

    REQUIRE(result.success);
    CHECK(context.getValueByPath("tasks.__root__.outputs.image_name")
              .as<std::string>() == "managed_process_fixture.exe");
    CHECK(context.getValueByPath("tasks.__root__.outputs.operation")
              .as<std::string>() == "status");
    CHECK(context.getValueByPath("tasks.__root__.outputs.state")
              .as<std::string>() == "running");
    CHECK(context.getValueByPath("tasks.__root__.outputs.pid").as<int64_t>() ==
          kFixturePid);
    CHECK_FALSE(context.getValueByPath("tasks.__root__.outputs.changed")
                    .as<bool>());
    CHECK(context.getValueByPath("tasks.__root__.outputs.duration_ms")
              .as<int64_t>() >= 0);
  }

  SECTION("backend failure") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      return ManagedProcessResult<ManagedProcessSnapshot>{
          false, {}, 5, "failed to enumerate managed processes"};
    };
    Praktor::Execution::ManagedProcessExecutor executor(
        backend, std::chrono::milliseconds(1));

    Task task;
    task.name = "query_fixture";
    task.action = TaskAction::ManagedProcess;
    task.declared_runner = "managed_process";
    task.specifics = params(SystemOperation::Status);
    WorkflowContext context;

    const TaskResult result = executor.execute(task, context);

    CHECK_FALSE(result.success);
    CHECK(result.error_code == "service_state_failed");
    CHECK(result.error_phase == "query");
    CHECK_FALSE(result.error_details.contains("native_error"));
  }

  SECTION("unsupported platform") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      return ManagedProcessResult<ManagedProcessSnapshot>{
          false, {}, 0,
          std::string(Praktor::System::kManagedProcessUnsupportedPlatform)};
    };
    Praktor::Execution::ManagedProcessExecutor executor(
        backend, std::chrono::milliseconds(1));

    Task task;
    task.name = "query_fixture";
    task.action = TaskAction::ManagedProcess;
    task.declared_runner = "managed_process";
    task.specifics = params(SystemOperation::Status);
    WorkflowContext context;

    const TaskResult result = executor.execute(task, context);

    CHECK_FALSE(result.success);
    CHECK(result.error_code == "unsupported_platform");
    CHECK(result.error_phase == "query");
  }
}

#ifdef _WIN32
namespace {

constexpr auto kFixtureWindowReadyTimeout = std::chrono::seconds(3);
constexpr auto kFixtureWindowPollInterval = std::chrono::milliseconds(10);

struct FixtureWindowProbe {
  DWORD pid{0};
  bool found{false};
};

BOOL CALLBACK findFixtureTopLevelWindow(HWND window, LPARAM parameter) {
  auto* probe = reinterpret_cast<FixtureWindowProbe*>(parameter);
  DWORD window_pid = 0;
  GetWindowThreadProcessId(window, &window_pid);
  if (window_pid == probe->pid && GetWindow(window, GW_OWNER) == nullptr) {
    probe->found = true;
    return FALSE;
  }
  return TRUE;
}

bool waitForFixtureTopLevelWindow(std::uint32_t pid) {
  const auto deadline =
      std::chrono::steady_clock::now() + kFixtureWindowReadyTimeout;
  do {
    FixtureWindowProbe probe{static_cast<DWORD>(pid)};
    static_cast<void>(
        EnumWindows(findFixtureTopLevelWindow, reinterpret_cast<LPARAM>(&probe)));
    if (probe.found) {
      return true;
    }
    std::this_thread::sleep_for(kFixtureWindowPollInterval);
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

ManagedProcessCommandResult cleanupManagedProcessFixture(IManagedProcessBackend &backend,
                                                         const ManagedProcessIdentity &identity,
                                                         std::chrono::milliseconds timeout,
                                                         std::chrono::milliseconds poll_interval) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool termination_requested = false;
  std::uint32_t terminated_pid = 0;
  std::uint64_t terminated_token = 0;
  do {
    auto queried = backend.query(identity);
    if (!queried.ok) {
      return {false, queried.native_error, std::move(queried.message)};
    }
    if (queried.value.state == ManagedProcessState::NotRunning) {
      return {true, 0, {}};
    }

    const bool new_instance = !termination_requested ||
                              queried.value.pid != terminated_pid ||
                              queried.value.instance_token != terminated_token;
    if (new_instance) {
      auto terminated = backend.terminate(queried.value);
      if (!terminated.ok) {
        return terminated;
      }
      termination_requested = true;
      terminated_pid = queried.value.pid;
      terminated_token = queried.value.instance_token;
    }
    std::this_thread::sleep_for(poll_interval);
  } while (std::chrono::steady_clock::now() < deadline);

  auto final_query = backend.query(identity);
  if (!final_query.ok) {
    return {false, final_query.native_error, std::move(final_query.message)};
  }
  if (final_query.value.state == ManagedProcessState::NotRunning) {
    return {true, 0, {}};
  }
  return {false, ERROR_TIMEOUT, "timed out waiting for managed process fixture cleanup"};
}

class ManagedProcessFixtureCleanup {
public:
  ManagedProcessFixtureCleanup(IManagedProcessBackend &backend, ManagedProcessIdentity identity)
      : backend_(backend), identity_(std::move(identity)) {}

  ~ManagedProcessFixtureCleanup() noexcept {
    try {
      static_cast<void>(cleanup());
    } catch (...) {
    }
  }

  ManagedProcessFixtureCleanup(const ManagedProcessFixtureCleanup &) = delete;
  ManagedProcessFixtureCleanup &operator=(const ManagedProcessFixtureCleanup &) = delete;

  ManagedProcessCommandResult cleanup() {
    return cleanupManagedProcessFixture(backend_, identity_, std::chrono::seconds(3),
                                        std::chrono::milliseconds(20));
  }

private:
  IManagedProcessBackend &backend_;
  ManagedProcessIdentity identity_;
};

} // namespace

TEST_CASE("Windows managed process identity matching is current-session exact basename") {
  using Praktor::System::WinDetail::matchesManagedProcessCandidate;

  const auto case_insensitive =
      matchesManagedProcessCandidate("C:/configured/WORKER.EXE", "C:/runtime/worker.exe", 7, 7);
  REQUIRE(case_insensitive.ok);
  CHECK(case_insensitive.value);

  const auto different_extension =
      matchesManagedProcessCandidate("worker.exe", "C:/runtime/worker.com", 7, 7);
  REQUIRE(different_extension.ok);
  CHECK_FALSE(different_extension.value);

  const auto different_session =
      matchesManagedProcessCandidate("worker.exe", "C:/runtime/WORKER.EXE", 8, 7);
  REQUIRE(different_session.ok);
  CHECK_FALSE(different_session.value);
}

TEST_CASE("Windows fixture cleanup reports backend failures") {
  ManagedProcessIdentity identity;
  identity.image_name = "managed_process_fixture.exe";

  SECTION("query failure") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      return ManagedProcessResult<ManagedProcessSnapshot>{
          false, {}, 5, "fixture cleanup query failed"};
    };

    const auto cleaned = cleanupManagedProcessFixture(
        backend, identity, std::chrono::milliseconds(10), std::chrono::milliseconds(1));

    CHECK_FALSE(cleaned.ok);
    CHECK(cleaned.native_error == 5);
  }

  SECTION("termination failure") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      ManagedProcessSnapshot running;
      running.state = ManagedProcessState::Running;
      running.pid = kFixturePid;
      return ManagedProcessResult<ManagedProcessSnapshot>{true, running, 0, {}};
    };
    backend.terminate_result = {false, 6, "fixture cleanup terminate failed"};

    const auto cleaned = cleanupManagedProcessFixture(
        backend, identity, std::chrono::milliseconds(10), std::chrono::milliseconds(1));

    CHECK_FALSE(cleaned.ok);
    CHECK(cleaned.native_error == 6);
  }

  SECTION("timeout") {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      ManagedProcessSnapshot running;
      running.state = ManagedProcessState::Running;
      running.pid = kFixturePid;
      return ManagedProcessResult<ManagedProcessSnapshot>{true, running, 0, {}};
    };

    const auto cleaned = cleanupManagedProcessFixture(
        backend, identity, std::chrono::milliseconds(0), std::chrono::milliseconds(0));

    CHECK_FALSE(cleaned.ok);
    CHECK(cleaned.native_error == ERROR_TIMEOUT);
  }
}

TEST_CASE("Windows action validation rejects changed process evidence") {
  using Praktor::System::WinDetail::matchesManagedProcessSnapshot;

  ManagedProcessSnapshot expected;
  expected.state = ManagedProcessState::Running;
  expected.pid = kFixturePid;
  expected.session_id = 7;
  expected.instance_token = 101;
  expected.canonical_image_name = "worker.exe";

  auto changed_token = expected;
  ++changed_token.instance_token;
  const auto token_match = matchesManagedProcessSnapshot(expected, changed_token, 7);
  REQUIRE(token_match.ok);
  CHECK_FALSE(token_match.value);

  auto changed_image = expected;
  changed_image.canonical_image_name = "worker.com";
  const auto image_match = matchesManagedProcessSnapshot(expected, changed_image, 7);
  REQUIRE(image_match.ok);
  CHECK_FALSE(image_match.value);

  auto changed_session = expected;
  ++changed_session.session_id;
  const auto session_match = matchesManagedProcessSnapshot(expected, changed_session, 7);
  REQUIRE(session_match.ok);
  CHECK_FALSE(session_match.value);
}

TEST_CASE("Windows stop rejects a stale managed process instance token") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  INFO(started.phase << ":" << started.message);
  REQUIRE(started.ok);
  REQUIRE(started.changed);

  auto stale = started.snapshot;
  ++stale.instance_token;
  const auto stopped = backend->requestStop(stale);
  CHECK_FALSE(stopped.ok);

  const auto after = backend->query(request.identity);
  REQUIRE(after.ok);
  CHECK(after.value.state == ManagedProcessState::Running);
  CHECK(after.value.instance_token == started.snapshot.instance_token);

  const auto final_cleanup = cleanup.cleanup();
  INFO(final_cleanup.native_error << ":" << final_cleanup.message);
  REQUIRE(final_cleanup.ok);
  const auto cleaned = backend->query(request.identity);
  REQUIRE(cleaned.ok);
  CHECK(cleaned.value.state == ManagedProcessState::NotRunning);
}

TEST_CASE("Windows terminate rejects changed canonical process identity") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  INFO(started.phase << ":" << started.message);
  REQUIRE(started.ok);
  REQUIRE(started.changed);

  auto changed_identity = started.snapshot;
  changed_identity.canonical_image_name = "managed_process_fixture.com";
  const auto terminated = backend->terminate(changed_identity);
  CHECK_FALSE(terminated.ok);

  const auto after = backend->query(request.identity);
  REQUIRE(after.ok);
  CHECK(after.value.state == ManagedProcessState::Running);
  CHECK(after.value.instance_token == started.snapshot.instance_token);

  const auto final_cleanup = cleanup.cleanup();
  INFO(final_cleanup.native_error << ":" << final_cleanup.message);
  REQUIRE(final_cleanup.ok);
  const auto cleaned = backend->query(request.identity);
  REQUIRE(cleaned.ok);
  CHECK(cleaned.value.state == ManagedProcessState::NotRunning);
}

TEST_CASE("Windows managed process backend starts queries and gracefully stops fixture") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = false;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  INFO(started.phase << ":" << started.message);
  REQUIRE(started.ok);
  REQUIRE(started.snapshot.state == ManagedProcessState::Running);
  CHECK(started.changed);
  DWORD current_session_id = 0;
  REQUIRE(ProcessIdToSessionId(GetCurrentProcessId(), &current_session_id));
  CHECK(started.snapshot.session_id == current_session_id);
  CHECK(started.snapshot.instance_token != 0);
  CHECK(started.snapshot.canonical_image_name == request.identity.image_name);

  ManagedProcessIdentity uppercase_identity = request.identity;
  std::transform(uppercase_identity.image_name.begin(), uppercase_identity.image_name.end(),
                 uppercase_identity.image_name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  const auto queried = backend->query(uppercase_identity);
  INFO(queried.message);
  REQUIRE(queried.ok);
  CHECK(queried.value.state == ManagedProcessState::Running);
  CHECK(queried.value.pid == started.snapshot.pid);
  CHECK(queried.value.session_id == started.snapshot.session_id);
  CHECK(queried.value.instance_token == started.snapshot.instance_token);
  CHECK(queried.value.canonical_image_name == started.snapshot.canonical_image_name);
  REQUIRE(waitForFixtureTopLevelWindow(started.snapshot.pid));

  request.operation = SystemOperation::Stop;
  const auto stopped = controller.execute(request);
  INFO(stopped.phase << ":" << stopped.message);
  REQUIRE(stopped.ok);
  CHECK(stopped.snapshot.state == ManagedProcessState::NotRunning);

  const auto after = backend->query(request.identity);
  REQUIRE(after.ok);
  CHECK(after.value.state == ManagedProcessState::NotRunning);

  const auto final_cleanup = cleanup.cleanup();
  INFO(final_cleanup.native_error << ":" << final_cleanup.message);
  REQUIRE(final_cleanup.ok);
  const auto cleaned = backend->query(request.identity);
  REQUIRE(cleaned.ok);
  CHECK(cleaned.value.state == ManagedProcessState::NotRunning);
}

TEST_CASE("Windows managed process startup does not hide a fixed backend delay") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 1;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);

  INFO(started.phase << ":" << started.message << ", duration_ms=" << started.duration_ms);
  CHECK_FALSE(started.ok);
  CHECK(started.error == ManagedProcessError::Timeout);
  CHECK(started.phase == "start_poll");
  CHECK(started.snapshot.state == ManagedProcessState::NotRunning);
  CHECK(started.duration_ms < 250);

  const auto after = backend->query(request.identity);
  REQUIRE(after.ok);
  CHECK(after.value.state == ManagedProcessState::NotRunning);

  const auto final_cleanup = cleanup.cleanup();
  INFO(final_cleanup.native_error << ":" << final_cleanup.message);
  REQUIRE(final_cleanup.ok);
  const auto cleaned = backend->query(request.identity);
  REQUIRE(cleaned.ok);
  CHECK(cleaned.value.state == ManagedProcessState::NotRunning);
}

TEST_CASE("Windows managed process query does not match a different extension") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams alternate = params(SystemOperation::Start);
  alternate.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE_ALT;
  alternate.working_directory = std::filesystem::path(alternate.executable).parent_path().string();
  alternate.identity.image_name = std::filesystem::path(alternate.executable).filename().string();
  alternate.startup_timeout_ms = 3000;
  alternate.stop_timeout_ms = 3000;
  alternate.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, alternate.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);
  ManagedProcessFixtureCleanup cleanup(*backend, alternate.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(alternate);
  INFO(started.phase << ":" << started.message);
  REQUIRE(started.ok);
  REQUIRE(started.changed);

  ManagedProcessIdentity exe_identity;
  exe_identity.image_name = "managed_process_fixture.exe";
  const auto wrong_extension = backend->query(exe_identity);
  INFO(wrong_extension.message);
  REQUIRE(wrong_extension.ok);
  CHECK(wrong_extension.value.state == ManagedProcessState::NotRunning);

  const auto final_cleanup = cleanup.cleanup();
  INFO(final_cleanup.native_error << ":" << final_cleanup.message);
  REQUIRE(final_cleanup.ok);
  const auto cleaned = backend->query(alternate.identity);
  REQUIRE(cleaned.ok);
  CHECK(cleaned.value.state == ManagedProcessState::NotRunning);
}

TEST_CASE("Windows fixture guard cleans a started process on early scope exit") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;

  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  INFO(setup_cleanup.message);
  REQUIRE(setup_cleanup.ok);

  {
    ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
    ManagedProcessController controller(*backend, std::chrono::milliseconds(20));
    const auto started = controller.execute(request);
    INFO(started.phase << ":" << started.message);
    REQUIRE(started.ok);
    REQUIRE(started.changed);
    REQUIRE(started.snapshot.state == ManagedProcessState::Running);
  }

  const auto after_scope = backend->query(request.identity);
  INFO(after_scope.message);
  REQUIRE(after_scope.ok);
  CHECK(after_scope.value.state == ManagedProcessState::NotRunning);
}
#endif
