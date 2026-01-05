#include "executors/file_operation_executors.hpp"
#include "util/variable_substitution.hpp"
#include "fmtlog.h"

#include <filesystem>
#include <variant>

namespace Praktor::Execution {

TaskResult CopyFileExecutor::execute(const Task& task, WorkflowContext& context) {
    logi("Copying file for task: {}", task.name);

    try {
        const auto& params = std::get<CopyFileParams>(task.specifics);

        std::string source = substituteVariables(params.source, context);
        std::string destination = substituteVariables(params.destination, context);

        if (params.overwrite || !std::filesystem::exists(destination)) {
            std::filesystem::copy_file(
                source,
                destination,
                params.overwrite
                    ? std::filesystem::copy_options::overwrite_existing
                    : std::filesystem::copy_options::none
            );

            logi("Successfully copied file from {} to {}", source, destination);
            return TaskResult(true);
        } else {
            return TaskResult(false, fmt::format("Destination file exists and overwrite is false: {}", destination));
        }

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, fmt::format("Task does not contain CopyFileParams: {}", e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, fmt::format("File copy failed: {}", e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, fmt::format("Copy file execution failed: {}", e.what()));
    }
}

TaskResult MoveFileExecutor::execute(const Task& task, WorkflowContext& context) {
    logi("Moving file for task: {}", task.name);

    try {
        const auto& params = std::get<MoveFileParams>(task.specifics);

        std::string source = substituteVariables(params.source, context);
        std::string destination = substituteVariables(params.destination, context);

        if (params.overwrite && std::filesystem::exists(destination)) {
            std::filesystem::remove(destination);
        }

        std::filesystem::rename(source, destination);

        logi("Successfully moved file from {} to {}", source, destination);
        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, fmt::format("Task does not contain MoveFileParams: {}", e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, fmt::format("File move failed: {}", e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, fmt::format("Move file execution failed: {}", e.what()));
    }
}

TaskResult CreateDirectoryExecutor::execute(const Task& task, WorkflowContext& context) {
    logi("Creating directory for task: {}", task.name);

    try {
        const auto& params = std::get<CreateDirectoryParams>(task.specifics);

        std::string path = substituteVariables(params.path, context);
        std::filesystem::path dirPath(path);

        if (params.parents) {
            std::filesystem::create_directories(dirPath);
        } else {
            std::filesystem::create_directory(dirPath);
        }

        if (!std::filesystem::exists(dirPath)) {
            return TaskResult(false, fmt::format("Failed to create directory: {}", path));
        }

        logi("Successfully created directory: {}", path);
        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, fmt::format("Task does not contain CreateDirectoryParams: {}", e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, fmt::format("Directory creation failed: {}", e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, fmt::format("Create directory execution failed: {}", e.what()));
    }
}

std::unique_ptr<TaskExecutor> createCopyFileExecutor() {
    return std::make_unique<CopyFileExecutor>();
}

std::unique_ptr<TaskExecutor> createMoveFileExecutor() {
    return std::make_unique<MoveFileExecutor>();
}

std::unique_ptr<TaskExecutor> createCreateDirectoryExecutor() {
    return std::make_unique<CreateDirectoryExecutor>();
}

} // namespace Praktor::Execution
