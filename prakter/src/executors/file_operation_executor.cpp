#include "executors/file_operation_executor.hpp"
#include "fmtlog.h"
#include "util/variable_substitution.hpp"

#include <filesystem>
#include <variant>

namespace Prakter::Execution
{

TaskResult CreateDirectoryExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    logd("Attempting to execute CreateDirectory task: {}", task.name);
    const auto& params = std::get<CreateDirectoryParams>(task.specifics);

    logi("Creating directory: {}", params.path);
    std::string resolved_path = substituteVariables(params.path, context);
    std::filesystem::path dirPath(resolved_path);

    if (params.parents) {
      std::filesystem::create_directories(dirPath);
    } else {
      std::filesystem::create_directory(dirPath);
    }

    if (!std::filesystem::exists(dirPath)) {
      return TaskResult(false, fmt::format("Failed to create directory: {}", resolved_path));
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, fmt::format("Task does not contain CreateDirectoryParams: {}", e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, fmt::format("CreateDirectory execution failed: {}", e.what()));
  }
}

TaskResult CopyFileExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<CopyFileParams>(task.specifics);

    std::string resolved_source = substituteVariables(params.source, context);
    std::string resolved_dest = substituteVariables(params.destination, context);

    logi("Copying file from {} to {}", resolved_source, resolved_dest);

    if (params.overwrite || !std::filesystem::exists(resolved_dest)) {
      std::filesystem::copy_file(
          resolved_source,
          resolved_dest,
          params.overwrite
              ? std::filesystem::copy_options::overwrite_existing
              : std::filesystem::copy_options::none);
    }

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, fmt::format("Task does not contain CopyFileParams: {}", e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, fmt::format("CopyFile execution failed: {}", e.what()));
  }
}

TaskResult MoveFileExecutor::execute(const Task& task, WorkflowContext& context)
{
  try {
    const auto& params = std::get<MoveFileParams>(task.specifics);

    std::string resolved_source = substituteVariables(params.source, context);
    std::string resolved_dest = substituteVariables(params.destination, context);

    logi("Moving file from {} to {}", resolved_source, resolved_dest);

    if (params.overwrite && std::filesystem::exists(resolved_dest)) {
      std::filesystem::remove(resolved_dest);
    }
    std::filesystem::rename(resolved_source, resolved_dest);

    return TaskResult(true);

  } catch (const std::bad_variant_access& e) {
    return TaskResult(false, fmt::format("Task does not contain MoveFileParams: {}", e.what()));
  } catch (const std::exception& e) {
    return TaskResult(false, fmt::format("MoveFile execution failed: {}", e.what()));
  }
}

std::unique_ptr<TaskExecutor> createCreateDirectoryExecutor()
{
  return std::make_unique<CreateDirectoryExecutor>();
}

std::unique_ptr<TaskExecutor> createCopyFileExecutor()
{
  return std::make_unique<CopyFileExecutor>();
}

std::unique_ptr<TaskExecutor> createMoveFileExecutor()
{
  return std::make_unique<MoveFileExecutor>();
}

}  // namespace Prakter::Execution
