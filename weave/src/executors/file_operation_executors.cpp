#include "executors/file_operation_executors.hpp"
#include "util/variable_substitution.hpp"
#include "util/logger.hpp"

#include <filesystem>
#include <variant>

namespace Weave::Execution {

TaskResult CopyFileExecutor::execute(const Task& task, WorkflowContext& context) {
    LOG_INFO("Copying file for task: " + task.name);

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

            LOG_INFO("Successfully copied file from " + source + " to " + destination);
            return TaskResult(true);
        } else {
            return TaskResult(false, "Destination file exists and overwrite is false: " + destination);
        }

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, "Task does not contain CopyFileParams: " + std::string(e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, "File copy failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, "Copy file execution failed: " + std::string(e.what()));
    }
}

TaskResult MoveFileExecutor::execute(const Task& task, WorkflowContext& context) {
    LOG_INFO("Moving file for task: " + task.name);

    try {
        const auto& params = std::get<MoveFileParams>(task.specifics);

        std::string source = substituteVariables(params.source, context);
        std::string destination = substituteVariables(params.destination, context);

        if (params.overwrite && std::filesystem::exists(destination)) {
            std::filesystem::remove(destination);
        }

        std::filesystem::rename(source, destination);

        LOG_INFO("Successfully moved file from " + source + " to " + destination);
        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, "Task does not contain MoveFileParams: " + std::string(e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, "File move failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, "Move file execution failed: " + std::string(e.what()));
    }
}

TaskResult CreateDirectoryExecutor::execute(const Task& task, WorkflowContext& context) {
    LOG_INFO("Creating directory for task: " + task.name);

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
            return TaskResult(false, "Failed to create directory: " + path);
        }

        LOG_INFO("Successfully created directory: " + path);
        return TaskResult(true);

    } catch (const std::bad_variant_access& e) {
        return TaskResult(false, "Task does not contain CreateDirectoryParams: " + std::string(e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        return TaskResult(false, "Directory creation failed: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return TaskResult(false, "Create directory execution failed: " + std::string(e.what()));
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

} // namespace Weave::Execution
