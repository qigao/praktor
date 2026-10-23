#include "praktor_test.hpp"
#include "executors/service_executor.hpp"
#include "system/service_controller.hpp"


#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Praktor::Shell::ProcessSpec;
using Praktor::Shell::ProcessState;
using Praktor::Shell::ShellResult;
using Praktor::System::IProcessRunner;
using Praktor::System::ServiceCommandProfile;
using Praktor::System::ServiceController;
using Praktor::System::ServiceExecutionResult;
using Praktor::System::ServiceProfileRegistry;

ShellResult exited(std::string output = {}, int exit_code = 0) {
  ShellResult result;
  result.state = ProcessState::Exited;
  result.exit_code = exit_code;
  result.stdout_output = std::move(output);
  return result;
}

ShellResult serviceState(int state) {
  return exited("SERVICE_NAME: fixture\n        STATE              : " +
                std::to_string(state) + "  LOCALIZED_TEXT\n");
}

class ScriptedProcessRunner final : public IProcessRunner {
public:
  explicit ScriptedProcessRunner(std::vector<ShellResult> results,
                                 bool repeat_last = false)
      : results_(std::move(results)), repeat_last_(repeat_last) {}

  ShellResult run(const ProcessSpec& spec) override {
    calls.push_back(spec);
    if (next_ >= results_.size()) {
      if (repeat_last_ && !results_.empty()) {
        return results_.back();
      }
      throw std::runtime_error("fake runner script exhausted");
    }
    if (repeat_last_ && next_ + 1 == results_.size()) {
      return results_[next_];
    }
    return results_[next_++];
  }

  std::vector<ProcessSpec> calls;

private:
  std::vector<ShellResult> results_;
  std::size_t next_ = 0;
  bool repeat_last_ = false;
};

ServiceProfileRegistry profiles() {
  ServiceProfileRegistry registry;
  ServiceCommandProfile profile;
  profile.name = "fixture";
  profile.program = "fake-service-controller";
  registry.registerProfile(std::move(profile));
  return registry;
}

ServiceParams params(SystemOperation operation) {
  ServiceParams value;
  value.operation = operation;
  value.name = "Retro Camera Service";
  value.profile = "fixture";
  value.arguments = {"--mode", "safe value"};
  value.timeout_ms = 1000;
  value.poll_interval_ms = 1;
  return value;
}

void checkArgs(const ProcessSpec& call, const StrList& expected) {
  check((call.program == "fake-service-controller"));
  check((call.args == expected));
}

}  // namespace

[[maybe_unused]] static void test_service_controller_parses_numeric_windows_scm_states_1(std::size_t row_0) {
  auto registry = profiles();

  const auto expected = std::array{std::tuple<int, std::string>{1, "stopped"}, std::tuple<int, std::string>{2, "start_pending"}, std::tuple<int, std::string>{3, "stop_pending"}, std::tuple<int, std::string>{4, "running"}}.at(row_0);
  ScriptedProcessRunner runner({serviceState(std::get<0>(expected))});
  ServiceController controller(runner, registry);

  const ServiceExecutionResult result = controller.execute(params(SystemOperation::Status));

  check((result.task_result.success));
  check((result.state == std::get<1>(expected)));
  check((!(result.changed)));
  check((runner.calls.size() == 1));
  checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
}

spec("service controller parses numeric Windows SCM states") {
    for (std::size_t row_0 = 0; row_0 < 4; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_service_controller_parses_numeric_windows_scm_states_1(row_0); }
    }
}

[[maybe_unused]] static void test_service_start_and_stop_are_idempotent_at_their_target_state_2(int section_index) {
  auto registry = profiles();

  if (section_index == 0) {
    ScriptedProcessRunner runner({serviceState(4)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Start));

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message; }).c_str());
    check((result.task_result.success));
    check((result.state == "running"));
    check((!(result.changed)));
    check((runner.calls.size() == 1));
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
  }

  if (section_index == 1) {
    ScriptedProcessRunner runner({serviceState(1)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Stop));

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message; }).c_str());
    check((result.task_result.success));
    check((result.state == "stopped"));
    check((!(result.changed)));
    check((runner.calls.size() == 1));
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
  }
}

