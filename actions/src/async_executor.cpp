#include "actions/async_executor.hpp"
#include "core/executor.hpp"
#include "actions/shell_executor.hpp"
#include "actions/thread_pool.hpp"

namespace actions {

// Template specialization for cancel() implementation
template<>
void AsyncTaskImpl<NodeStatus>::cancel() {
  if (pid_ > 0) {
    ShellExecutor::killProcess(pid_);
  }
  // Note: std::future doesn't support cancellation
}

std::shared_ptr<AsyncTask> AsyncExecutor::submit(std::function<NodeStatus()> func) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Use actions's shared thread pool instead of std::async
  // This prevents thread explosion and provides better resource control
  auto future = ThreadPool::instance().submit(std::move(func));
  auto task = std::make_shared<AsyncTaskImpl<NodeStatus>>(std::move(future));
  tasks_.push_back(task);

  return task;
}

void AsyncExecutor::cleanup() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Remove completed tasks
  tasks_.erase(
    std::remove_if(tasks_.begin(), tasks_.end(),
      [](const std::shared_ptr<AsyncTask>& task) {
        return task->isDone();
      }),
    tasks_.end()
  );
}

void AsyncExecutor::cancelAll() {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& task : tasks_) {
    task->cancel();
  }

  tasks_.clear();
}

size_t AsyncExecutor::activeCount() const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (const auto& task : tasks_) {
    if (!task->isDone()) {
      count++;
    }
  }

  return count;
}

} // namespace actions
