#ifndef __TASK_EXECUTOR_POOL_HPP__
#define __TASK_EXECUTOR_POOL_HPP__

#include "task_executor.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace Weave::Execution
{

/**
 * @brief Thread-safe executor pool for reusing TaskExecutor instances
 * 
 * Linus approach: "Good programmers worry about data structures"
 * This pool eliminates the performance overhead of repeated new/delete operations
 */
class TaskExecutorPool
{
public:
  using CreateFunction = std::function<std::unique_ptr<TaskExecutor>()>;
  
  static TaskExecutorPool& getInstance()
  {
    static TaskExecutorPool instance;
    return instance;
  }
  
  /**
   * @brief Register an executor type with its factory function
   */
  void registerExecutor(const std::string& taskType, CreateFunction creator);
  
  /**
   * @brief Get or create an executor instance (thread-safe)
   * @param taskType The type of task executor needed
   * @return Shared pointer to executor (never null if registered)
   */
  std::shared_ptr<TaskExecutor> getExecutor(const std::string& taskType);
  
  /**
   * @brief Clear all cached executors (for testing/cleanup)
   */
  void clear();
  
  /**
   * @brief Get pool statistics for monitoring
   */
  struct PoolStats {
    size_t total_executors = 0;
    size_t registered_types = 0;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
  };
  
  PoolStats getStats() const;

private:
  TaskExecutorPool() = default;
  ~TaskExecutorPool() = default;
  
  // Non-copyable, non-movable (singleton)
  TaskExecutorPool(const TaskExecutorPool&) = delete;
  TaskExecutorPool& operator=(const TaskExecutorPool&) = delete;
  TaskExecutorPool(TaskExecutorPool&&) = delete;
  TaskExecutorPool& operator=(TaskExecutorPool&&) = delete;
  
  mutable std::mutex mutex_;
  std::unordered_map<std::string, CreateFunction> creators_;
  std::unordered_map<std::string, std::shared_ptr<TaskExecutor>> executors_;
  
  // Statistics (mutable for const getStats())
  mutable size_t cache_hits_ = 0;
  mutable size_t cache_misses_ = 0;
};

}  // namespace Weave::Execution

#endif  // __TASK_EXECUTOR_POOL_HPP__