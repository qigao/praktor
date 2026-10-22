#include "praktor_test.hpp"
#include "executors/managed_process_executor.hpp"
#include "system/managed_process.hpp"


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
  return {ManagedProcessState::NotRunning, 0, 0, 0, {}};
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

[[maybe_unused]] static void test_managed_process_status_reports_the_queried_snapshot_1() {
  FakeManagedProcessBackend backend;
  backend.query_action = [] { return success(running()); };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(params(SystemOperation::Status));

  check((result.ok));
  check((result.snapshot.state == ManagedProcessState::Running));
  check((result.snapshot.pid == kFixturePid));
  check((!(result.changed)));
  check((backend.queried_identities.size() == 1));
}

spec("managed process status reports the queried snapshot") {
    it("complete case") { test_managed_process_status_reports_the_queried_snapshot_1(); }
}

[[maybe_unused]] static void test_managed_process_start_and_stop_are_idempotent_at_their_target_state_2(int section_index) {
  if (section_index == 0) {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(running()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));

    const auto result = controller.execute(params(SystemOperation::Start));

    check((result.ok));
    check((result.snapshot.state == ManagedProcessState::Running));
    check((!(result.changed)));
    check((backend.started_params.empty()));
  }

  if (section_index == 1) {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(notRunning()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));

    const auto result = controller.execute(params(SystemOperation::Stop));

    check((result.ok));
    check((result.snapshot.state == ManagedProcessState::NotRunning));
    check((!(result.changed)));
    check((backend.stopped_snapshots.empty()));
  }
}

spec("managed process start and stop are idempotent at their target state") {
    it("start when already running") { test_managed_process_start_and_stop_are_idempotent_at_their_target_state_2(0); }
    it("stop when already stopped") { test_managed_process_start_and_stop_are_idempotent_at_their_target_state_2(1); }
}

[[maybe_unused]] static void test_managed_process_start_waits_for_identity_query_to_report_running_3() {
  FakeManagedProcessBackend backend;
  std::size_t query_count = 0;
  backend.query_action = [&] {
    ++query_count;
    return success(query_count < 3 ? notRunning() : running());
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto request = params(SystemOperation::Start);
  const auto result = controller.execute(request);

  info("%s", praktor_test::message([&](std::ostream& out) { out << result.message; }).c_str());
  check((result.ok));
  check((result.snapshot.state == ManagedProcessState::Running));
  check((result.changed));
  check((backend.started_params.size() == 1));
  check((backend.started_params.front().arguments == request.arguments));
  check((query_count == 3));
}

spec("managed process start waits for identity query to report running") {
    it("complete case") { test_managed_process_start_waits_for_identity_query_to_report_running_3(); }
}

[[maybe_unused]] static void test_managed_process_stop_requests_graceful_close_and_confirms_stopped_4() {
  FakeManagedProcessBackend backend;
  std::size_t query_count = 0;
  backend.query_action = [&] {
    ++query_count;
    return success(query_count < 3 ? running() : notRunning());
  };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(params(SystemOperation::Stop));

  info("%s", praktor_test::message([&](std::ostream& out) { out << result.message; }).c_str());
  check((result.ok));
  check((result.snapshot.state == ManagedProcessState::NotRunning));
  check((result.changed));
  check((backend.stopped_snapshots.size() == 1));
  check((backend.stopped_snapshots.front().pid == kFixturePid));
  check((backend.terminated_snapshots.empty()));
}

spec("managed process stop requests graceful close and confirms stopped") {
    it("complete case") { test_managed_process_stop_requests_graceful_close_and_confirms_stopped_4(); }
}

[[maybe_unused]] static void test_managed_process_restart_confirms_stop_before_starting_5() {
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

  info("%s", praktor_test::message([&](std::ostream& out) { out << result.message; }).c_str());
  check((result.ok));
  check((result.snapshot.state == ManagedProcessState::Running));
  check((result.snapshot.pid == 4343));
  check((result.changed));
  check((backend.stopped_snapshots.size() == 1));
  check((backend.started_params.size() == 1));
  check((query_count == 4));
}

spec("managed process restart confirms stop before starting") {
    it("complete case") { test_managed_process_restart_confirms_stop_before_starting_5(); }
}

[[maybe_unused]] static void test_managed_process_startup_timeout_is_bounded_by_the_startup_deadline_6() {
  FakeManagedProcessBackend backend;
  backend.query_action = [] { return success(notRunning()); };
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));
  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;

  const auto result = controller.execute(request);

  check((!(result.ok)));
  check((result.error == ManagedProcessError::Timeout));
  check((result.phase == "start_poll"));
  check((result.snapshot.state == ManagedProcessState::NotRunning));
  check((result.changed));
  check((backend.started_params.size() == 1));
}

