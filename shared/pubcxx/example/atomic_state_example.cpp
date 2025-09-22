#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

enum class TaskState { IDLE, RUNNING, PAUSED, STOPPED };
std::atomic<TaskState> state{TaskState::IDLE};

// Helper function to attempt state transition
bool transition_to(TaskState expected, TaskState desired) {
    return state.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire);
}

// Worker thread function
void worker(int id) {
    while (true) {
        TaskState current = state.load(std::memory_order_acquire);
        switch (current) {
            case TaskState::IDLE:
                std::cout << "Worker " << id << ": Idling\n";
                break;
            case TaskState::RUNNING:
                std::cout << "Worker " << id << ": Processing task\n";
                break;
            case TaskState::PAUSED:
                std::cout << "Worker " << id << ": Paused\n";
                break;
            case TaskState::STOPPED:
                std::cout << "Worker " << id << ": Stopping\n";
                return;   // Exit thread
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // Simulate work
    }
}

// Controller thread function to manage state transitions
void controller() {
    using namespace std::chrono_literals;

    // Transition to RUNNING
    std::this_thread::sleep_for(1s);
    if (transition_to(TaskState::IDLE, TaskState::RUNNING)) {
        std::cout << "Controller: Transitioned to RUNNING\n";
    } else {
        std::cout << "Controller: Failed to transition to RUNNING\n";
    }

    // Try invalid transition (RUNNING to STOPPED)
    std::this_thread::sleep_for(1s);
    if (transition_to(TaskState::RUNNING, TaskState::STOPPED)) {
        std::cout << "Controller: Transitioned to STOPPED\n";
    } else {
        std::cout << "Controller: Failed to transition to STOPPED (expected)\n";
    }

    // Transition to PAUSED
    std::this_thread::sleep_for(1s);
    if (transition_to(TaskState::RUNNING, TaskState::PAUSED)) {
        std::cout << "Controller: Transitioned to PAUSED\n";
    } else {
        std::cout << "Controller: Failed to transition to PAUSED\n";
    }

    // Transition to STOPPED
    std::this_thread::sleep_for(1s);
    if (transition_to(TaskState::PAUSED, TaskState::STOPPED)) {
        std::cout << "Controller: Transitioned to STOPPED\n";
    } else {
        std::cout << "Controller: Failed to transition to STOPPED\n";
    }
}

int main() {
    // Create 3 worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) { workers.emplace_back(worker, i + 1); }

    // Create controller thread
    std::thread ctrl(controller);

    // Join all threads
    ctrl.join();
    for (auto& w : workers) { w.join(); }

    std::cout << "Main: All threads completed\n";
    return 0;
}
