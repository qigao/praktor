#include "actions/thread_pool.hpp"
#include <algorithm>

namespace actions {

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this] {
      while (true) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(lock, [this] { 
            return this->stop_ || !this->tasks_.empty(); 
          });
          if (this->stop_ && this->tasks_.empty()) {
            return;
          }
          task = std::move(this->tasks_.front());
          this->tasks_.pop();
        }
        task();
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    stop_ = true;
  }
  condition_.notify_all();
  for (std::thread& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

ThreadPool& ThreadPool::instance() {
  // Global shared thread pool
  // Use 2x hardware concurrency for I/O-bound tasks, clamped to [4, 32]
  size_t cores = std::thread::hardware_concurrency();
  if (cores == 0) cores = 2;
  size_t optimal = std::clamp(cores * 2, size_t(4), size_t(32));
  
  static ThreadPool pool(optimal);
  return pool;
}

} // namespace actions