spec("service start and stop are idempotent at their target state") {
    it("start when already running") { test_service_start_and_stop_are_idempotent_at_their_target_state_2(0); }
    it("stop when already stopped") { test_service_start_and_stop_are_idempotent_at_their_target_state_2(1); }
}

[[maybe_unused]] static void test_service_transitions_preserve_argv_entries_and_mutate_once_3(int section_index) {
  auto registry = profiles();

  if (section_index == 0) {
    ScriptedProcessRunner runner(
        {serviceState(1), exited(), serviceState(2), serviceState(4)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Start));

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message; }).c_str());
    check((result.task_result.success));
    check((result.state == "running"));
    check((result.changed));
    check((runner.calls.size() == 4));
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[1],
              {"start", "Retro Camera Service", "--mode", "safe value"});
    checkArgs(runner.calls[2], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[3], {"query", "Retro Camera Service"});
  }

  if (section_index == 1) {
    ScriptedProcessRunner runner(
        {serviceState(4), exited(), serviceState(3), serviceState(1)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Stop));

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message; }).c_str());
    check((result.task_result.success));
    check((result.state == "stopped"));
    check((result.changed));
    check((runner.calls.size() == 4));
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[1],
              {"stop", "Retro Camera Service", "--mode", "safe value"});
    checkArgs(runner.calls[2], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[3], {"query", "Retro Camera Service"});
  }
}

spec("service transitions preserve argv entries and mutate once") {
    it("start") { test_service_transitions_preserve_argv_entries_and_mutate_once_3(0); }
    it("stop") { test_service_transitions_preserve_argv_entries_and_mutate_once_3(1); }
}

[[maybe_unused]] static void test_service_restart_confirms_stop_before_starting_4() {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4), exited(), serviceState(3),
                                serviceState(1), exited(), serviceState(2),
                                serviceState(4)});
  ServiceController controller(runner, registry);

  const auto result = controller.execute(params(SystemOperation::Restart));

  info("%s", praktor_test::message([&](std::ostream& out) { out << result.task_result.error_code << ":" << result.task_result.error_phase
                                    << ":" << result.task_result.error_message; }).c_str());
  check((result.task_result.success));
  check((result.state == "running"));
  check((result.changed));
  check((runner.calls.size() == 7));
  checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
  checkArgs(runner.calls[1],
            {"stop", "Retro Camera Service", "--mode", "safe value"});
  checkArgs(runner.calls[2], {"query", "Retro Camera Service"});
  checkArgs(runner.calls[3], {"query", "Retro Camera Service"});
  checkArgs(runner.calls[4],
            {"start", "Retro Camera Service", "--mode", "safe value"});
  checkArgs(runner.calls[5], {"query", "Retro Camera Service"});
  checkArgs(runner.calls[6], {"query", "Retro Camera Service"});
}

spec("service restart confirms stop before starting") {
    it("complete case") { test_service_restart_confirms_stop_before_starting_4(); }
}

