#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>


namespace btdsl {

enum class NodeStatus;
class ShellExecutor; // Forward declaration

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
  explicit AsyncTaskImpl(std::function<NodeStatus()> func)
      : future_(std::async(std::launch::async, std::move(func))),
        start_time_(std::chrono::steady_clock::now()) {}

  explicit AsyncTaskImpl(std::function<NodeStatus()> func, int pid)
      : future_(std::async(std::launch::async, std::move(func))),
        start_time_(std::chrono::steady_clock::now()), pid_(pid) {}

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

  int getPid() const override { return pid_; }

private:
  mutable std::future<NodeStatus> future_;
  std::chrono::steady_clock::time_point start_time_;
  int pid_ = -1;
  NodeStatus result_ = NodeStatus::RUNNING;
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

} // namespace btdsl
