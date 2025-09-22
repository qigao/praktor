#include "dag/task_executor_pool.hpp"
#include "util/logger.hpp"
#include <iostream>

namespace Weave::Execution
{

/**
 * @brief Performance monitor for TaskExecutorPool
 * 
 * Linus approach: "Measure, don't guess"
 */
class ExecutorPoolMonitor
{
public:
  static void logPoolStats()
  {
    auto& pool = TaskExecutorPool::getInstance();
    auto stats = pool.getStats();
    
    LOG_INFO("=== Executor Pool Performance Stats ===");
    LOG_INFO("Registered executor types: " + std::to_string(stats.registered_types));
    LOG_INFO("Total cached executors: " + std::to_string(stats.total_executors));
    LOG_INFO("Cache hits: " + std::to_string(stats.cache_hits));
    LOG_INFO("Cache misses: " + std::to_string(stats.cache_misses));
    
    if (stats.cache_hits + stats.cache_misses > 0) {
      double hit_rate = static_cast<double>(stats.cache_hits) / 
                       (stats.cache_hits + stats.cache_misses) * 100.0;
      LOG_INFO("Cache hit rate: " + std::to_string(hit_rate) + "%");
      
      // Performance analysis
      if (hit_rate > 80.0) {
        LOG_INFO("EXCELLENT: High cache efficiency - significant performance gain!");
      } else if (hit_rate > 50.0) {
        LOG_INFO("GOOD: Decent cache efficiency - moderate performance gain");
      } else {
        LOG_INFO("WARNING: Low cache efficiency - consider workload analysis");
      }
    }
    LOG_INFO("=========================================");
  }
  
  static void resetStats()
  {
    auto& pool = TaskExecutorPool::getInstance();
    pool.clear();
    LOG_INFO("Executor pool stats reset");
  }
};

}  // namespace Weave::Execution