#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace actions {

/**
 * @brief Simple thread pool for actions async execution
 * 
 * Provides a fixed-size thread pool to execute tasks asynchronously.
 * This prevents thread explosion when executing Parallel nodes.
 */
class ThreadPool {
public:
  explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
  ~ThreadPool();

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  template<class F, class... Args>
  auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      if (stop_) {
        throw std::runtime_error("submit on stopped ThreadPool");
      }
      tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return res;
  }

  size_t num_threads() const { return workers_.size(); }

  // Get global shared instance
  static ThreadPool& instance();

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_;
};

} // namespace actions