[[maybe_unused]] static void test_service_failures_distinguish_profile_state_spawn_and_timeout_5(int section_index) {
  auto registry = profiles();

  if (section_index == 0) {
    ScriptedProcessRunner runner({});
    ServiceController controller(runner, registry);
    auto request = params(SystemOperation::Status);
    request.profile = "missing";

    const auto result = controller.execute(request);

    check((!(result.task_result.success)));
    check((result.task_result.error_code == "service_state_failed"));
    check((runner.calls.empty()));
  }

  if (section_index == 1) {
    ScriptedProcessRunner runner({exited("STATE : RUNNING\n")});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    check((!(result.task_result.success)));
    check((result.task_result.error_code == "service_state_failed"));
  }

  if (section_index == 2) {
    ScriptedProcessRunner runner({exited("STATE : 4oops\n")});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    check((!(result.task_result.success)));
    check((result.task_result.error_code == "service_state_failed"));
  }

  if (section_index == 3) {
    ShellResult spawn_failure;
    spawn_failure.state = ProcessState::SpawnFailed;
    spawn_failure.exit_code = -1;
    spawn_failure.stderr_output = "cannot spawn";
    ScriptedProcessRunner runner({std::move(spawn_failure)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    check((!(result.task_result.success)));
    check((result.task_result.error_code == "process_spawn_failed"));
  }

  if (section_index == 4) {
    ScriptedProcessRunner runner(
        {serviceState(1), exited(), serviceState(2)}, true);
    ServiceController controller(runner, registry);
    auto request = params(SystemOperation::Start);
    request.timeout_ms = 100;

    const auto result = controller.execute(request);

    check((!(result.task_result.success)));
    check((result.task_result.error_code == "timeout"));
    check((result.state == "start_pending"));
    check((runner.calls.size() >= 3));
    checkArgs(runner.calls[1],
              {"start", "Retro Camera Service", "--mode", "safe value"});
  }
}

spec("service failures distinguish profile state spawn and timeout") {
    it("unknown profile") { test_service_failures_distinguish_profile_state_spawn_and_timeout_5(0); }
    it("malformed numeric state") { test_service_failures_distinguish_profile_state_spawn_and_timeout_5(1); }
    it("numeric state with a malformed suffix") { test_service_failures_distinguish_profile_state_spawn_and_timeout_5(2); }
    it("spawn failure") { test_service_failures_distinguish_profile_state_spawn_and_timeout_5(3); }
    it("bounded polling timeout") { test_service_failures_distinguish_profile_state_spawn_and_timeout_5(4); }
}

[[maybe_unused]] static void test_service_controller_maps_every_terminal_shellresult_state_6(std::size_t row_0) {
  const auto expected = std::array{std::tuple<ProcessState, int, std::string>{ProcessState::TimedOut, -1, "timeout"}, std::tuple<ProcessState, int, std::string>{ProcessState::Cancelled, -1, "cancelled"}, std::tuple<ProcessState, int, std::string>{ProcessState::WaitFailed, -1, "service_state_failed"}, std::tuple<ProcessState, int, std::string>{ProcessState::OutputLimitExceeded, -1, "service_state_failed"}, std::tuple<ProcessState, int, std::string>{ProcessState::Signaled, -1, "service_state_failed"}, std::tuple<ProcessState, int, std::string>{ProcessState::Exited, 17, "service_state_failed"}}.at(row_0);
  info("%s", praktor_test::message([&](std::ostream& out) { out << Praktor::Shell::processStateName(std::get<0>(expected)); }).c_str());

  ShellResult terminal;
  terminal.state = std::get<0>(expected);
  terminal.exit_code = std::get<1>(expected);
  ScriptedProcessRunner runner({terminal});
  ServiceController controller(runner, profiles());

  const auto result = controller.execute(params(SystemOperation::Status));

  check((!(result.task_result.success)));
  check((result.task_result.error_code == std::get<2>(expected)));
  check((result.task_result.error_phase == "query"));
  if (result.task_result.error_code == "service_state_failed") {
    check((result.task_result.error_details["process_state"].as<std::string>() ==
          Praktor::Shell::processStateName(terminal.state)));
    check((result.task_result.error_details["exit_code"].as<std::int64_t>() ==
          terminal.exit_code));
  }
}

spec("service controller maps every terminal ShellResult state") {
    for (std::size_t row_0 = 0; row_0 < 6; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_service_controller_maps_every_terminal_shellresult_state_6(row_0); }
    }
}

[[maybe_unused]] static void test_service_controller_rejects_unsupported_operations_before_querying_7() {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4)});
  ServiceController controller(runner, registry);
  auto request = params(SystemOperation::Status);
  request.operation = static_cast<SystemOperation>(99);

  const auto result = controller.execute(request);

  check((!(result.task_result.success)));
  check((result.task_result.error_code == "schema_invalid"));
  check((runner.calls.empty()));
}

spec("service controller rejects unsupported operations before querying") {
    it("complete case") { test_service_controller_rejects_unsupported_operations_before_querying_7(); }
}