spec("managed process startup timeout is bounded by the startup deadline") {
    it("complete case") { test_managed_process_startup_timeout_is_bounded_by_the_startup_deadline_6(); }
}

[[maybe_unused]] static void test_managed_process_backend_start_time_consumes_the_startup_budget_7() {
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

  check((!(result.ok)));
  check((result.error == ManagedProcessError::Timeout));
  check((result.phase == "start_poll"));
  check((result.duration_ms >= 15));
  check((backend.started_params.size() == 1));
}

spec("managed process backend start time consumes the startup budget") {
    it("complete case") { test_managed_process_backend_start_time_consumes_the_startup_budget_7(); }
}

[[maybe_unused]] static void test_startup_timeout_compensates_the_exact_started_instance_8() {
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
    check((snapshot.pid == started.pid));
    check((snapshot.session_id == started.session_id));
    check((snapshot.instance_token == started.instance_token));
    check((snapshot.canonical_image_name == started.canonical_image_name));
    terminated = true;
    return commandSuccess();
  };

  auto request = params(SystemOperation::Start);
  request.startup_timeout_ms = 5;
  request.stop_timeout_ms = 25;
  ManagedProcessController controller(backend, std::chrono::milliseconds(1));

  const auto result = controller.execute(request);

  check((!(result.ok)));
  check((result.error == ManagedProcessError::Timeout));
  check((result.phase == "start_poll"));
  check((result.snapshot.state == ManagedProcessState::NotRunning));
  check((result.snapshot.pid == 0));
  check((backend.terminated_snapshots.size() == 1));
}

spec("startup timeout compensates the exact started instance") {
    it("complete case") { test_startup_timeout_compensates_the_exact_started_instance_8(); }
}

[[maybe_unused]] static void test_startup_timeout_reports_compensation_command_failure_9() {
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

  check((!(result.ok)));
  check((result.error == ManagedProcessError::BackendFailure));
  check((result.phase == "start_timeout_terminate"));
  check((result.native_error == 5));
  check((result.snapshot.state == ManagedProcessState::Running));
  check((result.snapshot.pid == started.pid));
  check((result.snapshot.instance_token == started.instance_token));
  check((backend.terminated_snapshots.size() == 1));
}

spec("startup timeout reports compensation command failure") {
    it("complete case") { test_startup_timeout_reports_compensation_command_failure_9(); }
}

[[maybe_unused]] static void test_startup_timeout_reports_compensation_confirmation_timeout_10() {
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

  check((!(result.ok)));
  check((result.error == ManagedProcessError::Timeout));
  check((result.phase == "start_timeout_cleanup_poll"));
  check((result.snapshot.state == ManagedProcessState::Running));
  check((result.snapshot.pid == started.pid));
  check((result.snapshot.instance_token == started.instance_token));
  check((backend.terminated_snapshots.size() == 1));
}

spec("startup timeout reports compensation confirmation timeout") {
    it("complete case") { test_startup_timeout_reports_compensation_confirmation_timeout_10(); }
}

