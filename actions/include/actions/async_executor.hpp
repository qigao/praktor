#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace actions {

enum class NodeStatus;
class ShellExecutor; // Forward declaration

class CancellationContext {
public:
  void setHandler(std::function<void()> handler);
  void clearHandler();
  bool requestCancel();
  bool isCancellationRequested() const;

private:
  mutable std::mutex mutex_;
  bool cancellation_requested_ = false;
  std::function<void()> handler_;
};

// Base class for async tasks
class AsyncTask {
public:
  virtual ~AsyncTask() = default;
  virtual bool isDone() const = 0;
  virtual NodeStatus getResult() = 0;
  virtual void cancel() = 0;

  // Monitoring interface
  virtual bool isRunning() const { return !isDone(); }
  virtual bool isTimeout(int max_seconds) const = 0;
  virtual int getPid() const { return -1; }
};

// Concrete async task implementation
template <typename T> class AsyncTaskImpl : public AsyncTask {
public:
  // Constructor accepting a pre-created future (from thread pool)
  explicit AsyncTaskImpl(std::future<NodeStatus> future,
                         std::shared_ptr<CancellationContext> cancellation)
      : future_(std::move(future)),
        start_time_(std::chrono::steady_clock::now()),
        cancellation_(std::move(cancellation)) {}

  bool isDone() const override {
    if (result_cached_) {
      return true;
    }
    return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  }

  NodeStatus getResult() override {
    if (!result_cached_) {
      result_ = future_.get();
      result_cached_ = true;
    }
    return result_;
  }

  void cancel() override;

  bool isTimeout(int max_seconds) const override {
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - start_time_)
                       .count();
    return elapsed > max_seconds;
  }

private:
  mutable std::future<NodeStatus> future_;
  std::chrono::steady_clock::time_point start_time_;
  std::shared_ptr<CancellationContext> cancellation_;
  NodeStatus result_{};
  bool result_cached_ = false;
};

// Manages async task execution
class AsyncExecutor {
public:
  // Submit an async task
  std::shared_ptr<AsyncTask> submit(std::function<NodeStatus()> func);

  // Cleanup completed tasks
  void cleanup();

  // Cancel all tasks
  void cancelAll();

  // Get number of active tasks
  size_t activeCount() const;

private:
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<AsyncTask>> tasks_;
};

} // namespace actions
