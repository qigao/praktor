#include "dag/task_executor_pool.hpp"
#include "fmtlog.h"
#include <iostream>

namespace Prakter::Execution
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
    
    logi("=== Executor Pool Performance Stats ===");
    logi("Registered executor types: {}", stats.registered_types);
    logi("Total cached executors: {}", stats.total_executors);
    logi("Cache hits: {}", stats.cache_hits);
    logi("Cache misses: {}", stats.cache_misses);
    
    if (stats.cache_hits + stats.cache_misses > 0) {
      double hit_rate = static_cast<double>(stats.cache_hits) / 
                       (stats.cache_hits + stats.cache_misses) * 100.0;
      logi("Cache hit rate: {:.2f}%", hit_rate);
      
      // Performance analysis
      if (hit_rate > 80.0) {
        logi("EXCELLENT: High cache efficiency - significant performance gain!");
      } else if (hit_rate > 50.0) {
        logi("GOOD: Decent cache efficiency - moderate performance gain");
      } else {
        logi("WARNING: Low cache efficiency - consider workload analysis");
      }
    }
    logi("=========================================");
  }
  
  static void resetStats()
  {
    auto& pool = TaskExecutorPool::getInstance();
    pool.clear();
    logi("Executor pool stats reset");
  }
};

}  // namespace Prakter::Execution
