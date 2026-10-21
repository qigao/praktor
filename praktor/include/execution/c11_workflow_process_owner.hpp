#pragma once

// Explicit, opt-in ServiceHost-facing owner for C11 workflow Shell I/O.
// The default Praktor execution backend remains Legacy. This type is only
// available to consumers compiled with PRAKTOR_ENABLE_C11_ACTIONS=ON.
#if !defined(PRAKTOR_C11_ACTIONS_ENABLED) || !PRAKTOR_C11_ACTIONS_ENABLED
#error "C11WorkflowProcessOwner requires PRAKTOR_ENABLE_C11_ACTIONS=ON"
#endif

#include "workflow_runner.hpp"

#include <salts/process_retainer.h>
#include <salts/error_codes.h>

#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Praktor::Execution {

/*
 * Long-lived, bounded, single-driver owner.
 *
 * A ServiceHost or embedding application must retain this object and call
 * runReady() on a scheduled owner turn until it returns SALTS_OK with zero
 * pending leases. A permanent NativeIO error is returned to the host and
 * keeps the Process and its callback/buffer payload in a bounded slot; there
 * is no detached worker, second Process owner or forced release.
 *
 * execute(), runReady(), and close() use one nonblocking exclusive gate.
 * Concurrent attempts fail closed; an external ExecutionControl may still
 * request cancellation through its own atomics. The host must stop all
 * callers before destruction. Destruction without prior drain is a fatal
 * ownership-contract violation, never an implicit unsafe force-free.
 */
class C11WorkflowProcessOwner final {
public:
    explicit C11WorkflowProcessOwner(std::size_t capacity) {
        const int status = cflow_process_retainer_init(&retainer_, capacity);
        if (status != SALTS_OK) {
            throw std::invalid_argument(
                "C11 recovery owner initialization failed: " +
                std::to_string(status));
        }
    }

    ~C11WorkflowProcessOwner() noexcept {
        if (retainer_.impl != nullptr &&
            cflow_process_retainer_destroy(&retainer_) != SALTS_OK) {
            std::terminate();
        }
    }

    C11WorkflowProcessOwner(const C11WorkflowProcessOwner&) = delete;
    C11WorkflowProcessOwner& operator=(const C11WorkflowProcessOwner&) = delete;
    C11WorkflowProcessOwner(C11WorkflowProcessOwner&&) = delete;
    C11WorkflowProcessOwner& operator=(C11WorkflowProcessOwner&&) = delete;

    WorkflowExecutionResult execute(
        WorkflowRunner& runner,
        std::shared_ptr<ExecutionControl> control = {}) {
        OwnerTurn turn(active_);
        if (!turn.acquired()) {
            return unavailable("C11 Process owner already has an active driver");
        }
        if (retainer_.impl == nullptr) {
            return unavailable("C11 Process owner is closed");
        }
        return runner.executeWithC11ProcessOwner(&retainer_, std::move(control));
    }

    // Never loops or invents a terminal state. Returns SALTS_EBUSY when
    // another turn is executing or active leases remain.
    int runReady(std::size_t max_steps, std::size_t* out_reaped,
                 std::size_t* out_pending) noexcept {
        if (out_reaped != nullptr) *out_reaped = 0u;
        if (out_pending != nullptr) *out_pending = 0u;
        OwnerTurn turn(active_);
        if (!turn.acquired()) return SALTS_EBUSY;
        if (retainer_.impl == nullptr) return SALTS_EINVAL;
        return cflow_process_retainer_run_ready(
            &retainer_, max_steps, out_reaped, out_pending);
    }

    // Caller must retain this owner when EBUSY/error is returned. Successful
    // close is terminal; execute() and runReady() reject further work.
    int close() noexcept {
        OwnerTurn turn(active_);
        if (!turn.acquired()) return SALTS_EBUSY;
        if (retainer_.impl == nullptr) return SALTS_EALREADY;
        return cflow_process_retainer_destroy(&retainer_);
    }

private:
    class OwnerTurn {
    public:
        explicit OwnerTurn(std::atomic_flag& active) noexcept
            : active_(active),
              acquired_(!active_.test_and_set(std::memory_order_acquire)) {}
        ~OwnerTurn() noexcept {
            if (acquired_) active_.clear(std::memory_order_release);
        }
        OwnerTurn(const OwnerTurn&) = delete;
        OwnerTurn& operator=(const OwnerTurn&) = delete;
        bool acquired() const noexcept { return acquired_; }

    private:
        std::atomic_flag& active_;
        bool acquired_ = false;
    };

    static WorkflowExecutionResult unavailable(std::string reason) {
        WorkflowExecutionResult result{};
        result.success = false;
        result.error_message = std::move(reason);
        result.value = WorkflowValue::object();
        result.value["workflow_status"] = WorkflowValue("failed");
        return result;
    }

    cflow_process_retainer retainer_{};
    std::atomic_flag active_ = ATOMIC_FLAG_INIT;
};

} // namespace Praktor::Execution