[[maybe_unused]] static void test_managed_process_stop_timeout_does_not_terminate_unless_explicitly_enabled_11(int section_index) {
  if (section_index == 0) {
    FakeManagedProcessBackend backend;
    backend.query_action = [] { return success(running()); };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));
    auto request = params(SystemOperation::Stop);
    request.stop_timeout_ms = 5;

    const auto result = controller.execute(request);

    check((!(result.ok)));
    check((result.error == ManagedProcessError::Timeout));
    check((result.phase == "stop_poll"));
    check((result.snapshot.state == ManagedProcessState::Running));
    check((backend.stopped_snapshots.size() == 1));
    check((backend.terminated_snapshots.empty()));
  }

  if (section_index == 1) {
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

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.message; }).c_str());
    check((result.ok));
    check((result.error == ManagedProcessError::None));
    check((result.phase.empty()));
    check((result.message.empty()));
    check((result.snapshot.state == ManagedProcessState::NotRunning));
    check((result.changed));
    check((backend.terminated_snapshots.size() == 1));
    check((backend.terminated_snapshots.front().pid == kFixturePid));
  }
}

spec("managed process stop timeout does not terminate unless explicitly enabled") {
    it("force termination disabled") { test_managed_process_stop_timeout_does_not_terminate_unless_explicitly_enabled_11(0); }
    it("force termination enabled") { test_managed_process_stop_timeout_does_not_terminate_unless_explicitly_enabled_11(1); }
}

spec("C process control bridge preserves ownership and exceptions") {
  it("keeps the last successful image alive after a failed query") {
    FakeManagedProcessBackend backend;
    const std::string image(4096, 'x');
    std::size_t queries = 0;
    backend.query_action = [&]() -> ManagedProcessResult<ManagedProcessSnapshot> {
      if (++queries > 1) return {false, {}, 5, "query denied"};
      auto snapshot = running(); snapshot.canonical_image_name = image;
      return success(std::move(snapshot));
    };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));
    const auto result = controller.execute(params(SystemOperation::Stop));
    check((!result.ok && result.changed && result.native_error == 5));
    check((result.phase == "stop_poll" && result.snapshot.canonical_image_name == image));
    check((result.snapshot.pid == kFixturePid && backend.terminated_snapshots.empty()));
  }
  it("rethrows backend exceptions after returning across the C boundary") {
    FakeManagedProcessBackend backend;
    backend.start_action = []() -> ManagedProcessResult<ManagedProcessSnapshot> {
      throw std::runtime_error("backend admission exception");
    };
    ManagedProcessController controller(backend, std::chrono::milliseconds(1));
    check((praktor_test::throws_containing(
        [&] { controller.execute(params(SystemOperation::Start)); },
        {"backend admission exception"})));
    check((backend.started_params.size() == 1 && backend.terminated_snapshots.empty()));
  }
}

[[maybe_unused]] static void test_managed_process_executor_maps_outputs_and_structured_failures_12(int section_index) {
  if (section_index == 0) {
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

    check((result.success));
    check((context.getValueByPath("tasks.__root__.outputs.image_name")
              .as<std::string>() == "managed_process_fixture.exe"));
    check((context.getValueByPath("tasks.__root__.outputs.operation")
              .as<std::string>() == "status"));
    check((context.getValueByPath("tasks.__root__.outputs.state")
              .as<std::string>() == "running"));
    check((context.getValueByPath("tasks.__root__.outputs.pid").as<int64_t>() ==
          kFixturePid));
    check((!(context.getValueByPath("tasks.__root__.outputs.changed")
                    .as<bool>())));
    check((context.getValueByPath("tasks.__root__.outputs.duration_ms")
              .as<int64_t>() >= 0));
  }

  if (section_index == 1) {
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

    check((!(result.success)));
    check((result.error_code == "service_state_failed"));
    check((result.error_phase == "query"));
    check((!(result.error_details.contains("native_error"))));
  }

  if (section_index == 2) {
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

    check((!(result.success)));
    check((result.error_code == "unsupported_platform"));
    check((result.error_phase == "query"));
  }
}

