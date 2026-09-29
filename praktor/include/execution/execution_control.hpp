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
    using CancellationProbe = bool (*)(void* user_data);
    using CancelProbe = bool (*)(void*) noexcept;

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

    ExecutionControl(CancellationProbe probe, void* probe_user_data,
                     std::optional<Clock::time_point> deadline = std::nullopt)
        : cancellation_probe_(probe)
        , cancellation_probe_user_data_(probe_user_data)
        , deadline_(deadline)
    {
    }

    ExecutionControl(std::optional<Clock::time_point> deadline,
                     CancelProbe cancel_probe,
                     void* cancel_probe_user_data) noexcept
        : deadline_(deadline)
        , cancel_probe_(cancel_probe)
        , cancel_probe_user_data_(cancel_probe_user_data)
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
        return observeCancellation(Clock::now()) != kNotRequested;
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
        const auto cancelled_at = observeCancellation(now);

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
    void observeExternalCancellation(Clock::time_point now) const noexcept
    {
        if (!cancellation_probe_ ||
            cancel_requested_at_ns_.load(std::memory_order_acquire) != kNotRequested) {
            return;
        }

        bool requested = false;
        try {
            requested = cancellation_probe_(cancellation_probe_user_data_);
        } catch (...) {
            // C-facing cancellation probes must not throw. Treat a violation as
            // a cancellation request so execution fails closed rather than
            // propagating an exception across the callback boundary.
            requested = true;
        }
        if (!requested) {
            return;
        }

        auto expected = kNotRequested;
        const auto observed_at = toNanoseconds(now);
        cancel_requested_at_ns_.compare_exchange_strong(
            expected, observed_at, std::memory_order_release,
            std::memory_order_relaxed);
    }

    static constexpr std::int64_t kNotRequested =
        std::numeric_limits<std::int64_t>::max();

    static std::int64_t toNanoseconds(Clock::time_point value) noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   value.time_since_epoch())
            .count();
    }

    std::int64_t observeCancellation(Clock::time_point observed_at) const noexcept
    {
        auto cancelled_at =
            cancel_requested_at_ns_.load(std::memory_order_acquire);
        if (cancelled_at != kNotRequested || !cancel_probe_) {
            return cancelled_at;
        }

        if (cancel_probe_(cancel_probe_user_data_)) {
            const auto requested_at = toNanoseconds(observed_at);
            auto expected = kNotRequested;
            cancel_requested_at_ns_.compare_exchange_strong(
                expected, requested_at,
                std::memory_order_release, std::memory_order_relaxed);
            cancelled_at =
                cancel_requested_at_ns_.load(std::memory_order_acquire);
        }
        return cancelled_at;
    }

    mutable std::atomic<std::int64_t> cancel_requested_at_ns_{kNotRequested};
    std::optional<Clock::time_point> deadline_;
    CancelProbe cancel_probe_ = nullptr;
    void* cancel_probe_user_data_ = nullptr;
};

} // namespace Praktor::Execution
