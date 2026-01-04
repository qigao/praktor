#ifndef __TASK_EXECUTION_EXCEPTION_HPP__
#define __TASK_EXECUTION_EXCEPTION_HPP__

#include <stdexcept>
#include <string>
#include "workflow_context.hpp"

/**
 * @class TaskExecutionException
 * @brief Exception thrown when a task execution fails with detailed context
 */
class TaskExecutionException : public std::runtime_error
{
public:
    TaskExecutionException(const std::string& message, const TaskFailureContext& context)
        : std::runtime_error(message), failure_context_(context)
    {
    }
    
    const TaskFailureContext& getFailureContext() const {
        return failure_context_;
    }

private:
    TaskFailureContext failure_context_;
};

#endif  // __TASK_EXECUTION_EXCEPTION_HPP__
