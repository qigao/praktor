#include "executors/service_executor.hpp"
#include "system/service_controller.hpp"

#include <catch2/catch_all.hpp>

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
  CHECK(call.program == "fake-service-controller");
  CHECK(call.args == expected);
}

}  // namespace

TEST_CASE("service controller parses numeric Windows SCM states") {
  auto registry = profiles();

  const auto expected = GENERATE(
      table<int, std::string>({{1, "stopped"},
                               {2, "start_pending"},
                               {3, "stop_pending"},
                               {4, "running"}}));
  ScriptedProcessRunner runner({serviceState(std::get<0>(expected))});
  ServiceController controller(runner, registry);

  const ServiceExecutionResult result = controller.execute(params(SystemOperation::Status));

  REQUIRE(result.task_result.success);
  CHECK(result.state == std::get<1>(expected));
  CHECK_FALSE(result.changed);
  REQUIRE(runner.calls.size() == 1);
  checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
}

TEST_CASE("service start and stop are idempotent at their target state") {
  auto registry = profiles();

  SECTION("start when already running") {
    ScriptedProcessRunner runner({serviceState(4)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Start));

    INFO(result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message);
    REQUIRE(result.task_result.success);
    CHECK(result.state == "running");
    CHECK_FALSE(result.changed);
    REQUIRE(runner.calls.size() == 1);
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
  }

  SECTION("stop when already stopped") {
    ScriptedProcessRunner runner({serviceState(1)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Stop));

    INFO(result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message);
    REQUIRE(result.task_result.success);
    CHECK(result.state == "stopped");
    CHECK_FALSE(result.changed);
    REQUIRE(runner.calls.size() == 1);
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
  }
}

TEST_CASE("service transitions preserve argv entries and mutate once") {
  auto registry = profiles();

  SECTION("start") {
    ScriptedProcessRunner runner(
        {serviceState(1), exited(), serviceState(2), serviceState(4)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Start));

    INFO(result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message);
    REQUIRE(result.task_result.success);
    CHECK(result.state == "running");
    CHECK(result.changed);
    REQUIRE(runner.calls.size() == 4);
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[1],
              {"start", "Retro Camera Service", "--mode", "safe value"});
    checkArgs(runner.calls[2], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[3], {"query", "Retro Camera Service"});
  }

  SECTION("stop") {
    ScriptedProcessRunner runner(
        {serviceState(4), exited(), serviceState(3), serviceState(1)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Stop));

    INFO(result.task_result.error_code << ":" << result.task_result.error_phase
                                      << ":" << result.task_result.error_message);
    REQUIRE(result.task_result.success);
    CHECK(result.state == "stopped");
    CHECK(result.changed);
    REQUIRE(runner.calls.size() == 4);
    checkArgs(runner.calls[0], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[1],
              {"stop", "Retro Camera Service", "--mode", "safe value"});
    checkArgs(runner.calls[2], {"query", "Retro Camera Service"});
    checkArgs(runner.calls[3], {"query", "Retro Camera Service"});
  }
}

TEST_CASE("service restart confirms stop before starting") {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4), exited(), serviceState(3),
                                serviceState(1), exited(), serviceState(2),
                                serviceState(4)});
  ServiceController controller(runner, registry);

  const auto result = controller.execute(params(SystemOperation::Restart));

  INFO(result.task_result.error_code << ":" << result.task_result.error_phase
                                    << ":" << result.task_result.error_message);
  REQUIRE(result.task_result.success);
  CHECK(result.state == "running");
  CHECK(result.changed);
  REQUIRE(runner.calls.size() == 7);
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

TEST_CASE("service failures distinguish profile state spawn and timeout") {
  auto registry = profiles();

  SECTION("unknown profile") {
    ScriptedProcessRunner runner({});
    ServiceController controller(runner, registry);
    auto request = params(SystemOperation::Status);
    request.profile = "missing";

    const auto result = controller.execute(request);

    CHECK_FALSE(result.task_result.success);
    CHECK(result.task_result.error_code == "service_state_failed");
    CHECK(runner.calls.empty());
  }

  SECTION("malformed numeric state") {
    ScriptedProcessRunner runner({exited("STATE : RUNNING\n")});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    CHECK_FALSE(result.task_result.success);
    CHECK(result.task_result.error_code == "service_state_failed");
  }

  SECTION("numeric state with a malformed suffix") {
    ScriptedProcessRunner runner({exited("STATE : 4oops\n")});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    CHECK_FALSE(result.task_result.success);
    CHECK(result.task_result.error_code == "service_state_failed");
  }

  SECTION("spawn failure") {
    ShellResult spawn_failure;
    spawn_failure.state = ProcessState::SpawnFailed;
    spawn_failure.exit_code = -1;
    spawn_failure.stderr_output = "cannot spawn";
    ScriptedProcessRunner runner({std::move(spawn_failure)});
    ServiceController controller(runner, registry);

    const auto result = controller.execute(params(SystemOperation::Status));

    CHECK_FALSE(result.task_result.success);
    CHECK(result.task_result.error_code == "process_spawn_failed");
  }

  SECTION("bounded polling timeout") {
    ScriptedProcessRunner runner(
        {serviceState(1), exited(), serviceState(2)}, true);
    ServiceController controller(runner, registry);
    auto request = params(SystemOperation::Start);
    request.timeout_ms = 100;

    const auto result = controller.execute(request);

    CHECK_FALSE(result.task_result.success);
    CHECK(result.task_result.error_code == "timeout");
    CHECK(result.state == "start_pending");
    REQUIRE(runner.calls.size() >= 3);
    checkArgs(runner.calls[1],
              {"start", "Retro Camera Service", "--mode", "safe value"});
  }
}

TEST_CASE("service controller rejects unsupported operations before querying") {
  auto registry = profiles();
  ScriptedProcessRunner runner({serviceState(4)});
  ServiceController controller(runner, registry);
  auto request = params(SystemOperation::Status);
  request.operation = static_cast<SystemOperation>(99);

  const auto result = controller.execute(request);

  CHECK_FALSE(result.task_result.success);
  CHECK(result.task_result.error_code == "schema_invalid");
  CHECK(runner.calls.empty());
}

TEST_CASE("service executor maps controller outputs into task context") {
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

  REQUIRE(result.success);
  CHECK(context.getValueByPath("tasks.__root__.outputs.name").as<std::string>() ==
        "Retro Camera Service");
  CHECK(context.getValueByPath("tasks.__root__.outputs.operation").as<std::string>() ==
        "status");
  CHECK(context.getValueByPath("tasks.__root__.outputs.state").as<std::string>() ==
        "running");
  CHECK_FALSE(context.getValueByPath("tasks.__root__.outputs.changed").as<bool>());
  CHECK(context.getValueByPath("tasks.__root__.outputs.duration_ms").as<int64_t>() >= 0);
}

TEST_CASE("built-in service profile uses sc.exe") {
  ServiceProfileRegistry registry;
  const auto* profile = registry.findProfile("windows_scm");

  REQUIRE(profile != nullptr);
  CHECK(profile->program == "sc.exe");
  CHECK(profile->status_args == StrList{"query", "{service_name}"});
  CHECK(profile->start_args == StrList{"start", "{service_name}", "{arguments}"});
  CHECK(profile->stop_args == StrList{"stop", "{service_name}", "{arguments}"});
}