[[maybe_unused]] static void test_service_executor_maps_controller_outputs_into_task_context_8() {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4)});
  Praktor::Execution::ServiceExecutor executor(runner, registry);

  Task task;
  task.name = "query_camera";
  task.action = TaskAction::Service;
  task.declared_runner = "service";
  task.specifics = params(SystemOperation::Status);
  WorkflowContext context;

  const auto result = executor.execute(task, context);

  check((result.success));
  check((context.getValueByPath("tasks.__root__.outputs.name").as<std::string>() ==
        "Retro Camera Service"));
  check((context.getValueByPath("tasks.__root__.outputs.operation").as<std::string>() ==
        "status"));
  check((context.getValueByPath("tasks.__root__.outputs.state").as<std::string>() ==
        "running"));
  check((!(context.getValueByPath("tasks.__root__.outputs.changed").as<bool>())));
  check((context.getValueByPath("tasks.__root__.outputs.duration_ms").as<int64_t>() >= 0));
}

spec("service executor maps controller outputs into task context") {
    it("complete case") { test_service_executor_maps_controller_outputs_into_task_context_8(); }
}

[[maybe_unused]] static void test_built_in_service_profile_uses_sc_exe_9() {
  ServiceProfileRegistry registry;
  const auto* profile = registry.findProfile("windows_scm");

  check((profile != nullptr));
  check((profile->program == "sc.exe"));
  check((profile->status_args == StrList{"query", "{service_name}"}));
  check((profile->start_args == StrList{"start", "{service_name}", "{arguments}"}));
  check((profile->stop_args == StrList{"stop", "{service_name}", "{arguments}"}));
}

spec("built-in service profile uses sc.exe") {
    it("complete case") { test_built_in_service_profile_uses_sc_exe_9(); }
}

[[maybe_unused]] static void test_service_controller_owns_a_frozen_profile_registry_snapshot_10() {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4)});
  ServiceController controller(runner, registry);

  ServiceCommandProfile late_profile;
  late_profile.name = "late";
  late_profile.program = "late-service-controller";
  registry.registerProfile(std::move(late_profile));

  auto request = params(SystemOperation::Status);
  request.profile = "late";
  const auto result = controller.execute(request);

  check((!(result.task_result.success)));
  check((result.task_result.error_code == "service_state_failed"));
  check((result.task_result.error_phase == "profile"));
  check((runner.calls.empty()));
}

spec("service controller owns a frozen profile registry snapshot") {
    it("complete case") { test_service_controller_owns_a_frozen_profile_registry_snapshot_10(); }
}

[[maybe_unused]] static void test_service_controller_safely_owns_a_temporary_profile_registry_11() {
  ScriptedProcessRunner runner({serviceState(4)});
  ServiceController controller(runner, profiles());

  const auto result = controller.execute(params(SystemOperation::Status));

  check((result.task_result.success));
  check((result.state == "running"));
  check((runner.calls.size() == 1));
  checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
}

spec("service controller safely owns a temporary profile registry") {
    it("complete case") { test_service_controller_safely_owns_a_temporary_profile_registry_11(); }
}


spec("C service compatibility metadata and exception boundary") {
  it("rejects embedded NUL before dispatch") {
    ScriptedProcessRunner runner({serviceState(4)});
    ServiceController controller(runner, profiles());
    auto request = params(SystemOperation::Start);
    request.arguments.push_back(std::string("safe\0unsafe",11));
    const auto result = controller.execute(request);
    check(!result.task_result.success && result.task_result.error_code == "schema_invalid");
    check(runner.calls.empty() && !result.changed);
  }
  it("contains standard and nonstandard runner exceptions at the C boundary") {
    ScriptedProcessRunner exhausted({});
    ServiceController controller(exhausted, profiles());
    const auto result = controller.execute(params(SystemOperation::Status));
    check(!result.task_result.success && result.task_result.error_code == "process_spawn_failed");
    check(result.task_result.error_message == "failed to run service command: fake runner script exhausted");
    class ThrowingRunner final : public IProcessRunner {
      ShellResult run(const ProcessSpec&) override { throw 7; }
    } runner;
    ServiceController other(runner, profiles());
    const auto unknown = other.execute(params(SystemOperation::Status));
    check(!unknown.task_result.success && unknown.task_result.error_code == "process_spawn_failed");
    check(unknown.task_result.error_message == "failed to run service command");
  }
}
