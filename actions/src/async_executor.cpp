#include "actions/async_executor.hpp"
#include "core/executor.hpp"
#include "actions/shell_executor.hpp"
#include "actions/thread_pool.hpp"

namespace actions {

namespace {

thread_local std::shared_ptr<CancellationContext> g_cancellation_context;

} // namespace

void CancellationContext::setHandler(std::function<void()> handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  handler_ = std::move(handler);
}

void CancellationContext::clearHandler() {
  std::lock_guard<std::mutex> lock(mutex_);
  handler_ = {};
}

bool CancellationContext::requestCancel() {
  std::function<void()> handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cancellation_requested_) {
      return false;
    }
    cancellation_requested_ = true;
    handler = handler_;
  }
  if (handler) {
    handler();
  }
  return true;
}

bool CancellationContext::isCancellationRequested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancellation_requested_;
}

void ShellExecutor::setCancellationContext(
    std::shared_ptr<CancellationContext> cancellation) {
  g_cancellation_context = std::move(cancellation);
}

std::shared_ptr<CancellationContext> ShellExecutor::getCancellationContext() {
  return g_cancellation_context;
}

// Template specialization for cancel() implementation
template<>
void AsyncTaskImpl<NodeStatus>::cancel() {
  if (cancellation_) {
    cancellation_->requestCancel();
  }
}

std::shared_ptr<AsyncTask> AsyncExecutor::submit(std::function<NodeStatus()> func) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Use actions's shared thread pool instead of std::async
  // This prevents thread explosion and provides better resource control
  auto stream_callback = ShellExecutor::getStreamCallback();
  auto cancellation = std::make_shared<CancellationContext>();
  auto future = ThreadPool::instance().submit(
      [func = std::move(func), stream_callback = std::move(stream_callback),
       cancellation]() mutable {
        auto previous_callback = ShellExecutor::getStreamCallback();
        ShellExecutor::setStreamCallback(std::move(stream_callback));
        ShellExecutor::setCancellationContext(cancellation);
        try {
          auto result = func();
          ShellExecutor::setCancellationContext({});
          ShellExecutor::setStreamCallback(std::move(previous_callback));
          return result;
        } catch (...) {
          ShellExecutor::setCancellationContext({});
          ShellExecutor::setStreamCallback(std::move(previous_callback));
          throw;
        }
      });
  auto task = std::make_shared<AsyncTaskImpl<NodeStatus>>(std::move(future), cancellation);
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
