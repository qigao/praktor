#include "dag/task_executor_pool.hpp"
#include "util/logging.hpp"
#include <iostream>

namespace Praktor::Execution
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
    
    TLOG_INFO("=== Executor Pool Performance Stats ===");
    TLOG_INFO("Registered executor types: {}", stats.registered_types);
    TLOG_INFO("Total cached executors: {}", stats.total_executors);
    TLOG_INFO("Cache hits: {}", stats.cache_hits);
    TLOG_INFO("Cache misses: {}", stats.cache_misses);
    
    if (stats.cache_hits + stats.cache_misses > 0) {
      double hit_rate = static_cast<double>(stats.cache_hits) / 
                       (stats.cache_hits + stats.cache_misses) * 100.0;
      TLOG_INFO("Cache hit rate: {:.2f}%", hit_rate);
      
      // Performance analysis
      if (hit_rate > 80.0) {
        TLOG_INFO("EXCELLENT: High cache efficiency - significant performance gain!");
      } else if (hit_rate > 50.0) {
        TLOG_INFO("GOOD: Decent cache efficiency - moderate performance gain");
      } else {
        TLOG_INFO("WARNING: Low cache efficiency - consider workload analysis");
      }
    }
    TLOG_INFO("=========================================");
  }
  
  static void resetStats()
  {
    auto& pool = TaskExecutorPool::getInstance();
    pool.clear();
    TLOG_INFO("Executor pool stats reset");
  }
};

}  // namespace Praktor::Execution
