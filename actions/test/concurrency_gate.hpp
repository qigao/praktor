#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

// A bounded rendezvous proves overlapping execution without a speed threshold.
class ConcurrencyGate {
public:
    explicit ConcurrencyGate(std::size_t participants) : participants_(participants) {}

    bool arriveAndWait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        cv_.notify_all();
        return cv_.wait_for(lock, std::chrono::seconds(5),
                            [&] { return arrived_ >= participants_; });
    }

private:
    const std::size_t participants_;
    std::size_t arrived_ = 0;
    std::mutex mutex_;
    std::condition_variable cv_;
};
