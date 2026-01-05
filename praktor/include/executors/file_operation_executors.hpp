#ifndef __FILE_OPERATION_EXECUTORS_HPP__
#define __FILE_OPERATION_EXECUTORS_HPP__

#include "dag/task_executor.hpp"
#include "yml/task_types.hpp"

namespace Praktor::Execution {

/**
 * @brief Executor for copy_file tasks
 */
class CopyFileExecutor : public TaskExecutor {
public:
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "copy_file"; }
};

/**
 * @brief Executor for move_file tasks  
 */
class MoveFileExecutor : public TaskExecutor {
public:
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "move_file"; }
};

/**
 * @brief Executor for create_directory tasks
 */
class CreateDirectoryExecutor : public TaskExecutor {
public:
    TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "create_directory"; }
};

// Factory functions
std::unique_ptr<TaskExecutor> createCopyFileExecutor();
std::unique_ptr<TaskExecutor> createMoveFileExecutor();
std::unique_ptr<TaskExecutor> createCreateDirectoryExecutor();

} // namespace Praktor::Execution

#endif // __FILE_OPERATION_EXECUTORS_HPP__