spec("managed process executor maps outputs and structured failures") {
    it("successful status") { test_managed_process_executor_maps_outputs_and_structured_failures_12(0); }
    it("backend failure") { test_managed_process_executor_maps_outputs_and_structured_failures_12(1); }
    it("unsupported platform") { test_managed_process_executor_maps_outputs_and_structured_failures_12(2); }
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

[[maybe_unused]] static void test_windows_managed_process_identity_matching_is_current_session_exact_basename_13() {
  using Praktor::System::WinDetail::matchesManagedProcessCandidate;

  const auto case_insensitive =
      matchesManagedProcessCandidate("C:/configured/WORKER.EXE", "C:/runtime/worker.exe", 7, 7);
  check((case_insensitive.ok));
  check((case_insensitive.value));

  const auto different_extension =
      matchesManagedProcessCandidate("worker.exe", "C:/runtime/worker.com", 7, 7);
  check((different_extension.ok));
  check((!(different_extension.value)));

  const auto different_session =
      matchesManagedProcessCandidate("worker.exe", "C:/runtime/WORKER.EXE", 8, 7);
  check((different_session.ok));
  check((!(different_session.value)));
}

spec("Windows managed process identity matching is current-session exact basename") {
    it("complete case") { test_windows_managed_process_identity_matching_is_current_session_exact_basename_13(); }
}

[[maybe_unused]] static void test_windows_fixture_cleanup_reports_backend_failures_14(int section_index) {
  ManagedProcessIdentity identity;
  identity.image_name = "managed_process_fixture.exe";

  if (section_index == 0) {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      return ManagedProcessResult<ManagedProcessSnapshot>{
          false, {}, 5, "fixture cleanup query failed"};
    };

    const auto cleaned = cleanupManagedProcessFixture(
        backend, identity, std::chrono::milliseconds(10), std::chrono::milliseconds(1));

    check((!(cleaned.ok)));
    check((cleaned.native_error == 5));
  }

  if (section_index == 1) {
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

    check((!(cleaned.ok)));
    check((cleaned.native_error == 6));
  }

  if (section_index == 2) {
    FakeManagedProcessBackend backend;
    backend.query_action = [] {
      ManagedProcessSnapshot running;
      running.state = ManagedProcessState::Running;
      running.pid = kFixturePid;
      return ManagedProcessResult<ManagedProcessSnapshot>{true, running, 0, {}};
    };

    const auto cleaned = cleanupManagedProcessFixture(
        backend, identity, std::chrono::milliseconds(0), std::chrono::milliseconds(0));

    check((!(cleaned.ok)));
    check((cleaned.native_error == ERROR_TIMEOUT));
  }
}

spec("Windows fixture cleanup reports backend failures") {
    it("query failure") { test_windows_fixture_cleanup_reports_backend_failures_14(0); }
    it("termination failure") { test_windows_fixture_cleanup_reports_backend_failures_14(1); }
    it("timeout") { test_windows_fixture_cleanup_reports_backend_failures_14(2); }
}

[[maybe_unused]] static void test_windows_action_validation_rejects_changed_process_evidence_15() {
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
  check((token_match.ok));
  check((!(token_match.value)));

  auto changed_image = expected;
  changed_image.canonical_image_name = "worker.com";
  const auto image_match = matchesManagedProcessSnapshot(expected, changed_image, 7);
  check((image_match.ok));
  check((!(image_match.value)));

  auto changed_session = expected;
  ++changed_session.session_id;
  const auto session_match = matchesManagedProcessSnapshot(expected, changed_session, 7);
  check((session_match.ok));
  check((!(session_match.value)));
}

spec("Windows action validation rejects changed process evidence") {
    it("complete case") { test_windows_action_validation_rejects_changed_process_evidence_15(); }
}

[[maybe_unused]] static void test_windows_stop_rejects_a_stale_managed_process_instance_token_16() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message; }).c_str());
  check((started.ok));
  check((started.changed));

  auto stale = started.snapshot;
  ++stale.instance_token;
  const auto stopped = backend->requestStop(stale);
  check((!(stopped.ok)));

  const auto after = backend->query(request.identity);
  check((after.ok));
  check((after.value.state == ManagedProcessState::Running));
  check((after.value.instance_token == started.snapshot.instance_token));

  const auto final_cleanup = cleanup.cleanup();
  info("%s", praktor_test::message([&](std::ostream& out) { out << final_cleanup.native_error << ":" << final_cleanup.message; }).c_str());
  check((final_cleanup.ok));
  const auto cleaned = backend->query(request.identity);
  check((cleaned.ok));
  check((cleaned.value.state == ManagedProcessState::NotRunning));
}

