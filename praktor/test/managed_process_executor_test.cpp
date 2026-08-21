#include "executors/managed_process_executor.hpp"
#include "system/managed_process.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
  return {ManagedProcessState::Running, pid};
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

  ManagedProcessResult<std::uint32_t> start(
      const ManagedProcessParams& params) override {
    started_params.push_back(params);
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
    return terminate_result;
  }

  std::function<ManagedProcessResult<ManagedProcessSnapshot>()> query_action;
  ManagedProcessResult<std::uint32_t> start_result = success(kFixturePid);
  ManagedProcessCommandResult stop_result = commandSuccess();
  ManagedProcessCommandResult terminate_result = commandSuccess();
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

class ManagedProcessFixtureCleanup {
public:
  ManagedProcessFixtureCleanup(IManagedProcessBackend& backend,
                               ManagedProcessIdentity identity)
      : backend_(backend), identity_(std::move(identity)) {
    cleanup();
  }

  ~ManagedProcessFixtureCleanup() { cleanup(); }

  ManagedProcessFixtureCleanup(const ManagedProcessFixtureCleanup&) = delete;
  ManagedProcessFixtureCleanup& operator=(const ManagedProcessFixtureCleanup&) = delete;

private:
  void cleanup() noexcept {
    constexpr auto cleanup_timeout = std::chrono::seconds(3);
    constexpr auto cleanup_poll_interval = std::chrono::milliseconds(20);
    const auto deadline = std::chrono::steady_clock::now() + cleanup_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      auto queried = backend_.query(identity_);
      if (!queried.ok || queried.value.state == ManagedProcessState::NotRunning) {
        return;
      }
      static_cast<void>(backend_.terminate(queried.value));
      std::this_thread::sleep_for(cleanup_poll_interval);
    }
  }

  IManagedProcessBackend& backend_;
  ManagedProcessIdentity identity_;
};

}  // namespace

TEST_CASE("Windows managed process backend starts queries and gracefully stops fixture") {
  auto backend = Praktor::System::createManagedProcessBackend();
  REQUIRE(backend != nullptr);

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory =
      std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name =
      std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = false;
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  INFO(started.phase << ":" << started.message);
  REQUIRE(started.ok);
  REQUIRE(started.snapshot.state == ManagedProcessState::Running);

  const auto queried = backend->query(request.identity);
  INFO(queried.message);
  REQUIRE(queried.ok);
  CHECK(queried.value.state == ManagedProcessState::Running);
  CHECK(queried.value.pid == started.snapshot.pid);

  request.operation = SystemOperation::Stop;
  const auto stopped = controller.execute(request);
  INFO(stopped.phase << ":" << stopped.message);
  REQUIRE(stopped.ok);
  CHECK(stopped.snapshot.state == ManagedProcessState::NotRunning);

  const auto after = backend->query(request.identity);
  REQUIRE(after.ok);
  CHECK(after.value.state == ManagedProcessState::NotRunning);
}
#endif
