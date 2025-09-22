#include <stdexcept>

#include "dag/task_executor_pool.hpp"

namespace Weave::Execution
{

void TaskExecutorPool::registerExecutor(const std::string& taskType,
                                        CreateFunction creator)
{
  std::lock_guard<std::mutex> lock(mutex_);
  creators_[taskType] = creator;
}

std::shared_ptr<TaskExecutor> TaskExecutorPool::getExecutor(
    const std::string& taskType)
{
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if executor already exists in pool
  auto executor_it = executors_.find(taskType);
  if (executor_it != executors_.end()) {
    cache_hits_++;
    return executor_it->second;
  }

  // Create new executor if not found
  auto creator_it = creators_.find(taskType);
  if (creator_it == creators_.end()) {
    throw std::runtime_error("No executor registered for task type: "
                             + taskType);
  }

  // Create and cache the executor
  auto executor = creator_it->second();
  if (!executor) {
    throw std::runtime_error("Failed to create executor for task type: "
                             + taskType);
  }

  // Convert to shared_ptr and cache it
  std::shared_ptr<TaskExecutor> shared_executor(executor.release());
  executors_[taskType] = shared_executor;
  cache_misses_++;

  return shared_executor;
}

void TaskExecutorPool::clear()
{
  std::lock_guard<std::mutex> lock(mutex_);
  executors_.clear();
  cache_hits_ = 0;
  cache_misses_ = 0;
}

TaskExecutorPool::PoolStats TaskExecutorPool::getStats() const
{
  std::lock_guard<std::mutex> lock(mutex_);

  PoolStats stats;
  stats.total_executors = executors_.size();
  stats.registered_types = creators_.size();
  stats.cache_hits = cache_hits_;
  stats.cache_misses = cache_misses_;

  return stats;
}

}  // namespace Weave::Execution