spec("Windows stop rejects a stale managed process instance token") {
    it("complete case") { test_windows_stop_rejects_a_stale_managed_process_instance_token_16(); }
}

[[maybe_unused]] static void test_windows_terminate_rejects_changed_canonical_process_identity_17() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message; }).c_str());
  check((started.ok));
  check((started.changed));

  auto changed_identity = started.snapshot;
  changed_identity.canonical_image_name = "managed_process_fixture.com";
  const auto terminated = backend->terminate(changed_identity);
  check((!(terminated.ok)));

  const auto after = backend->query(request.identity);
  check((after.ok));
  check((after.value.state == ManagedProcessState::Running));
  check((after.value.instance_token == started.snapshot.instance_token));

  const auto final_cleanup = cleanup.cleanup();
  info("%s", praktor_test::message([&](std::ostream& out) { out << final_cleanup.native_error << ":" << final_cleanup.message; }).c_str());
  check((final_cleanup.ok));
  const auto cleaned = backend->query(request.identity);
  check((cleaned.ok));
  check((cleaned.value.state == ManagedProcessState::NotRunning));
}

spec("Windows terminate rejects changed canonical process identity") {
    it("complete case") { test_windows_terminate_rejects_changed_canonical_process_identity_17(); }
}

[[maybe_unused]] static void test_windows_managed_process_backend_starts_queries_and_gracefully_stops_fixture_18() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = false;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);
  info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message; }).c_str());
  check((started.ok));
  check((started.snapshot.state == ManagedProcessState::Running));
  check((started.changed));
  DWORD current_session_id = 0;
  check((ProcessIdToSessionId(GetCurrentProcessId(), &current_session_id)));
  check((started.snapshot.session_id == current_session_id));
  check((started.snapshot.instance_token != 0));
  check((started.snapshot.canonical_image_name == request.identity.image_name));

  ManagedProcessIdentity uppercase_identity = request.identity;
  std::transform(uppercase_identity.image_name.begin(), uppercase_identity.image_name.end(),
                 uppercase_identity.image_name.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  const auto queried = backend->query(uppercase_identity);
  info("%s", praktor_test::message([&](std::ostream& out) { out << queried.message; }).c_str());
  check((queried.ok));
  check((queried.value.state == ManagedProcessState::Running));
  check((queried.value.pid == started.snapshot.pid));
  check((queried.value.session_id == started.snapshot.session_id));
  check((queried.value.instance_token == started.snapshot.instance_token));
  check((queried.value.canonical_image_name == started.snapshot.canonical_image_name));
  check((waitForFixtureTopLevelWindow(started.snapshot.pid)));

  request.operation = SystemOperation::Stop;
  const auto stopped = controller.execute(request);
  info("%s", praktor_test::message([&](std::ostream& out) { out << stopped.phase << ":" << stopped.message; }).c_str());
  check((stopped.ok));
  check((stopped.snapshot.state == ManagedProcessState::NotRunning));

  const auto after = backend->query(request.identity);
  check((after.ok));
  check((after.value.state == ManagedProcessState::NotRunning));

  const auto final_cleanup = cleanup.cleanup();
  info("%s", praktor_test::message([&](std::ostream& out) { out << final_cleanup.native_error << ":" << final_cleanup.message; }).c_str());
  check((final_cleanup.ok));
  const auto cleaned = backend->query(request.identity);
  check((cleaned.ok));
  check((cleaned.value.state == ManagedProcessState::NotRunning));
}

spec("Windows managed process backend starts queries and gracefully stops fixture") {
    it("complete case") { test_windows_managed_process_backend_starts_queries_and_gracefully_stops_fixture_18(); }
}

[[maybe_unused]] static void test_windows_managed_process_startup_does_not_hide_a_fixed_backend_delay_19() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 1;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));
  ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(request);

  info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message << ", duration_ms=" << started.duration_ms; }).c_str());
  check((!(started.ok)));
  check((started.error == ManagedProcessError::Timeout));
  check((started.phase == "start_poll"));
  check((started.snapshot.state == ManagedProcessState::NotRunning));
  check((started.duration_ms < 250));

  const auto after = backend->query(request.identity);
  check((after.ok));
  check((after.value.state == ManagedProcessState::NotRunning));

  const auto final_cleanup = cleanup.cleanup();
  info("%s", praktor_test::message([&](std::ostream& out) { out << final_cleanup.native_error << ":" << final_cleanup.message; }).c_str());
  check((final_cleanup.ok));
  const auto cleaned = backend->query(request.identity);
  check((cleaned.ok));
  check((cleaned.value.state == ManagedProcessState::NotRunning));
}

