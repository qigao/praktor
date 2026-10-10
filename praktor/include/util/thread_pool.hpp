#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

namespace pubcxx {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency()) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                current_pool_ = this;
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex_);
                        this->condition_.wait(lock, [this] { return this->stop_ || !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty()) {
                            current_pool_ = nullptr;
                            return;
                        }
                        task = std::move(this->tasks_.front().run);
                        this->tasks_.pop_front();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto submit_task(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        return enqueueFor(nullptr, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // group is a borrowed identity, kept alive by its owner until all work drains.
    template<class F, class... Args>
    auto enqueueFor(const void* group, F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("submit_task on stopped ThreadPool");
            }
            tasks_.push_back(WorkItem{group, [task](){ (*task)(); }});
        }
        condition_.notify_one();
        return res;
    }

    // Alias for Praktor compatibility
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) {
        return submit_task(std::forward<F>(f), std::forward<Args>(args)...);
    }

    size_t num_threads() const {
        return workers_.size();
    }

    bool isWorkerThread() const noexcept {
        return current_pool_ == this;
    }

    bool hasQueuedWorkFor(const void* group) const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (const auto& task : tasks_) {
            if (task.group == group) return true;
        }
        return false;
    }

    // A worker waiting for a nested workflow helps only that workflow. Running
    // unrelated work here could re-enter an ancestor waiting on this stack.
    bool tryRunOneFor(const void* group) {
        if (!isWorkerThread()) return false;
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
                if (it->group == group) {
                    task = std::move(it->run);
                    tasks_.erase(it);
                    break;
                }
            }
        }
        if (!task) return false;
        task();
        return true;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }

private:
    struct WorkItem {
        const void* group;
        std::function<void()> run;
    };
    inline static thread_local ThreadPool* current_pool_ = nullptr;
    std::vector<std::thread> workers_;
    std::deque<WorkItem> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
};

} // namespace pubcxx

