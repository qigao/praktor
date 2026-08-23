#include "executors/program_executor.hpp"

#include "praktor/shell/process_executor.hpp"
#include "util/env_parser.hpp"
#include "util/logging.hpp"
#include "util/path_utils.hpp"
#include "util/variable_substitution.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>

namespace Praktor::Execution {

namespace {

using EnvMap = std::unordered_map<std::string, std::string>;

int parseDurationMs(const std::string& timeout) {
  if (timeout.empty()) {
    return 30000;
  }

  if (std::all_of(timeout.begin(), timeout.end(),
                  [](unsigned char ch) { return std::isdigit(ch); })) {
    return std::stoi(timeout);
  }

  if (timeout.size() < 2) {
    throw std::runtime_error("Invalid timeout format: " + timeout);
  }

  char unit = timeout.back();
  int value = std::stoi(timeout.substr(0, timeout.size() - 1));

  switch (unit) {
    case 's': return value * 1000;
    case 'm': return value * 60 * 1000;
    case 'h': return value * 60 * 60 * 1000;
    default: throw std::runtime_error("Invalid timeout unit in: " + timeout);
  }
}

bool hasPathSeparator(const std::string& value) {
  return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
}

std::string resolveProgramPath(const Task& task, const std::string& program) {
  std::filesystem::path path(program);
  if (path.is_absolute() || hasPathSeparator(program)) {
    return Praktor::util::resolveRelativePath(task.source_path, program).string();
  }
  return program;
}

std::string resolveWorkingDirectory(const Task& task, const WorkflowContext& context) {
  if (!task.working_dir) {
    return {};
  }
  const std::string substituted = substituteVariables(*task.working_dir, context);
  if (substituted.empty()) {
    return {};
  }
  return Praktor::util::resolveRelativePath(task.source_path, substituted).string();
}

void setContextEnvironmentValue(WorkflowContext& context,
                                const std::string& key,
                                const std::string& value) {
  context.setValue("env." + key, value);
}

EnvMap buildTaskEnvironment(const Task& task,
                            WorkflowContext& context,
                            const EnvMap& base_environment) {
  EnvMap env = base_environment;

  for (const auto& env_file : task.dot_env) {
    try {
      auto parsed = Praktor::util::parseDotEnvFile(
          Praktor::util::resolveRelativePath(task.source_path, env_file));
      for (const auto& [key, value] : parsed) {
        env[key] = substituteVariables(value, context);
      }
    } catch (const std::exception& e) {
      TLOG_WARNF("Failed to load task dotEnv file '{}': {}", env_file, e.what());
    }
  }

  for (const auto& [key, value] : task.env) {
    env[key] = substituteVariables(value, context);
  }

  for (const auto& [key, value] : env) {
    setContextEnvironmentValue(context, key, value);
  }

  return env;
}

void emitProcessConsoleLine(const std::string& line) {
  if (Praktor::Logging::isVerboseEnabled()) {
    return;
  }

  std::string message = "__PROCESS__:";
  message += line;
  Praktor::Logging::emitConsoleEvent(message);
}

WorkflowValue parseJsonOutput(const std::string& output) {
  try {
    return WorkflowValue::parse(output);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Failed to parse program stdout as JSON: ") + e.what());
  }
}

} // namespace

ProgramExecutor::ProgramExecutor(EnvMap base_environment)
    : base_environment_(std::move(base_environment)) {}

TaskResult ProgramExecutor::execute(const Task& task, WorkflowContext& context) {
  const auto& params = std::get<ProgramParams>(task.specifics);

  Praktor::Shell::ProcessSpec spec;
  spec.program = resolveProgramPath(task, substituteVariables(params.program, context));
  spec.input = substituteVariables(params.input, context);
  spec.working_dir = resolveWorkingDirectory(task, context);
  spec.timeout_ms = task.timeout ? parseDurationMs(*task.timeout) : 30000;
  spec.stream_output = !task.silent;

  spec.args.reserve(params.args.size());
  for (const auto& arg : params.args) {
    spec.args.push_back(substituteVariables(arg, context));
  }

  EnvMap env = buildTaskEnvironment(task, context, base_environment_);
  spec.env = std::map<std::string, std::string>(env.begin(), env.end());

  Praktor::Shell::ShellExecutor::setStreamCallback(emitProcessConsoleLine);
  auto start_time = std::chrono::high_resolution_clock::now();
  auto process_result = Praktor::Shell::ProcessExecutor::execute(spec);
  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

  context.setCurrentTaskOutput("exit_code", std::to_string(process_result.exit_code));
  context.setCurrentTaskOutput("stdout", process_result.stdout_output);
  context.setCurrentTaskOutput("stderr", process_result.stderr_output);
  context.setCurrentTaskOutput("execution_time_ms", std::to_string(duration.count()));

  if (params.output_format == CommandOutputFormat::Json) {
    context.setCurrentTaskOutput("data", parseJsonOutput(process_result.stdout_output));
  }

  TaskResult result(process_result.success(),
                    process_result.success() ? "" :
                        "Program failed with exit code " + std::to_string(process_result.exit_code));
  result.exit_code = process_result.exit_code;
  result.stdout_data = process_result.stdout_output;
  result.stderr_data = process_result.stderr_output;
  result.output_streamed_live = process_result.output_streamed_live;

  if (!process_result.success() && !process_result.stderr_output.empty()) {
    result.error_message += ". Stderr: " + process_result.stderr_output;
  }

  return result;
}

std::unique_ptr<TaskExecutor> createProgramExecutor(EnvMap base_environment) {
  return std::make_unique<ProgramExecutor>(std::move(base_environment));
}

} // namespace Praktor::Execution
