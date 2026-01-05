#ifndef __TASK_EXECUTOR_FACTORY_HPP__
#define __TASK_EXECUTOR_FACTORY_HPP__

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include "task_executor.hpp"

namespace Praktor::Execution
{

class TaskExecutorFactory
{
public:
  using CreateFunction = std::function<std::unique_ptr<TaskExecutor>()>;
  
  static TaskExecutorFactory& getInstance()
  {
    static TaskExecutorFactory instance;
    return instance;
  }
  
  void registerExecutor(const std::string& taskType, CreateFunction creator)
  {
    creators_[taskType] = creator;
  }
  
  std::unique_ptr<TaskExecutor> createExecutor(const std::string& taskType)
  {
    auto it = creators_.find(taskType);
    if (it != creators_.end()) {
      return it->second();
    }
    return nullptr;
  }

private:
  std::unordered_map<std::string, CreateFunction> creators_;
  TaskExecutorFactory() = default;
};

}  // namespace Praktor::Execution

#endif  // __TASK_EXECUTOR_FACTORY_HPP__
