#include "btdsl/async_executor.hpp"
#include "core/executor.hpp"
#include "btdsl/shell_executor.hpp"

namespace btdsl {

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

  auto task = std::make_shared<AsyncTaskImpl<NodeStatus>>(std::move(func));
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

} // namespace btdsl
