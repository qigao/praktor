#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace Praktor::Execution {

enum class ExecutionEventType {
    WorkflowStarted,
    TaskStarted,
    TaskProgress,
    TaskCompleted,
    TaskFailed,
    WorkflowCompleted,
};

struct ExecutionEvent {
    std::uint64_t sequence = 0;
    ExecutionEventType type = ExecutionEventType::WorkflowStarted;
    std::string task_name;
    std::string status;
    std::string message;
};

class ExecutionObserver {
public:
    using Sink = std::function<void(const ExecutionEvent&)>;

    explicit ExecutionObserver(Sink sink)
        : sink_(std::move(sink)) {}

    void emit(ExecutionEventType type,
              std::string task_name = {},
              std::string status = {},
              std::string message = {}) noexcept {
        if (!sink_) {
            return;
        }

        ExecutionEvent event;
        event.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
        event.type = type;
        event.task_name = std::move(task_name);
        event.status = std::move(status);
        event.message = std::move(message);

        try {
            sink_(event);
        } catch (...) {
            // Observation must never alter workflow execution semantics.
        }
    }

private:
    std::atomic<std::uint64_t> sequence_{0};
    Sink sink_;
};

} // namespace Praktor::Execution
