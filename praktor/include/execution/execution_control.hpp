#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>

namespace Praktor::Execution {

class ExecutionControl {
public:
    using Clock = std::chrono::steady_clock;

    enum class StopReason {
        None,
        Cancelled,
        DeadlineExceeded,
    };

    ExecutionControl() = default;

    explicit ExecutionControl(Clock::time_point deadline)
        : deadline_(deadline)
    {
    }

    void requestCancel() noexcept
    {
        const auto requested_at = toNanoseconds(Clock::now());
        auto expected = kNotRequested;
        cancel_requested_at_ns_.compare_exchange_strong(
            expected, requested_at, std::memory_order_release, std::memory_order_relaxed);
    }

    bool cancellationRequested() const noexcept
    {
        return cancel_requested_at_ns_.load(std::memory_order_acquire) != kNotRequested;
    }

    std::optional<Clock::time_point> deadline() const noexcept
    {
        return deadline_;
    }

    std::optional<Clock::duration> remainingDeadline() const noexcept
    {
        if (!deadline_) {
            return std::nullopt;
        }

        const auto now = Clock::now();
        if (now >= *deadline_) {
            return Clock::duration::zero();
        }
        return *deadline_ - now;
    }

    StopReason stopReason() const noexcept
    {
        const auto now = Clock::now();
        const auto cancelled_at =
            cancel_requested_at_ns_.load(std::memory_order_acquire);

        if (deadline_ && now >= *deadline_) {
            const auto deadline_ns = toNanoseconds(*deadline_);
            if (cancelled_at == kNotRequested || deadline_ns <= cancelled_at) {
                return StopReason::DeadlineExceeded;
            }
        }

        if (cancelled_at != kNotRequested) {
            return StopReason::Cancelled;
        }

        return StopReason::None;
    }

    bool stopRequested() const noexcept
    {
        return stopReason() != StopReason::None;
    }

private:
    static constexpr std::int64_t kNotRequested =
        std::numeric_limits<std::int64_t>::max();

    static std::int64_t toNanoseconds(Clock::time_point value) noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   value.time_since_epoch())
            .count();
    }

    std::atomic<std::int64_t> cancel_requested_at_ns_{kNotRequested};
    std::optional<Clock::time_point> deadline_;
};

} // namespace Praktor::Execution
