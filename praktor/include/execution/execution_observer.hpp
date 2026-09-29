#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
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

struct ExecutionLineage {
    std::string thread_id;
    std::string run_id;
    std::string turn_id;
    std::string tool_call_id;
};

struct ExecutionEvent {
    std::uint64_t sequence = 0;
    ExecutionEventType type = ExecutionEventType::WorkflowStarted;
    std::string task_name;
    std::string status;
    std::string payload_json;
};

class ExecutionObserver {
public:
    using Callback = std::function<void(const ExecutionEvent&, const ExecutionLineage&)>;

    ExecutionObserver(ExecutionLineage lineage, Callback callback)
        : lineage_(std::move(lineage)), callback_(std::move(callback)) {}

    void emit(ExecutionEventType type,
              std::string task_name = {},
              std::string status = {},
              std::string payload_json = {}) noexcept {
        if (!callback_) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ExecutionEvent event;
        event.sequence = ++sequence_;
        event.type = type;
        event.task_name = std::move(task_name);
        event.status = std::move(status);
        event.payload_json = std::move(payload_json);
        try {
            callback_(event, lineage_);
        } catch (...) {
            // Observability must never alter workflow execution semantics.
        }
    }

    const ExecutionLineage& lineage() const noexcept { return lineage_; }

private:
    ExecutionLineage lineage_;
    Callback callback_;
    mutable std::mutex mutex_;
    std::uint64_t sequence_ = 0;
};

} // namespace Praktor::Execution