spec("Windows managed process startup does not hide a fixed backend delay") {
    it("complete case") { test_windows_managed_process_startup_does_not_hide_a_fixed_backend_delay_19(); }
}

[[maybe_unused]] static void test_windows_managed_process_query_does_not_match_a_different_extension_20() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams alternate = params(SystemOperation::Start);
  alternate.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE_ALT;
  alternate.working_directory = std::filesystem::path(alternate.executable).parent_path().string();
  alternate.identity.image_name = std::filesystem::path(alternate.executable).filename().string();
  alternate.startup_timeout_ms = 3000;
  alternate.stop_timeout_ms = 3000;
  alternate.force_terminate = true;
  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, alternate.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));
  ManagedProcessFixtureCleanup cleanup(*backend, alternate.identity);
  ManagedProcessController controller(*backend, std::chrono::milliseconds(20));

  const auto started = controller.execute(alternate);
  info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message; }).c_str());
  check((started.ok));
  check((started.changed));

  ManagedProcessIdentity exe_identity;
  exe_identity.image_name = "managed_process_fixture.exe";
  const auto wrong_extension = backend->query(exe_identity);
  info("%s", praktor_test::message([&](std::ostream& out) { out << wrong_extension.message; }).c_str());
  check((wrong_extension.ok));
  check((wrong_extension.value.state == ManagedProcessState::NotRunning));

  const auto final_cleanup = cleanup.cleanup();
  info("%s", praktor_test::message([&](std::ostream& out) { out << final_cleanup.native_error << ":" << final_cleanup.message; }).c_str());
  check((final_cleanup.ok));
  const auto cleaned = backend->query(alternate.identity);
  check((cleaned.ok));
  check((cleaned.value.state == ManagedProcessState::NotRunning));
}

spec("Windows managed process query does not match a different extension") {
    it("complete case") { test_windows_managed_process_query_does_not_match_a_different_extension_20(); }
}

[[maybe_unused]] static void test_windows_fixture_guard_cleans_a_started_process_on_early_scope_exit_21() {
  auto backend = Praktor::System::createManagedProcessBackend();
  check((backend != nullptr));

  ManagedProcessParams request = params(SystemOperation::Start);
  request.executable = PRAKTOR_MANAGED_PROCESS_FIXTURE;
  request.working_directory = std::filesystem::path(request.executable).parent_path().string();
  request.identity.image_name = std::filesystem::path(request.executable).filename().string();
  request.startup_timeout_ms = 3000;
  request.stop_timeout_ms = 3000;
  request.force_terminate = true;

  const auto setup_cleanup = cleanupManagedProcessFixture(
      *backend, request.identity, std::chrono::seconds(3), std::chrono::milliseconds(20));
  info("%s", praktor_test::message([&](std::ostream& out) { out << setup_cleanup.message; }).c_str());
  check((setup_cleanup.ok));

  {
    ManagedProcessFixtureCleanup cleanup(*backend, request.identity);
    ManagedProcessController controller(*backend, std::chrono::milliseconds(20));
    const auto started = controller.execute(request);
    info("%s", praktor_test::message([&](std::ostream& out) { out << started.phase << ":" << started.message; }).c_str());
    check((started.ok));
    check((started.changed));
    check((started.snapshot.state == ManagedProcessState::Running));
  }

  const auto after_scope = backend->query(request.identity);
  info("%s", praktor_test::message([&](std::ostream& out) { out << after_scope.message; }).c_str());
  check((after_scope.ok));
  check((after_scope.value.state == ManagedProcessState::NotRunning));
}

spec("Windows fixture guard cleans a started process on early scope exit") {
    it("complete case") { test_windows_fixture_guard_cleans_a_started_process_on_early_scope_exit_21(); }
}
#endif
